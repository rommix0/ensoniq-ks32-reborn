#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "check.h"
#include "m68k/harte_format.h"

using namespace ens::m68k;

namespace {

using RamList = std::vector<std::pair<uint32_t, uint8_t>>;

std::pair<uint32_t, uint8_t> B(uint32_t addr, unsigned v) {
    return {addr, static_cast<uint8_t>(v)};
}

bool loadSample(std::vector<uint8_t>& bytes) {
    std::FILE* f = std::fopen(ENS_TEST_DATA_DIR "/m68k_harte_sample.m68t", "rb");
    if (!f) return false;
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);
    return true;
}

// A NOP-shaped test whose final state we can perturb.
HarteTest baseTest() {
    HarteTest t{};
    t.name = "synthetic";
    t.initial.regs.sr = 0x2700;
    t.initial.regs.ssp = 0x1000;
    t.initial.regs.pc = 0x400;
    t.initial.regs.prefetch[0] = 0x4E71;
    t.initial.regs.prefetch[1] = 0x4E71;
    t.final = t.initial;
    t.final.regs.pc = 0x402;
    t.cycles = 4;
    return t;
}

}  // namespace

TEST(harte_sample_parses_and_runs) {
    std::vector<uint8_t> bytes;
    CHECK(loadSample(bytes));
    std::vector<HarteTest> tests;
    std::string err;
    CHECK(parseHarte(bytes, tests, err));
    CHECK_EQ(tests.size(), 3u);
    if (tests.size() != 3) return;
    std::string diff;
    CHECK(runHarteTest(tests[0], diff) == HarteResult::Pass);
    CHECK(runHarteTest(tests[2], diff) == HarteResult::Pass);
    diff.clear();
    CHECK(runHarteTest(tests[1], diff) == HarteResult::Fail);
    CHECK(diff.find("cycles") != std::string::npos);
    CHECK(diff.find("expected 6") != std::string::npos);
    CHECK(diff.find("got 4") != std::string::npos);
    // The runner reused across tests gives the same answers.
    HarteRunner runner;
    CHECK(runner.run(tests[2], diff) == HarteResult::Pass);
    CHECK(runner.run(tests[0], diff) == HarteResult::Pass);
}

TEST(harte_reader_rejects_truncated_file) {
    std::vector<uint8_t> bytes;
    CHECK(loadSample(bytes));
    for (size_t n = 0; n < bytes.size(); ++n) {
        std::vector<uint8_t> prefix(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n));
        std::vector<HarteTest> tests;
        std::string err;
        if (parseHarte(prefix, tests, err) || err.empty()) {
            CHECK(false);
            std::printf("  prefix of %zu bytes was accepted\n", n);
            break;
        }
    }
    std::vector<uint8_t> extra = bytes;
    extra.push_back(0);  // trailing garbage is corrupt too
    std::vector<HarteTest> tests;
    std::string err;
    CHECK(!parseHarte(extra, tests, err));
}

