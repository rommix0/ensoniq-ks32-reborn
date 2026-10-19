#include <cstdint>
#include <functional>
#include <set>
#include <vector>

#include "check.h"
#include "esp/esp.h"
#include "esp/esp_isa.h"

using ens::Esp;
using ens::EspBus;
using ens::esp::Instr;
using ens::esp::Op;
using ens::esp::RamOp;

namespace {

const uint64_t kEndWord = Instr{0, 0, 0, 0, Op::END, 0, false, false, RamOp::ReadDelay}.encode();
// MOV gpr0 > gpr0, RAM op read.
const uint64_t kNopWord = Instr{0, 0, 0, 0, Op::MOV, 0, false, false, RamOp::ReadDelay}.encode();
const uint64_t kWriteDelayNop =
    Instr{0, 0, 0, 0, Op::MOV, 0, false, false, RamOp::WriteDelay}.encode();
const uint64_t kMovToFifo =  // A > FIFO
    Instr{0, 0, 0, 0, Op::MOV, 5, false, false, RamOp::ReadDelay}.encode();

struct Access {
    bool io;
    uint32_t addr;
    uint16_t v;
};

struct HostBus : EspBus {
    std::vector<uint16_t> ram = std::vector<uint16_t>(0x10000, 0);
    std::vector<Access> writes;
    std::vector<Access> reads;
    std::function<void()> onWrite;
    uint16_t read(bool io, uint32_t a) override {
        reads.push_back({io, a, 0});
        return io ? 0x5A5A : ram[a & 0xFFFF];
    }
    void write(bool io, uint32_t a, uint16_t v) override {
        writes.push_back({io, a, v});
        if (!io) ram[a & 0xFFFF] = v;
        if (onWrite) onWrite();
    }
};

void hw3(Esp& e, uint8_t base, uint32_t v) {
    e.hostWrite(base, uint8_t(v >> 16));
    e.hostWrite(uint8_t(base + 1), uint8_t(v >> 8));
    e.hostWrite(uint8_t(base + 2), uint8_t(v));
}
uint32_t hr3(Esp& e, uint8_t base) {
    return (uint32_t(e.hostRead(base)) << 16) | (uint32_t(e.hostRead(uint8_t(base + 1))) << 8) |
           e.hostRead(uint8_t(base + 2));
}
void hwInstr(Esp& e, uint64_t w) {
    for (int i = 0; i < 6; i++) e.hostWrite(uint8_t(3 + i), uint8_t(w >> (40 - 8 * i)));
}
bool busy(Esp& e) { return (e.hostRead(0x12) & 4) != 0; }

// Halts the ESP at the END of program word 0 (all other words are zero).
void haltAtEnd(Esp& e) {
    e.setInstr(0, kEndWord);
    e.setHaltPin(true);
    e.runSample();
}

// Host select write while halted: each idleStep services one pending transfer.
void xfer(Esp& e, uint8_t sel, uint8_t addr) {
    e.hostWrite(sel, addr);
    for (int i = 0; i < 4 && busy(e); i++) e.idleStep();
}

}  // namespace

TEST(host_write_gpr_running) {
    HostBus bus;
    Esp e(bus);
    e.setInstr(0, kEndWord);
    hw3(e, 0x00, 0x123456);
    e.hostWrite(0xA0, 0x10);
    CHECK_EQ(e.hostRead(0x12) & 4, 4);
    CHECK_EQ(e.gpr(0x10), 0u);  // nothing lands before the END transfer slot
    e.runSample();
    CHECK_EQ(e.gpr(0x10), 0x123456u);
    CHECK_EQ(e.hostRead(0x12) & 4, 0);
}

