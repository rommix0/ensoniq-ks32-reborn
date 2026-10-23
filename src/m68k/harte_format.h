#pragma once
// Tom Harte's 68000 per-instruction tests, converted to .m68t by tools/m68k_harte/convert_harte.py.
// Pure byte parsing and test running: no file I/O (main.cpp reads the files).
//
// .m68t, little-endian: "M68T", u32 version = 1, u32 count; each test: u16 nameLen, the name,
// State initial, State final, u32 cycles.
// State: u32 d[8], a[7], usp, ssp, pc; u16 sr; u16 prefetch[2]; u32 ramCount;
// ramCount x (u32 addr, u8 value).
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "m68k/m68k.h"

namespace ens::m68k {

struct HarteState {
    Registers regs;
    std::vector<std::pair<uint32_t, uint8_t>> ram;  // [address, byte]
};

struct HarteTest {
    std::string name;
    HarteState initial, final;
    uint32_t cycles;
};

// Parses a whole .m68t file. Never throws; on failure returns false with a message in err.
bool parseHarte(const std::vector<uint8_t>& bytes, std::vector<HarteTest>& out, std::string& err);

// DECISION M1: Pass = registers (incl. prefetch), cycles and RAM all equal the suite's final
// state. Two documented deviations are allowed classes, never a Pass: SelfModifyingPrefetch and
// AddressErrorFrame (only bits 3..0 of the frame's status word may differ, M12). A third,
// ManualDeviation, is a failing test listed by exact name in a fixed table, each entry with
// the decision id that overrides the suite there and the only fields it covers (see
// manualDeviation); a listed test with any other difference is a Fail.
enum class HarteResult { Pass, Fail, SelfModifyingPrefetch, AddressErrorFrame, ManualDeviation };

// The decision id (e.g. "M15") that overrides the suite for the test of this exact name, or
// nullptr. DECISION M1: an entry needs a decision that follows the manuals against the suite;
// entries are never added just to silence a failure. A listed test that passes is stale.
const char* manualDeviation(const std::string& testName);

// Runs one test: loads the initial state into a sparse 24-bit bus, setRegs, one step(), compares.
// `diff` receives one line per difference ("field: expected X, got Y"). Builds a Cpu per call;
// use HarteRunner to run many tests.
HarteResult runHarteTest(const HarteTest& t, std::string& diff);

// The same with one Cpu and bus kept across tests (the Cpu constructor builds its decode table).
class HarteRunner {
public:
    HarteRunner();
    ~HarteRunner();
    HarteRunner(const HarteRunner&) = delete;
    HarteRunner& operator=(const HarteRunner&) = delete;
    HarteResult run(const HarteTest& t, std::string& diff);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Classifies a finished step. `gotRam` holds the final bytes at every address the test names
// or the CPU wrote; `instrAddr` is Cpu::instructionAddress() and `vectorTaken` the exception
// vector the step took (-1 for none). Returns Pass when nothing differs.
HarteResult classifyHarteFailure(const HarteTest& t, const Registers& got,
                                 const std::vector<std::pair<uint32_t, uint8_t>>& gotRam,
                                 uint32_t gotCycles, uint32_t instrAddr, int vectorTaken);

}  // namespace ens::m68k
