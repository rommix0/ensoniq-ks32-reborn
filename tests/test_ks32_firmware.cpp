// The KS-32 OS ROM (v3.01) booting on the machine. The firmware is run, never studied: these
// tests look only at device accesses, exception counts and device state after kBootMs.
#include <cstdint>
#include <cstdio>
#include <initializer_list>

#include "check.h"
#include "ks32_firmware.h"

using namespace ens::ks32;

namespace {

// One machine booted for kBootMs, shared by the tests that only read its state (suite runtime);
// nullptr without the ROMs.
Machine* bootedKs32() {
    static const std::unique_ptr<Machine> m = [] {
        auto b = loadKs32();
        if (b) runMs(*b, kBootMs);
        return b;
    }();
    if (!m) std::printf("  skip: ROMs not found\n");
    return m.get();
}

}  // namespace

// The boot clock test leaves OTIS at 32 voices with the K3 clocks (design 10 A).
TEST(ks32_firmware_boot_keeps_32_voices) {
    Machine* m = bootedKs32();
    if (!m) return;
    CHECK_EQ(m->board().otis().activeVoices(), 32);
}

TEST(ks32_firmware_boot_takes_no_fault_vectors) {
    Machine* m = bootedKs32();
    if (!m) return;
    for (int v : {2, 3, 4, 5, 6, 7, 8, 11, 14, 15, 24}) {
        if (m->cpu().exceptionCount(v) != 0) std::printf("  vector %d taken %u times\n", v, m->cpu().exceptionCount(v));
        CHECK_EQ(m->cpu().exceptionCount(v), 0u);
    }
    CHECK(!m->cpu().halted());
    std::printf("  line A (vector 10) taken %u times\n", m->cpu().exceptionCount(10));  // R15: printed only
}

// Every access is in the K1 map, with the device's own width.
TEST(ks32_firmware_boot_is_fully_mapped) {
    auto m = loadKs32();
    if (!m) {
        std::printf("  skip: ROMs not found\n");
        return;
    }
    uint32_t unmapped = 0, otisBytes = 0, evenBytes = 0;
    m->onBusEvent = [&](const BusEvent& e) {
        if (e.dev == Device::Unmapped) ++unmapped;
        if (e.dev == Device::Otis && e.size == 1) ++otisBytes;
        if ((e.dev == Device::Esp || e.dev == Device::Duart) && e.size == 1 && !(e.addr & 1)) ++evenBytes;
    };
    runMs(*m, kBootMs);
    CHECK_EQ(unmapped, 0u);
    CHECK_EQ(m->duart().unsupportedWrites(), 0u);
    CHECK_EQ(otisBytes, 0u);
    CHECK_EQ(evenBytes, 0u);
}

// The firmware releases ESPHALT (K15) and sets the serial control register (findings C).
TEST(ks32_firmware_boot_runs_the_esp) {
    Machine* m = bootedKs32();
    if (!m) return;
    CHECK(!m->board().esp().halted());
    CHECK_EQ(m->board().esp().serialControl(), 0x63);
}

TEST(ks32_firmware_boot_is_deterministic) {
    auto a = loadKs32();
    if (!a) {
        std::printf("  skip: ROMs not found\n");
        return;
    }
    auto b = loadKs32();
    CHECK_EQ(traceHash(*a, kBootMs), traceHash(*b, kBootMs));
}

// The steady display the boot reaches without a button press. With no analog inputs yet (Task 6)
// it is the low-battery warning (service manual PDF p12: "WARNING! Battery low. See manual.",
// then "Press any button to continue"), not the play screen. The boot sends no LED command.
// Task 6 updates this pin when the warning goes away.
TEST(ks32_firmware_boot_shows_play_screen) {
    Machine* m = bootedKs32();
    if (!m) return;
    const PanelState& p = m->panel();
    CHECK_EQ(p.rows.size(), 2u);
    if (p.rows.size() == 2) {
        CHECK(p.rows[0] == "WARNING! Battery");
        CHECK(p.rows[1] == "low. See manual.");
    }
    for (Led l : p.leds) CHECK(l == Led::Off);
    CHECK_EQ(p.blink, 0u);
    CHECK_EQ(p.unknown, 0u);  // every byte the boot sends is in the decoded table (K7)
}