TEST(host_read_instr) {
    HostBus bus;
    Esp e(bus);
    e.setInstr(0, kEndWord);
    const uint64_t x = 0x112233445566ull;
    e.setInstr(5, x);
    e.setGpr(5, 0xABCDEF);
    e.hostWrite(0x80, 5);
    CHECK_EQ(e.hostRead(0x12) & 4, 4);
    e.runSample();
    CHECK_EQ(e.hostRead(0x12) & 4, 0);
    CHECK_EQ(hr3(e, 0x00), 0xABCDEFu);
    CHECK_EQ(e.hostRead(0x03), 0x11);
    CHECK_EQ(e.hostRead(0x04), 0x22);
    CHECK_EQ(e.hostRead(0x05), 0x33);
    CHECK_EQ(e.hostRead(0x06), 0x44);
    CHECK_EQ(e.hostRead(0x07), 0x55);
    CHECK_EQ(e.hostRead(0x08), 0x66 & 0xF8);
    CHECK_EQ(e.hostRead(0x80), 5);  // select registers read back (datasheet 5.1.2 step 3)
}

TEST(host_write_instr_and_gpr) {
    HostBus bus;
    Esp e(bus);
    e.setInstr(0, kEndWord);
    hw3(e, 0x00, 0x00ABCD);
    hwInstr(e, 0x0102030405F8ull);
    e.hostWrite(0xE0, 7);
    e.runSample();
    CHECK_EQ(e.gpr(7), 0xABCDu);
    CHECK_EQ(e.instr(7), 0x0102030405F8ull);

    // $C0 writes only the instruction, $A0 only the GPR.
    hw3(e, 0x00, 0x111111);
    hwInstr(e, 0xAABBCCDDEEF8ull);
    e.hostWrite(0xC0, 8);
    e.runSample();
    CHECK_EQ(e.instr(8), 0xAABBCCDDEEF8ull);
    CHECK_EQ(e.gpr(8), 0u);
    e.hostWrite(0xA0, 9);
    e.runSample();
    CHECK_EQ(e.gpr(9), 0x111111u);
    CHECK_EQ(e.instr(9), 0u);
}

TEST(host_one_transfer_per_sample) {
    HostBus bus;
    Esp e(bus);
    e.setInstr(0, kEndWord);
    // Two requests without waiting for Host Access OK/: the hardware has a single address
    // latch, so the second request overwrites the first. Only one transfer happens per
    // sample (the surviving one); the first address is never written.
    hw3(e, 0x00, 0x111111);
    e.hostWrite(0xA0, 0x10);
    hw3(e, 0x00, 0x222222);
    e.hostWrite(0xA0, 0x11);
    e.runSample();
    CHECK_EQ(e.gpr(0x10), 0u);
    CHECK_EQ(e.gpr(0x11), 0x222222u);
    CHECK(!busy(e));
    e.runSample();  // nothing left pending: no further transfer
    CHECK_EQ(e.gpr(0x10), 0u);

    // Properly sequenced requests each take one sample.
    hw3(e, 0x00, 0x333333);
    e.hostWrite(0xA0, 0x12);
    e.runSample();
    CHECK_EQ(e.gpr(0x12), 0x333333u);
    hw3(e, 0x00, 0x444444);
    e.hostWrite(0xA0, 0x13);
    CHECK(busy(e));
    e.runSample();
    CHECK_EQ(e.gpr(0x13), 0x444444u);
}

TEST(host_transfer_reaches_spr_and_ignores_holes) {
    HostBus bus;
    Esp e(bus);
    e.setInstr(0, kEndWord);
    hw3(e, 0x00, 0x00F000);
    e.hostWrite(0xA0, 0xF8);  // DBASE
    e.runSample();
    // The transfer lands after the END-step DBASE decrement, so DBASE holds the written value.
    CHECK_EQ(e.agen().dbase, 0x00F000u);
    hw3(e, 0x00, 0x123456);
    e.hostWrite(0xA0, 200);  // hole 192..233: write ignored
    e.runSample();
    e.hostWrite(0x80, 200);
    e.runSample();
    CHECK_EQ(hr3(e, 0x00), 0u);
    // Instruction accesses beyond word 159 are ignored / read 0.
    hwInstr(e, 0x112233445566ull);
    e.hostWrite(0xC0, 0xA0);
    e.runSample();
    e.hostWrite(0x80, 0xA0);
    e.runSample();
    CHECK_EQ(e.hostRead(0x03), 0x00);
}