TEST(harte_failure_classes) {
    const RamList none;
    // Self-modifying prefetch: only the byte at instrAddr + 4 differs, and the test wrote it.
    {
        HarteTest t = baseTest();
        t.final.ram = {B(0x404, 0xAB)};
        const RamList got = {B(0x404, 0x00)};
        CHECK(classifyHarteFailure(t, t.final.regs, got, 4, 0x400, -1) ==
              HarteResult::SelfModifyingPrefetch);
        // The same difference with no write in the test's final RAM is a plain failure.
        HarteTest u = baseTest();
        u.initial.ram = {B(0x404, 0xAB)};
        u.final.ram = {B(0x404, 0xAB)};
        CHECK(classifyHarteFailure(u, u.final.regs, got, 4, 0x400, -1) == HarteResult::Fail);
        // A difference elsewhere is a failure too.
        const RamList got2 = {B(0x404, 0x00), B(0x500, 1)};
        t.final.ram = {B(0x404, 0xAB), B(0x500, 0)};
        CHECK(classifyHarteFailure(t, t.final.regs, got2, 4, 0x400, -1) == HarteResult::Fail);
    }
    // Self-modifying prefetch window [instrAddr, instrAddr + 6): the test writes inside it and the
    // only differences are RAM bytes in the window or prefetch words.
    {
        HarteTest t = baseTest();
        t.final.ram = {B(0x402, 0xAB)};  // (a) a write at instrAddr + 2
        const RamList same = {B(0x402, 0xAB)};
        Registers r = t.final.regs;
        r.prefetch[1] ^= 0x0100;
        CHECK(classifyHarteFailure(t, r, same, 4, 0x400, -1) == HarteResult::SelfModifyingPrefetch);
        r.prefetch[0] ^= 1;  // both prefetch words
        CHECK(classifyHarteFailure(t, r, same, 4, 0x400, -1) == HarteResult::SelfModifyingPrefetch);
        // (b) the same prefetch mismatch with no write in the window
        HarteTest u = baseTest();
        r = u.final.regs;
        r.prefetch[1] ^= 0x0100;
        CHECK(classifyHarteFailure(u, r, none, 4, 0x400, -1) == HarteResult::Fail);
        // a write only outside the window does not count either
        u.final.ram = {B(0x406, 0xAB)};
        const RamList outside = {B(0x406, 0xAB)};
        CHECK(classifyHarteFailure(u, r, outside, 4, 0x400, -1) == HarteResult::Fail);
        // (c) a register difference plus an in-window RAM difference
        r = t.final.regs;
        r.d[0] ^= 1;
        CHECK(classifyHarteFailure(t, r, none, 4, 0x400, -1) == HarteResult::Fail);
        r = t.final.regs;
        r.pc += 2;
        r.prefetch[0] ^= 1;
        CHECK(classifyHarteFailure(t, r, same, 4, 0x400, -1) == HarteResult::Fail);
        // the cycle count must still match
        CHECK(classifyHarteFailure(t, t.final.regs, same, 6, 0x400, -1) == HarteResult::Fail);
        // (d) a RAM difference at instrAddr + 5, (e) at instrAddr + 6
        HarteTest w = baseTest();
        w.final.ram = {B(0x405, 0xAB)};
        CHECK(classifyHarteFailure(w, w.final.regs, none, 4, 0x400, -1) ==
              HarteResult::SelfModifyingPrefetch);
        w.final.ram = {B(0x406, 0xAB)};
        CHECK(classifyHarteFailure(w, w.final.regs, none, 4, 0x400, -1) == HarteResult::Fail);
        // a difference in the window together with one outside it is a failure
        w.final.ram = {B(0x405, 0xAB), B(0x500, 0xCD)};
        CHECK(classifyHarteFailure(w, w.final.regs, none, 4, 0x400, -1) == HarteResult::Fail);
    }
    // Address error: vector 3, registers equal, the only RAM difference is in bits 3..0 of the
    // status word's low byte at SSP + 1. Frame at SSP 0xFF2: status 0xFF2/3, access address
    // 0xFF4..7, IR 0xFF8/9, SR 0xFFA/B, PC 0xFFC..F.
    {
        HarteTest t = baseTest();
        t.final.regs.ssp = 0xFF2;
        t.final.ram = {B(0xFF2, 0x12), B(0xFF3, 0x1E), B(0xFF4, 0x00), B(0xFF5, 0x34), B(0xFF8, 0x56)};
        const RamList exact = t.final.ram;
        // bit 3 (I/N) differs
        RamList got = exact;
        got[1] = B(0xFF3, 0x16);
        CHECK(classifyHarteFailure(t, t.final.regs, got, 4, 0x400, 3) ==
              HarteResult::AddressErrorFrame);
        CHECK(classifyHarteFailure(t, t.final.regs, got, 4, 0x400, 4) == HarteResult::Fail);
        CHECK(classifyHarteFailure(t, t.final.regs, got, 4, 0x400, -1) == HarteResult::Fail);
        // the function code (bits 2..0) too
        got[1] = B(0xFF3, 0x19);
        CHECK(classifyHarteFailure(t, t.final.regs, got, 4, 0x400, 3) ==
              HarteResult::AddressErrorFrame);
        // bit 4 (R/W) is exact
        got[1] = B(0xFF3, 0x0E);
        CHECK(classifyHarteFailure(t, t.final.regs, got, 4, 0x400, 3) == HarteResult::Fail);
        // the high status byte (IR bits 15..8) is exact
        got = exact;
        got[0] = B(0xFF2, 0x13);
        CHECK(classifyHarteFailure(t, t.final.regs, got, 4, 0x400, 3) == HarteResult::Fail);
        // the access address is exact
        got = exact;
        got[3] = B(0xFF5, 0x35);
        CHECK(classifyHarteFailure(t, t.final.regs, got, 4, 0x400, 3) == HarteResult::Fail);
        got = exact;
        got[2] = B(0xFF4, 0x01);
        CHECK(classifyHarteFailure(t, t.final.regs, got, 4, 0x400, 3) == HarteResult::Fail);
        // the IR word is exact
        got = exact;
        got[4] = B(0xFF8, 0x57);
        CHECK(classifyHarteFailure(t, t.final.regs, got, 4, 0x400, 3) == HarteResult::Fail);
        // the SR/PC part past the first 8 bytes is exact
        t.final.ram.push_back(B(0xFFA, 0x27));
        got = t.final.ram;
        got.back() = B(0xFFA, 0x26);
        CHECK(classifyHarteFailure(t, t.final.regs, got, 4, 0x400, 3) == HarteResult::Fail);
        // a status difference together with another frame difference is a failure
        got = t.final.ram;
        got[1] = B(0xFF3, 0x16);
        got[4] = B(0xFF8, 0x57);
        CHECK(classifyHarteFailure(t, t.final.regs, got, 4, 0x400, 3) == HarteResult::Fail);
        // the prefetch words stay exact
        got = t.final.ram;
        got[1] = B(0xFF3, 0x16);
        Registers r = t.final.regs;
        r.prefetch[0] ^= 1;
        CHECK(classifyHarteFailure(t, r, got, 4, 0x400, 3) == HarteResult::Fail);
    }
    // Any register difference is a failure, whatever the RAM or vector.
    {
        HarteTest t = baseTest();
        Registers r = t.final.regs;
        r.d[3] ^= 1;
        CHECK(classifyHarteFailure(t, r, none, 4, 0x400, -1) == HarteResult::Fail);
        r = t.final.regs;
        r.sr ^= 1;
        CHECK(classifyHarteFailure(t, r, none, 4, 0x400, 3) == HarteResult::Fail);
        r = t.final.regs;
        r.prefetch[1] ^= 1;
        CHECK(classifyHarteFailure(t, r, none, 4, 0x400, -1) == HarteResult::Fail);
        CHECK(classifyHarteFailure(t, t.final.regs, none, 6, 0x400, -1) == HarteResult::Fail);
        CHECK(classifyHarteFailure(t, t.final.regs, none, 4, 0x400, -1) == HarteResult::Pass);
    }
}

