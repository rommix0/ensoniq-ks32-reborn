#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "check.h"
#include "ks32/machine.h"

using namespace ens::ks32;

namespace {

// Places 16-bit words at CPU addresses in 0xC00000-0xC1FFFF (upper EPROM = even bytes, lower =
// odd bytes); the wave ROMs are zero-filled to their sizes. Reset vectors: SSP 0x00FFFF00, PC 0x00C00100.
RomSet romWith(std::map<uint32_t, std::vector<uint16_t>> words) {
    RomSet r;
    r.osUpper.assign(0x20000, 0);
    r.osLower.assign(0x10000, 0);
    r.u25.assign(0x80000, 0);
    r.u26.assign(0x80000, 0);
    r.rom2.assign(0x100000, 0);
    r.rom3.assign(0x100000, 0);
    words[0xC00000] = {0x00FF, 0xFF00, 0x00C0, 0x0100};
    for (const auto& [addr, ws] : words) {
        uint32_t off = addr - 0xC00000;
        for (uint16_t w : ws) {
            r.osUpper[off >> 1] = static_cast<uint8_t>(w >> 8);
            r.osLower[off >> 1] = static_cast<uint8_t>(w);
            off += 2;
        }
    }
    return r;
}

std::unique_ptr<Machine> machineWith(const std::vector<uint16_t>& code) {
    std::string err;
    auto m = Machine::create(romWith({{0xC00100, code}}), err);
    CHECK(m != nullptr);
    return m;
}

constexpr uint16_t kBraSelf = 0x60FE;

}  // namespace

TEST(ks32_machine_rejects_wrong_rom_size) {
    std::string err;
    RomSet r = romWith({});
    r.osUpper.clear();
    CHECK(Machine::create(r, err) == nullptr);
    CHECK(err.find("OS upper") != std::string::npos);
    r = romWith({});
    r.u26.assign(0x7FFFF, 0);
    err.clear();
    CHECK(Machine::create(r, err) == nullptr);
    CHECK(err.find("u26") != std::string::npos);
}

TEST(ks32_machine_ram_write) {
    auto m = machineWith({0x33FC, 0x1234, 0x00FF, 0x0100, kBraSelf});
    int16_t l, r;
    m->runFrame(l, r);
    CHECK_EQ(m->peek16(0xFF0100), 0x1234);
    CHECK_EQ(m->peek16(0x000100), 0x1234);
}

TEST(ks32_machine_duart_write_is_traced) {
    auto m = machineWith({0x13FC, 0x0040, 0x0028, 0x0019, kBraSelf});
    std::vector<BusEvent> ev;
    m->onBusEvent = [&](const BusEvent& e) { ev.push_back(e); };
    int16_t l, r;
    m->runFrame(l, r);
    CHECK_EQ(m->duart().ivr(), 0x40);
    CHECK_EQ(ev.size(), 1u);
    if (ev.size() == 1) {
        CHECK(ev[0].dev == Device::Duart);
        CHECK(ev[0].write);
        CHECK_EQ(ev[0].addr, 0x280019u);
        CHECK_EQ(ev[0].value, 0x40);
        CHECK_EQ(ev[0].size, 1);
    }
}

TEST(ks32_machine_unmapped_is_traced) {
    auto m = machineWith({0x3039, 0x0030, 0x0000, kBraSelf});
    std::vector<BusEvent> ev;
    m->onBusEvent = [&](const BusEvent& e) { ev.push_back(e); };
    int16_t l, r;
    m->runFrame(l, r);
    CHECK_EQ(ev.size(), 1u);
    if (ev.size() == 1) {
        CHECK(ev[0].dev == Device::Unmapped);
        CHECK(!ev[0].write);
        CHECK_EQ(ev[0].addr, 0x300000u);
    }
    CHECK_EQ(m->cpu().regs().d[0] & 0xFFFF, 0u);
}

TEST(ks32_machine_frame_cadence) {
    auto m = machineWith({kBraSelf});
    int16_t l, r;
    for (int i = 0; i < 100; ++i) m->runFrame(l, r);
    CHECK(m->cycles() >= 51200 && m->cycles() <= 51260);
}