// MOV gpr[b] > gpr[a] (opcode 9, operand select 0); its result commits in the following step.
uint64_t movWord(unsigned b, unsigned a) {
    return (uint64_t(9) << 12) | (uint64_t(b) << 24) | (uint64_t(a) << 16);
}

TEST(host_running_gpr_write_lands_at_end) {
    HostBus bus;
    Esp e(bus);
    e.setGpr(0x10, 0xAAAAAA);
    e.setInstr(0, movWord(0x10, 0x11));  // gpr11 = gpr10, read inside the sample
    e.setInstr(1, kEndWord);
    hw3(e, 0x00, 0x555555);
    e.hostWrite(0xA0, 0x10);
    e.runSample();
    CHECK_EQ(e.gpr(0x11), 0xAAAAAAu);  // word 0 saw the old value
    CHECK_EQ(e.gpr(0x10), 0x555555u);  // the transfer landed at END
    e.runSample();
    CHECK_EQ(e.gpr(0x11), 0x555555u);  // the next sample sees the new one
}

TEST(host_running_read_captures_value_computed_this_sample) {
    HostBus bus;
    Esp e(bus);
    e.setGpr(0x10, 0x123456);
    e.setInstr(0, movWord(0x10, 0x12));  // gpr12 = gpr10, committed before the END transfer
    e.setInstr(1, kEndWord);
    e.hostWrite(0x80, 0x12);
    e.runSample();
    CHECK_EQ(hr3(e, 0x00), 0x123456u);
}

TEST(halt_at_end) {
    HostBus bus;
    Esp e(bus);
    e.setInstr(0, kMovToFifo);
    e.setInstr(1, kEndWord);
    CHECK_EQ(e.runSample(), 2);
    CHECK_EQ(e.dolFifoSize(), 1);  // control: an END without HALT keeps the FIFO
    CHECK(!e.halted());

    e.setHaltPin(true);
    CHECK(!e.halted());  // level sampled only at END
    CHECK_EQ(e.runSample(), 2);
    CHECK(e.halted());
    CHECK_EQ(e.dolFifoSize(), 0);
    CHECK_EQ(e.runSample(), 0);
    CHECK_EQ(e.runSample(), 0);
    CHECK(e.halted());
    e.setHaltPin(false);
    CHECK(!e.halted());
    CHECK_EQ(e.runSample(), 2);  // resumes from pc 0
    CHECK_EQ(e.hostRead(0x16), 0);
}

TEST(halt_enable_immediate) {
    for (int pin = 0; pin < 2; pin++) {
        HostBus bus;
        Esp e(bus);
        for (int i = 0; i < 5; i++) e.setInstr(i, kNopWord);
        e.setInstr(1, kWriteDelayNop);  // its RAM cycle runs at step 2
        e.setInstr(5, kEndWord);
        e.setHaltPin(pin != 0);
        bus.onWrite = [&] { e.hostWrite(0x1F, 0xFF); };
        const int steps = e.runSample();
        if (pin) {
            CHECK_EQ(steps, 3);  // stopped mid-program, not at END
            CHECK(e.halted());
            CHECK_EQ(e.runSample(), 0);
            bus.onWrite = nullptr;
            e.setHaltPin(false);
            CHECK_EQ(e.runSample(), 6);  // restarts at pc 0 and runs to END
            CHECK_EQ(bus.writes.size(), 2u);  // one before the halt, one in the full rerun
        } else {
            CHECK_EQ(steps, 6);  // pin low: the halt-enable write does nothing
            CHECK(!e.halted());
        }
    }
}