TEST(harte_manual_deviation_by_name) {
    // DECISION M1: a failing test listed by name with the decision that overrides the suite is
    // a ManualDeviation only when every difference lies in the fields its entry covers; the
    // same failure under any other name is a Fail.
    // M15: DIVU by zero; only RAM 0x7FF (the stacked PC's low byte) may differ.
    {
        HarteTest t = baseTest();
        t.name = "80ef [DIVU (d16, A7), D0] 5745";
        t.final.ram = {B(0x7FF, 0)};
        const RamList pcByte = {B(0x7FF, 4)};
        CHECK(classifyHarteFailure(t, t.final.regs, pcByte, 4, 0x400, -1) ==
              HarteResult::ManualDeviation);
        CHECK(classifyHarteFailure(t, t.final.regs, t.final.ram, 4, 0x400, -1) == HarteResult::Pass);
        HarteTest u = t;
        u.name = "80ef [DIVU (d16, A7), D0] 5746";
        CHECK(classifyHarteFailure(u, u.final.regs, pcByte, 4, 0x400, -1) == HarteResult::Fail);
        // any other difference makes it a failure: another RAM byte, a register, the cycles
        const RamList more = {B(0x7FF, 4), B(0x7FE, 1)};
        CHECK(classifyHarteFailure(t, t.final.regs, more, 4, 0x400, -1) == HarteResult::Fail);
        Registers r = t.final.regs;
        r.d[0] = 1;
        CHECK(classifyHarteFailure(t, r, pcByte, 4, 0x400, -1) == HarteResult::Fail);
        r = t.final.regs;
        r.sr ^= 1;
        CHECK(classifyHarteFailure(t, r, pcByte, 4, 0x400, -1) == HarteResult::Fail);
        r = t.final.regs;
        r.prefetch[1] ^= 1;
        CHECK(classifyHarteFailure(t, r, pcByte, 4, 0x400, -1) == HarteResult::Fail);
        CHECK(classifyHarteFailure(t, t.final.regs, pcByte, 6, 0x400, -1) == HarteResult::Fail);
    }
    // M19: ASL.b; 1583 may differ in D2 and the condition codes, 1761 in D2 only.
    {
        const RamList none;
        HarteTest t = baseTest();
        t.name = "e502 [ASL.b Q, D2] 1583";
        Registers r = t.final.regs;
        r.d[2] = 0x1234;
        CHECK(classifyHarteFailure(t, r, none, 4, 0x400, -1) == HarteResult::ManualDeviation);
        r.sr ^= 0x13;
        CHECK(classifyHarteFailure(t, r, none, 4, 0x400, -1) == HarteResult::ManualDeviation);
        r.sr ^= 0x0100;  // a mask bit is not a condition code
        CHECK(classifyHarteFailure(t, r, none, 4, 0x400, -1) == HarteResult::Fail);
        r = t.final.regs;
        r.d[2] = 1;
        r.d[3] = 1;
        CHECK(classifyHarteFailure(t, r, none, 4, 0x400, -1) == HarteResult::Fail);
        r = t.final.regs;
        r.d[2] = 1;
        r.pc += 2;
        CHECK(classifyHarteFailure(t, r, none, 4, 0x400, -1) == HarteResult::Fail);
        r = t.final.regs;
        r.d[2] = 1;
        CHECK(classifyHarteFailure(t, r, {B(0x500, 1)}, 4, 0x400, -1) == HarteResult::Fail);
        HarteTest u = t;
        u.name = "e502 [ASL.b Q, D2] 1761";
        r = u.final.regs;
        r.d[2] = 0x1234;
        CHECK(classifyHarteFailure(u, r, none, 4, 0x400, -1) == HarteResult::ManualDeviation);
        r.sr ^= 1;
        CHECK(classifyHarteFailure(u, r, none, 4, 0x400, -1) == HarteResult::Fail);
        CHECK(classifyHarteFailure(u, u.final.regs, none, 4, 0x400, -1) == HarteResult::Pass);
    }
    CHECK(manualDeviation("80ef [DIVU (d16, A7), D0] 5745") != nullptr);
    if (const char* id = manualDeviation("80ef [DIVU (d16, A7), D0] 5745")) {
        CHECK(std::string(id) == "M15");
    }
    CHECK(manualDeviation("e502 [ASL.b Q, D2] 1583") != nullptr);
    CHECK(manualDeviation("synthetic") == nullptr);
}
