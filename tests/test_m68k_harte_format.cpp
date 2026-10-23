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
    // Address error: vector 3, registers equal, the difference lies in the first 8 frame bytes.
    {
        HarteTest t = baseTest();
        t.final.regs.ssp = 0xFF2;
        t.final.ram = {B(0xFF2, 0x11), B(0xFF9, 0x22), B(0xFFA, 0x33)};
        const RamList inFrame = {B(0xFF2, 0), B(0xFF9, 0), B(0xFFA, 0x33)};
        CHECK(classifyHarteFailure(t, t.final.regs, inFrame, 4, 0x400, 3) ==
              HarteResult::AddressErrorFrame);
        CHECK(classifyHarteFailure(t, t.final.regs, inFrame, 4, 0x400, 4) == HarteResult::Fail);
        // A difference beyond the first 8 bytes (the SR/PC part) is a failure.
        const RamList pastFrame = {B(0xFF2, 0x11), B(0xFF9, 0x22), B(0xFFA, 0)};
        CHECK(classifyHarteFailure(t, t.final.regs, pastFrame, 4, 0x400, 3) == HarteResult::Fail);
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