TEST(host_ram_write_read_halted) {
    HostBus bus;
    Esp e(bus);
    e.setGpr(244, 0xFF);  // MEMSIZ
    haltAtEnd(e);
    CHECK(e.halted());
    e.hostWrite(0x14, 0x40);  // write, RAM
    e.hostWrite(0x0C, 0xAB);
    e.hostWrite(0x0D, 0xCD);
    e.hostWrite(0x10, 0x12);
    e.hostWrite(0x11, 0x30);
    CHECK(bus.writes.empty());  // DADR HI starts the cycle
    e.hostWrite(0x0F, 0x00);
    CHECK_EQ(bus.writes.size(), 1u);
    CHECK(!bus.writes[0].io);
    CHECK_EQ(bus.writes[0].addr, 0x12u);
    CHECK_EQ(bus.writes[0].v, 0xABCD);
    CHECK_EQ(e.hostRead(0x10), 0x12);
    CHECK_EQ(e.hostRead(0x11), 0x30);

    e.hostWrite(0x14, 0xC0);  // read, RAM
    e.hostWrite(0x0F, 0x00);
    CHECK_EQ(e.hostRead(0x09), 0xAB);
    CHECK_EQ(e.hostRead(0x0A), 0xCD);
    CHECK_EQ(e.hostRead(0x0B), 0x00);
    CHECK_EQ(bus.writes.size(), 1u);
}

TEST(host_ram_access_io_space_and_running) {
    HostBus bus;
    Esp e(bus);
    e.setGpr(244, 0xFF);
    haltAtEnd(e);
    // I/O select: the address bypasses MEMSIZ and is the 20-bit offset >> 4.
    e.hostWrite(0x14, 0x80);  // read, I/O
    e.hostWrite(0x10, 0x12);
    e.hostWrite(0x11, 0x30);
    e.hostWrite(0x0F, 0x01);  // DADR = 0x011230 -> I/O word 0x1123
    CHECK_EQ(bus.reads.back().addr, 0x1123u);
    CHECK(bus.reads.back().io);
    CHECK_EQ(e.hostRead(0x09), 0x5A);
    e.hostWrite(0x14, 0x00);  // write, I/O
    hw3(e, 0x0C, 0x7E0000);
    e.hostWrite(0x0F, 0x01);
    CHECK(bus.writes.back().io);
    CHECK_EQ(bus.writes.back().addr, 0x1123u);
    CHECK_EQ(bus.writes.back().v, 0x7E00);

    // Direct RAM access is only available while halted: a running ESP ignores the trigger.
    e.setHaltPin(false);
    const size_t n = bus.writes.size(), r = bus.reads.size();
    e.hostWrite(0x0F, 0x01);
    CHECK_EQ(bus.writes.size(), n);
    CHECK_EQ(bus.reads.size(), r);
}

TEST(ram_clear) {
    HostBus bus;
    Esp e(bus);
    e.setGpr(244, 0xFF);
    e.setGpr(245, 0x00FF00);
    haltAtEnd(e);
    for (auto& w : bus.ram) w = 0xFFFF;
    bus.writes.clear();
    e.hostWrite(0x12, 2);
    CHECK_EQ(e.hostRead(0x12) & 2, 2);
    int steps = 0;
    while ((e.hostRead(0x12) & 2) && steps < 100000) {
        e.idleStep();
        ++steps;
    }
    CHECK_EQ(steps, 256);
    CHECK_EQ(bus.writes.size(), 256u);
    std::set<uint32_t> seen;
    bool allZero = true, anyIo = false;
    for (const Access& a : bus.writes) {
        seen.insert(a.addr);
        allZero = allZero && a.v == 0;
        anyIo = anyIo || a.io;
    }
    CHECK_EQ(seen.size(), 256u);
    CHECK_EQ(*seen.begin(), 0u);
    CHECK_EQ(*seen.rbegin(), 0xFFu);
    CHECK(allZero);
    CHECK(!anyIo);
    CHECK_EQ(e.agen().dbase, 0u);
    CHECK_EQ(bus.ram[0x00], 0);
    CHECK_EQ(bus.ram[0xFF], 0);
    CHECK_EQ(bus.ram[0x100], 0xFFFF);  // only the configured delay memory is cleared

    // Refresh starts when the clear completes: DBASE wraps back to DLENGTH.
    e.idleStep();
    CHECK_EQ(e.agen().dbase, 0xFF00u);
    e.idleStep();
    CHECK_EQ(e.agen().dbase, 0xFE00u);
    CHECK_EQ(bus.writes.size(), 256u);
}

