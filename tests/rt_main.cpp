// ens_rt_tests: the real-time checks. Global operator new and delete are replaced here, so this
// is its own executable; they count the allocations made between allocStart() and allocStop().
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "check.h"
#include "ks32_firmware.h"

namespace {

bool counting = false;  // single-threaded: the tests run one at a time on the main thread
size_t allocations = 0;

void allocStart() {
    allocations = 0;
    counting = true;
}

size_t allocStop() {
    counting = false;
    return allocations;
}

}  // namespace

void* operator new(std::size_t n) {
    if (counting) ++allocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace ens::ks32;

// The replacement is in use, so a count of 0 below means something.
TEST(rt_allocation_counter_counts) {
    allocStart();
    int* volatile p = new int(1);
    delete p;
    CHECK_EQ(allocStop(), 1u);
}

// Plan 7 real time: once booted, a second of runFrame with the events a host sends every 50 ms
// (MIDI in, a button tap, the mod wheel, MIDI out drained, the panel copied) allocates nothing.
TEST(rt_machine_runframe_does_not_allocate) {
    auto m = loadKs32();
    if (!m) {
        std::printf("  skip: ROMs not found\n");
        return;
    }
    runMs(*m, kBootMs);
    const uint8_t note[] = {0x90, 0x3C, 0x64, 0x80, 0x3C, 0x40};  // middle C on and off
    PanelState panel;
    uint16_t mod = 0;
    int16_t l, r;
    allocStart();
    for (uint32_t f = 0; f < kWavRate; ++f) {
        if (f % (kWavRate / 20) == 0) {
            for (const uint8_t b : note) m->midiIn(b);
            m->button(Button::UpYes, true);
            m->button(Button::UpYes, false);
            m->setAnalog(Analog::ModWheel, mod);
            mod = static_cast<uint16_t>((mod + 0x40) & 0x3FF);
            for (uint8_t b = 0; m->popMidiOut(b);) {
            }
            panel = m->panel();
        }
        m->runFrame(l, r);
    }
    const size_t n = allocStop();
    std::printf("  allocations: %zu\n", n);
    CHECK_EQ(n, 0u);
}

// Usage: ens_rt_tests [substring]  -- runs only tests whose name contains the substring.
int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int passed = 0, failed = 0;
    for (const auto& c : ens_test::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        ens_test::failures() = 0;
        std::printf("[ RUN  ] %s\n", c.name);
        c.fn();
        if (ens_test::failures() == 0) {
            ++passed;
            std::printf("[  OK  ] %s\n", c.name);
        } else {
            ++failed;
            std::printf("[ FAIL ] %s\n", c.name);
        }
    }
    std::printf("%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
