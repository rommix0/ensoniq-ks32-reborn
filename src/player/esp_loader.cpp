#include "player/esp_loader.h"

#include <utility>

namespace ens::player {

namespace {

constexpr uint8_t kSelRead = 0x80, kSelGpr = 0xA0, kSelInstr = 0xC0, kSelBoth = 0xE0;
constexpr uint8_t kHostCtl = 0x12, kRamCtl = 0x14, kSerCtl = 0x18, kHaltEnable = 0x1F;
constexpr uint8_t kCtlBusy = 4, kCtlClear = 2;
constexpr uint8_t kOutLevel = 0x7F;  // GPR 7F: output level, multiplies every program's output
constexpr uint32_t kDefaultLevel = 0x7FFF00;  // DECISION P11: master volume 127
constexpr uint64_t kEndWord = 0xFFFFFFFFFFFFull;
// DADR holds address bits 23..4; with MEMSIZ 0x0000FF (boot and every KS-32 program) the RAM word
// is DADR >> 8 (E27), so the host writes the word address into $0F/$10 and 0 into $11.
constexpr int kDadrShift = 8;

int32_t signed24(uint32_t v) { return static_cast<int32_t>((v & 0xFFFFFF) ^ 0x800000) - 0x800000; }

std::string hex2(int v) {
    constexpr char kDigits[] = "0123456789ABCDEF";
    return std::string{kDigits[(v >> 4) & 15], kDigits[v & 15]};
}

}  // namespace

void EspSequencer::gprData(uint32_t v24) {
    write(0x00, static_cast<uint8_t>(v24 >> 16));
    write(0x01, static_cast<uint8_t>(v24 >> 8));
    write(0x02, static_cast<uint8_t>(v24));
}

void EspSequencer::instrData(uint64_t w48) {
    for (int i = 0; i < 6; ++i) write(static_cast<uint8_t>(0x03 + i), static_cast<uint8_t>(w48 >> (40 - 8 * i)));
}

void EspSequencer::gprWrite(uint8_t addr, uint32_t v24) {
    gprData(v24);
    write(kSelGpr, addr);
    push(Kind::WaitAccess);
}

void EspSequencer::instrWrite(uint8_t addr, uint64_t w48) {
    instrData(w48);
    write(kSelInstr, addr);
    push(Kind::WaitAccess);
}

void EspSequencer::gprInstrWrite(uint8_t addr, uint32_t v24, uint64_t w48) {
    gprData(v24);
    instrData(w48);
    write(kSelBoth, addr);
    push(Kind::WaitAccess);
}

// DECISION P13: one host write of GPR 7F per frame over `frames` frames, linear in value and
// ending exactly at `to`; from == to (or frames <= 0) is a single write. Each step is data,
// select $A0, one frame, then Host Access OK: running, the transfer lands at that frame's END,
// so the next step's data never overwrite a pending request; halted, it has already landed.
void EspSequencer::ramp(uint32_t to, int frames) {
    const int32_t from = signed24(level_), target = signed24(to);
    const int steps = (frames <= 0 || from == target) ? 1 : frames;
    for (int k = 1; k <= steps; ++k) {
        const int64_t v = from + static_cast<int64_t>(target - from) * k / steps;
        gprData(static_cast<uint32_t>(v) & 0xFFFFFF);
        write(kSelGpr, kOutLevel);
        push(Kind::WaitFrames, 0, 1);
        push(Kind::WaitAccess);
    }
    level_ = to & 0xFFFFFF;
}

// DECISION P14: boot_init of esp_init_sequence.json (routine 0xC1C7E8) at start-up, without the
// read-back verification; then wait for the RAM clear, park the read select at $C0 and release
// HALT so the all-END program runs. The firmware's level copy starts at 0 (7F holds 0xFFFFFF, but
// the all-END program outputs nothing and the first load clears GPRs 00-8F).
void EspSequencer::queueBoot() {
    push(Kind::HaltPin, 0, 1);  // DUART OP6 reset: ESPHALT asserted
    write(kHaltEnable, 0x02);
    write(kHaltEnable, 0xFF);
    write(kSelBoth, 0x9F);  // GPR+INSTR write to 0x9F with whatever the latches hold
    push(Kind::WaitAccess);
    gprWrite(0xF4, 0x0000FF);  // MEMSIZ: 64K-word DRAM
    gprWrite(0xF5, 0xFFFFFF);  // DLENGTH
    gprWrite(0xF8, 0x000F00);  // DBASE
    write(kHostCtl, 0x03);     // RAM clear + refresh disable, then RAM clear (E25)
    write(kHostCtl, 0x02);
    write(kSerCtl, 0x63);      // slave, Sony format, SER3 out, SER2..0 in
    for (int a = 0x00; a <= 0x9F; ++a) gprInstrWrite(static_cast<uint8_t>(a), 0xFFFFFF, kEndWord);
    for (int a = 0xA0; a <= 0xBF; ++a) gprWrite(static_cast<uint8_t>(a), 0xFFFFFF);
    gprWrite(0xF9, 0xFFFFFF);  // SIGREG bit 22: product shift one
    for (int a = 0xEA; a <= 0xF1; ++a) gprWrite(static_cast<uint8_t>(a), 0);  // serial registers
    push(Kind::WaitClear);
    write(kSelRead, kSelInstr);
    push(Kind::WaitAccess);
    push(Kind::HaltPin, 0, 0);
    level_ = 0;
    q_.push_back(Op{Kind::Log, 0, 0, "esp booted"});
}

void EspSequencer::queueLoad(const EspProgram& p, int fadeFrames) {
    // 1-2. Fade out, then halt at once (E24).
    ramp(0, fadeFrames);
    push(Kind::HaltPin, 0, 1);
    write(kHaltEnable, 0x02);
    // 3-4. DLENGTH over the whole DRAM, then clear it.
    gprWrite(0xF5, 0xFFFFFF);
    write(kHostCtl, 0x03);
    write(kHostCtl, 0x02);
    push(Kind::WaitClear);
    // 5. GPR 00-8F = 0.
    for (int a = 0x00; a <= 0x8F; ++a) gprWrite(static_cast<uint8_t>(a), 0);
    // 6. DECISION P12: the JSON lost the record order. Type-1 range via $E0 (GPR + INSTR, the
    // GPR value from the program or 0), type-2 range via $C0 (INSTR only), then every other GPR
    // record ascending via $A0 (so the SPRs F4..F9 come last).
    const int t1 = p.type1Count, t12 = p.type1Count + p.type2Count;
    auto instrAt = [&](int i) { return i < static_cast<int>(p.instr.size()) ? p.instr[static_cast<size_t>(i)] : kEndWord; };
    auto gprAt = [&](int a) {
        for (const auto& g : p.gpr)
            if (g.first == a) return g.second;
        return 0u;
    };
    for (int i = 0; i < t1; ++i) gprInstrWrite(static_cast<uint8_t>(i), gprAt(i), instrAt(i));
    for (int i = t1; i < t12; ++i) instrWrite(static_cast<uint8_t>(i), instrAt(i));
    for (const auto& g : p.gpr)
        if (g.first >= t1) gprWrite(g.first, g.second);
    // 7. DRAM tables: $14 = write RAM, then per word DOL $0C/$0D and DADR $11, $10, $0F (the
    // $0F write performs the access, E27).
    if (!p.tables.empty()) write(kRamCtl, 0x7F);
    for (const DramTable& t : p.tables) {
        for (size_t k = 0; k < t.words.size(); ++k) {
            const uint32_t dadr = (t.wordAddr + static_cast<uint32_t>(k)) << kDadrShift;
            write(0x0C, static_cast<uint8_t>(t.words[k] >> 8));
            write(0x0D, static_cast<uint8_t>(t.words[k]));
            write(0x11, static_cast<uint8_t>(dadr));
            write(0x10, static_cast<uint8_t>(dadr >> 8));
            write(0x0F, static_cast<uint8_t>(dadr >> 16));
        }
    }
    // 8. Park the read select at $C0 (non-GPR), as the firmware does.
    write(kSelRead, kSelInstr);
    push(Kind::WaitAccess);
    // 9. Mix and handler GPRs (P11), everything but the output level.
    uint32_t target = kDefaultLevel;
    for (const auto& m : p.mix) {
        if (m.first == kOutLevel) target = m.second;
        else gprWrite(m.first, m.second);
    }
    // 10-11. Release HALT (the program starts at pc 0) and fade in.
    push(Kind::HaltPin, 0, 0);
    ramp(target, fadeFrames);
    std::string msg = "effect " + std::to_string(p.index) + " loaded (id " + hex2(p.effectId);
    if (!p.name.empty()) msg += ", '" + p.name + "'";
    q_.push_back(Op{Kind::Log, 0, 0, msg + ")"});
}

void EspSequencer::run(ks32::Board& b) {
    while (!q_.empty()) {
        Op& op = q_.front();
        switch (op.kind) {
            case Kind::Write: b.espHostWrite(op.addr, static_cast<uint8_t>(op.v)); break;
            case Kind::HaltPin: b.esp().setHaltPin(op.v != 0); break;
            case Kind::WaitAccess:
                if (b.esp().hostRead(kHostCtl) & kCtlBusy) return;
                break;
            case Kind::WaitClear:
                if (b.esp().hostRead(kHostCtl) & kCtlClear) return;
                break;
            case Kind::WaitFrames:
                if (op.v > 0) {
                    --op.v;
                    return;
                }
                break;
            case Kind::Log: log_.push_back(std::move(op.msg)); break;
        }
        q_.pop_front();
    }
}

std::vector<std::string> EspSequencer::takeLog() {
    std::vector<std::string> out;
    out.swap(log_);
    return out;
}

}  // namespace ens::player