TEST(ram_clear_needs_halt_and_refresh_enabled) {
    HostBus bus;
    Esp e(bus);
    e.setGpr(244, 0xFF);
    e.setGpr(245, 0x00FF00);
    // Running: RAM Clear is ignored (only meaningful during Halt).
    e.hostWrite(0x12, 2);
    CHECK_EQ(e.hostRead(0x12) & 2, 0);
    haltAtEnd(e);
    // Refresh Disable must be 0 when activating RAM Clear.
    e.hostWrite(0x12, 3);
    CHECK_EQ(e.hostRead(0x12) & 3, 1);
    for (int i = 0; i < 5; i++) e.idleStep();
    CHECK(bus.writes.empty());
    e.hostWrite(0x12, 2);
    CHECK_EQ(e.hostRead(0x12) & 3, 2);
    e.idleStep();
    CHECK_EQ(bus.writes.size(), 1u);
    // Writing bit 1 = 0 aborts (power-up step 4: reset the RAM Clear bit).
    e.hostWrite(0x12, 0);
    CHECK_EQ(e.hostRead(0x12) & 2, 0);
    e.idleStep();
    CHECK_EQ(bus.writes.size(), 1u);
    // Releasing HALT also abandons a clear in progress; the bit does not stay stuck.
    e.hostWrite(0x12, 2);
    CHECK_EQ(e.hostRead(0x12) & 2, 2);
    e.setHaltPin(false);
    CHECK_EQ(e.hostRead(0x12) & 2, 0);
}

TEST(refresh_disable_freezes_dbase) {
    HostBus bus;
    Esp e(bus);
    e.setGpr(244, 0xFF);
    e.setGpr(245, 0x00FF00);
    e.setGpr(248, 0x001000);
    haltAtEnd(e);
    e.hostWrite(0x12, 1);
    CHECK_EQ(e.hostRead(0x12) & 1, 1);
    const uint32_t d0 = e.agen().dbase;
    CHECK_EQ(d0, 0x000F00u);  // the END that halted still decremented DBASE (0x1000 - unit)
    for (int i = 0; i < 10; i++) e.idleStep();
    CHECK_EQ(e.agen().dbase, d0);
    e.hostWrite(0x12, 0);
    for (int i = 1; i <= 3; i++) {
        e.idleStep();
        CHECK_EQ(e.agen().dbase, d0 - 0x100u * i);
    }
    CHECK(bus.writes.empty());  // refresh decrements DBASE only; no data cycles
    // Going below zero reloads DLENGTH.
    e.setGpr(248, 0x000000);
    e.idleStep();
    CHECK_EQ(e.agen().dbase, 0xFF00u);
}

TEST(serial_control_register) {
    HostBus bus;
    Esp e(bus);
    CHECK_EQ(e.hostRead(0x18) & 3, 3);
    e.hostWrite(0x18, 0x60);
    CHECK_EQ(e.hostRead(0x18), 0x63);
    e.hostWrite(0x18, 0x63);
    CHECK_EQ(e.hostRead(0x18), 0x63);
    e.hostWrite(0x18, 0xFC);
    CHECK_EQ(e.hostRead(0x18), 0xFF);
    e.hostWrite(0x18, 0x00);
    CHECK_EQ(e.hostRead(0x18), 0x03);
}