TEST(ks32_machine_peek_has_no_side_effects) {
    auto m = machineWith({kBraSelf});
    int events = 0;
    m->onBusEvent = [&](const BusEvent&) { ++events; };
    CHECK_EQ(m->peek16(0xC00100), kBraSelf);
    CHECK_EQ(m->peek16(0x280006), 0);
    CHECK_EQ(events, 0);
}

// K1: vectors read from ROM until the first write into 0-7; ROM odd bytes mirror every 0x20000.
TEST(ks32_machine_low_overlay_and_rom_mirror) {
    // MOVE.W #$5555,$000002 then BRA.S *
    auto m = machineWith({0x33FC, 0x5555, 0x0000, 0x0002, kBraSelf});
    CHECK_EQ(m->peek16(0x000004), 0x00C0);  // PC high word, from ROM
    CHECK_EQ(m->peek16(0xC20100), 0x00FC);  // odd byte 0xFC of word 33FC mirrors, even byte (upper EPROM's upper half) is 0
    int16_t l, r;
    m->runFrame(l, r);
    CHECK_EQ(m->peek16(0x000002), 0x5555);  // overlay gone, RAM visible
    CHECK_EQ(m->peek16(0x000004), 0x0000);
}

// Wrong-width accesses (K1): traced with the device and the access's own size and address.
TEST(ks32_machine_wrong_width_accesses_are_traced) {
    // MOVE.B $280018,D0 (even DUART byte); MOVE.W $280018,D1 (word -> odd byte, reg 12 = IVR 0x0F);
    // MOVE.B #1,$200001 (byte write to OTIS)
    auto m = machineWith({0x1039, 0x0028, 0x0018, 0x3239, 0x0028, 0x0018, 0x13FC, 0x0001, 0x0020, 0x0001, kBraSelf});
    std::vector<BusEvent> ev;
    m->onBusEvent = [&](const BusEvent& e) { ev.push_back(e); };
    int16_t l, r;
    m->runFrame(l, r);
    CHECK_EQ(ev.size(), 3u);
    if (ev.size() == 3) {
        CHECK(ev[0].dev == Device::Duart && ev[0].size == 1 && ev[0].addr == 0x280018u && !ev[0].write);
        CHECK(ev[1].dev == Device::Duart && ev[1].size == 2 && ev[1].addr == 0x280018u && !ev[1].write);
        CHECK(ev[2].dev == Device::Otis && ev[2].size == 1 && ev[2].write);
    }
    CHECK_EQ(m->cpu().regs().d[0] & 0xFF, 0u);
    CHECK_EQ(m->cpu().regs().d[1] & 0xFFFF, 0x0Fu);  // IVR read through a word access: low byte only
}

// K5: a halted ESP gets exactly 89 idle steps per frame, spread over the frame's slots. Each idle
// step refreshes (DBASE -= unit), so DBASE counts the steps; the CPU reads an ESP register about
// every 26 clocks, and the trace hook samples the count at each read.
TEST(ks32_machine_halted_esp_idle_spread) {
    // MOVE.B $260025,D0 ; BRA.S back to it
    auto m = machineWith({0x1039, 0x0026, 0x0025, 0x60F8});
    ens::Esp& esp = m->board().esp();
    esp.setHaltPin(true);
    esp.hostWrite(0x1F, 0);       // halt enable with HALT high: halted now (E24)
    esp.setGpr(244, 0xFF);        // MEMSIZ: unit 0x100
    esp.setGpr(245, 0xFFFF00);    // DLENGTH
    esp.setGpr(248, 0x800000);    // DBASE, far from the reload
    const uint32_t d0 = esp.agen().dbase;
    std::set<uint32_t> seen;  // idle steps done when each read is made
    m->onBusEvent = [&](const BusEvent&) { seen.insert((d0 - esp.agen().dbase) / 0x100); };
    int16_t l, r;
    m->runFrame(l, r);
    CHECK(esp.halted());
    CHECK_EQ(d0 - esp.agen().dbase, 89u * 0x100);
    CHECK(!seen.empty() && *seen.begin() == 0);  // the first read, in slot 0, comes before any step
    CHECK(seen.size() >= 16);                    // the count grows slot by slot, not in one bunch
    CHECK(!seen.empty() && *seen.rbegin() < 89);
}