TEST(host_register_map_details) {
    HostBus bus;
    Esp e(bus);
    // DIL/DOL are 16 bits left-justified: the low bytes read 0 (E10: DOL low byte too).
    e.hostWrite(0x0C, 0x12);
    e.hostWrite(0x0D, 0x34);
    e.hostWrite(0x0E, 0x56);
    CHECK_EQ(e.hostRead(0x0C), 0x12);
    CHECK_EQ(e.hostRead(0x0D), 0x34);
    CHECK_EQ(e.hostRead(0x0E), 0x00);
    CHECK_EQ(e.hostRead(0x0B), 0x00);
    // DADR LO holds address bits 7..4 only.
    e.hostWrite(0x11, 0xFF);
    CHECK_EQ(e.hostRead(0x11), 0xF0);
    // RAM access control reads back; PC and test registers are harmless.
    e.hostWrite(0x14, 0x7F);
    CHECK_EQ(e.hostRead(0x14), 0x7F);
    CHECK_EQ(e.hostRead(0x16), 0x00);
    // Host control bit 2 is read only; bits 7..3 read 0.
    e.hostWrite(0x12, 0xFC);
    CHECK_EQ(e.hostRead(0x12), 0x00);
    // Unlisted addresses read 0 and ignore writes.
    e.hostWrite(0x13, 0xFF);
    e.hostWrite(0x40, 0xFF);
    CHECK_EQ(e.hostRead(0x13), 0x00);
    CHECK_EQ(e.hostRead(0x40), 0x00);
    // Select registers read back what was last written to each.
    e.hostWrite(0xA0, 0x7F);
    e.hostWrite(0xC0, 0xC0);
    CHECK_EQ(e.hostRead(0xA0), 0x7F);
    CHECK_EQ(e.hostRead(0xC0), 0xC0);
    CHECK_EQ(e.hostRead(0xE0), 0x00);
}

TEST(host_transfers_while_halted_one_per_idle_step) {
    HostBus bus;
    Esp e(bus);
    haltAtEnd(e);
    CHECK(!busy(e));
    hw3(e, 0x00, 0x00AAAA);
    e.hostWrite(0xA0, 0x20);
    CHECK(busy(e));
    CHECK_EQ(e.gpr(0x20), 0u);  // serviced by the next instruction period, not instantly
    e.idleStep();
    CHECK(!busy(e));
    CHECK_EQ(e.gpr(0x20), 0xAAAAu);
    // No pending request: idleStep leaves everything alone.
    e.idleStep();
    CHECK_EQ(e.gpr(0x20), 0xAAAAu);
    // Read request while halted.
    e.setInstr(3, 0x0123456789F8ull);
    e.hostWrite(0x80, 3);
    e.idleStep();
    CHECK_EQ(e.hostRead(0x03), 0x01);
    CHECK_EQ(e.hostRead(0x08), 0xF8);
    // idleStep on a running ESP does nothing, not even the transfer.
    e.setHaltPin(false);
    hw3(e, 0x00, 0x00BBBB);
    e.hostWrite(0xA0, 0x21);
    e.idleStep();
    CHECK(busy(e));
    CHECK_EQ(e.gpr(0x21), 0u);
}

// The KS-32 firmware boot and program-load sequences (data/esp_init_sequence.json).
TEST(host_firmware_boot_sequence) {
    HostBus bus;
    Esp e(bus);
    e.setHaltPin(true);  // DUART OP6 reset = ESPHALT asserted
    e.hostWrite(0x1F, 0x02);
    e.hostWrite(0x1F, 0xFF);
    CHECK(e.halted());
    xfer(e, 0xE0, 0x9F);
    auto gprw = [&](uint8_t addr, uint32_t v) {
        hw3(e, 0x00, v);
        xfer(e, 0xA0, addr);
    };
    gprw(0xF4, 0x0000FF);  // MEMSIZ
    // DLENGTH is written with read-back verification and retries (routine 0xC1CC80).
    int attempts = 0;
    for (bool ok = false; !ok && attempts < 10;) {
        ++attempts;
        gprw(0xF5, 0xFFFFFF);
        xfer(e, 0x80, 0xF5);
        ok = hr3(e, 0x00) == 0xFFFFFFu;
    }
    CHECK_EQ(attempts, 1);  // the firmware must not need a retry (error 0x21 after 10)
    CHECK_EQ(e.agen().memsiz, 0xFFu);
    xfer(e, 0x80, 0xF5);
    CHECK_EQ(hr3(e, 0x00), 0xFFFFFFu);  // R13: read back exactly as written
    CHECK_EQ(e.agen().dlength20(), 0xFFFFF0u);
    gprw(0xF8, 0x000F00);  // DBASE
    e.hostWrite(0x12, 0x03);
    e.hostWrite(0x12, 0x02);
    int steps = 0;
    while ((e.hostRead(0x12) & 2) && steps < 200000) {
        e.idleStep();
        ++steps;
    }
    CHECK_EQ(steps, 65536);
    CHECK_EQ(bus.writes.size(), 65536u);
    e.hostWrite(0x18, 0x63);
    CHECK_EQ(e.hostRead(0x18), 0x63);
    // Every instruction = END, every GPR = 0xFFFFFF.
    hw3(e, 0x00, 0xFFFFFF);
    hwInstr(e, 0xFFFFFFFFFFFFull);
    for (int i = 0; i < 0xA0; i++) xfer(e, 0xE0, uint8_t(i));
    for (int i = 0xA0; i < 0xC0; i++) xfer(e, 0xA0, uint8_t(i));
    CHECK_EQ(e.instr(0x9F), 0xFFFFFFFFFFFFull);
    CHECK_EQ(e.instr(0), 0xFFFFFFFFFFFFull);
    CHECK_EQ(e.gpr(0xBF), 0xFFFFFFu);
    CHECK_EQ(e.gpr(0x00), 0xFFFFFFu);
    gprw(0xF9, 0xFFFFFF);  // SIGREG
    CHECK_EQ(e.gpr(249), 0x400000u);
    for (int a = 0xEA; a <= 0xF1; a++) gprw(uint8_t(a), 0);
    // Read-back verification through $80, then parking the select registers at $C0.
    xfer(e, 0x80, 0x10);
    CHECK_EQ(hr3(e, 0x00), 0xFFFFFFu);
    xfer(e, 0x80, 0xC0);
    // Release ESPHALT: the all-END program runs one step per sample.
    e.setHaltPin(false);
    CHECK_EQ(e.runSample(), 1);
    CHECK(!e.halted());
}

TEST(host_firmware_program_load_with_dram_table) {
    HostBus bus;
    Esp e(bus);
    e.setGpr(244, 0xFF);
    e.setInstr(0, kEndWord);
    e.setHaltPin(true);
    e.runSample();
    e.hostWrite(0x1F, 0x02);
    CHECK(e.halted());
    // Table word at DRAM address 0x1FBF: $14 = 0x7F (write RAM), DOL, DADR $0F/$10 = address.
    e.hostWrite(0x14, 0x7F);
    e.hostWrite(0x0C, 0x80);
    e.hostWrite(0x0D, 0x01);
    e.hostWrite(0x11, 0x00);
    e.hostWrite(0x10, 0xBF);
    e.hostWrite(0x0F, 0x1F);
    CHECK_EQ(bus.writes.size(), 1u);
    CHECK_EQ(bus.writes[0].addr, 0x1FBFu);
    CHECK_EQ(bus.writes[0].v, 0x8001);
    e.hostWrite(0x14, 0xFF);  // read back
    e.hostWrite(0x0F, 0x1F);
    CHECK_EQ((e.hostRead(0x09) << 8) | e.hostRead(0x0A), 0x8001);
}
