// .m68t reader and the per-test runner for Tom Harte's 68000 tests (see harte_format.h).
#include "m68k/harte_format.h"

#include <algorithm>
#include <array>

namespace ens::m68k {

namespace {

// ---- reader ------------------------------------------------------------------------------

struct Reader {
    const std::vector<uint8_t>& b;
    size_t pos = 0;
    bool ok = true;

    size_t left() const { return b.size() - pos; }
    bool need(size_t n) {
        if (left() < n) ok = false;
        return ok;
    }
    uint8_t u8() {
        if (!need(1)) return 0;
        return b[pos++];
    }
    uint16_t u16() {
        if (!need(2)) return 0;
        const uint16_t v = static_cast<uint16_t>(b[pos] | (b[pos + 1] << 8));
        pos += 2;
        return v;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        const uint32_t v = static_cast<uint32_t>(b[pos]) | (static_cast<uint32_t>(b[pos + 1]) << 8) |
                           (static_cast<uint32_t>(b[pos + 2]) << 16) |
                           (static_cast<uint32_t>(b[pos + 3]) << 24);
        pos += 4;
        return v;
    }
};

void readState(Reader& r, HarteState& s) {
    s = HarteState{};
    for (uint32_t& v : s.regs.d) v = r.u32();
    for (uint32_t& v : s.regs.a) v = r.u32();
    s.regs.usp = r.u32();
    s.regs.ssp = r.u32();
    s.regs.pc = r.u32();
    s.regs.sr = r.u16();
    s.regs.prefetch[0] = r.u16();
    s.regs.prefetch[1] = r.u16();
    const uint32_t n = r.u32();
    if (!r.ok) return;
    if (static_cast<uint64_t>(n) * 5 > r.left()) {  // each entry is 5 bytes: no huge allocation
        r.ok = false;
        return;
    }
    s.ram.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t addr = r.u32();
        const uint8_t v = r.u8();
        s.ram.emplace_back(addr, v);
    }
}

// ---- diff text ---------------------------------------------------------------------------

std::string hex(uint32_t v) {
    static const char digits[] = "0123456789ABCDEF";
    std::string s;
    do {
        s.insert(s.begin(), digits[v & 15]);
        v >>= 4;
    } while (v != 0);
    return "0x" + s;
}

struct Diffs {
    std::string* text;
    bool any = false;

    void add(const std::string& field, const std::string& expected, const std::string& got) {
        any = true;
        if (!text) return;
        if (!text->empty()) *text += '\n';
        *text += field + ": expected " + expected + ", got " + got;
    }
    void hexField(const std::string& field, uint32_t expected, uint32_t got) {
        if (expected != got) add(field, hex(expected), hex(got));
    }
};

using RamList = std::vector<std::pair<uint32_t, uint8_t>>;

uint8_t lookup(const RamList& ram, uint32_t addr, bool& found) {
    for (const auto& e : ram) {
        if (e.first == addr) {
            found = true;
            return e.second;
        }
    }
    found = false;
    return 0;
}

// The expected byte at addr after the test: its final RAM, else the initial RAM, else 0.
uint8_t expectedAt(const HarteTest& t, uint32_t addr) {
    bool found = false;
    uint8_t v = lookup(t.final.ram, addr, found);
    if (found) return v;
    return lookup(t.initial.ram, addr, found);
}

// DECISION M1: the manual-deviation table. Each entry is one suite test, by exact name, where
// a decision follows the PRM/UM against the suite; nothing else may be listed.
struct ManualDeviationEntry {
    const char* name;
    const char* decision;
};
constexpr ManualDeviationEntry kManualDeviations[] = {
    // Divide by zero stacks the next instruction's address (UM 6.3.5); the suite stacks the
    // instruction's own address.
    {"80ef [DIVU (d16, A7), D0] 5745", "M15"},
    // The suite's recorded initial D2 cannot produce its final D2 and C (no ASL.b #2 can
    // change bits 31..8); the model's result follows from the recorded initial state.
    {"e502 [ASL.b Q, D2] 1583", "M19"},
    {"e502 [ASL.b Q, D2] 1761", "M19"},
};

HarteResult classifyCore(const HarteTest& t, const Registers& got, const RamList& gotRam,
                         uint32_t gotCycles, uint32_t instrAddr, int vectorTaken, std::string* diffText);

HarteResult classify(const HarteTest& t, const Registers& got, const RamList& gotRam,
                     uint32_t gotCycles, uint32_t instrAddr, int vectorTaken, std::string* diffText) {
    const HarteResult r = classifyCore(t, got, gotRam, gotCycles, instrAddr, vectorTaken, diffText);
    if (r == HarteResult::Fail && manualDeviation(t.name) != nullptr) return HarteResult::ManualDeviation;
    return r;
}

HarteResult classifyCore(const HarteTest& t, const Registers& got, const RamList& gotRam,
                         uint32_t gotCycles, uint32_t instrAddr, int vectorTaken, std::string* diffText) {
    Diffs d{diffText};
    const Registers& exp = t.final.regs;
    for (int i = 0; i < 8; ++i) d.hexField("d" + std::to_string(i), exp.d[i], got.d[i]);
    for (int i = 0; i < 7; ++i) d.hexField("a" + std::to_string(i), exp.a[i], got.a[i]);
    d.hexField("usp", exp.usp, got.usp);
    d.hexField("ssp", exp.ssp, got.ssp);
    d.hexField("pc", exp.pc, got.pc);
    d.hexField("sr", exp.sr, got.sr);
    if (t.cycles != gotCycles) d.add("cycles", std::to_string(t.cycles), std::to_string(gotCycles));
    const bool coreEqual = !d.any;  // d, a, usp, ssp, pc, sr and cycles
    d.hexField("prefetch0", exp.prefetch[0], got.prefetch[0]);
    d.hexField("prefetch1", exp.prefetch[1], got.prefetch[1]);
    const bool prefetchEqual = exp.prefetch[0] == got.prefetch[0] && exp.prefetch[1] == got.prefetch[1];

    // RAM: every byte the CPU wrote or the test names. An address missing from gotRam reads 0.
    std::vector<uint32_t> addrs;
    for (const auto& e : gotRam) addrs.push_back(e.first);
    for (const auto& e : t.final.ram) addrs.push_back(e.first);
    std::sort(addrs.begin(), addrs.end());
    addrs.erase(std::unique(addrs.begin(), addrs.end()), addrs.end());
    std::vector<uint32_t> ramDiffs;
    for (uint32_t a : addrs) {
        bool found = false;
        const uint8_t g = lookup(gotRam, a, found);
        const uint8_t e = expectedAt(t, a);
        if (e != g) {
            ramDiffs.push_back(a);
            d.add("ram[" + hex(a) + "]", hex(e), hex(g));
        }
    }
    if (!d.any) return HarteResult::Pass;
    if (!coreEqual) return HarteResult::Fail;

    // DECISION M1: d, a, usp, ssp, pc, sr and cycles are always exact (except for a test in
    // the manual-deviation table, see classify). Two classes may absorb the rest. (1) SelfModifyingPrefetch: the test's own final RAM shows a write inside the
    // instruction's prefetch window [instrAddr, instrAddr + 6), and every difference is a RAM byte
    // inside that window or one/both prefetch words (the closing fetch reads the old or the new
    // value depending on the order of write and fetch, which the suite records from the hardware).
    // (2) AddressErrorFrame: vector 3 and prefetch words equal, every RAM difference in the
    // first 8 bytes (status word, access address, IR) of the 14-byte frame at the final SSP.
    auto inWindow = [instrAddr](uint32_t a) { return a >= instrAddr && a - instrAddr < 6; };
    bool wroteInWindow = false;
    for (const auto& e : t.final.ram) {
        bool found = false;
        if (inWindow(e.first) && e.second != lookup(t.initial.ram, e.first, found)) wroteInWindow = true;
    }
    bool ramInWindow = true, ramInFrame = true;
    for (uint32_t a : ramDiffs) {
        if (!inWindow(a)) ramInWindow = false;
        if (!(a >= got.ssp && a - got.ssp < 8)) ramInFrame = false;
    }
    if (wroteInWindow && ramInWindow) return HarteResult::SelfModifyingPrefetch;
    if (vectorTaken == 3 && prefetchEqual && ramInFrame) return HarteResult::AddressErrorFrame;
    return HarteResult::Fail;
}

// ---- sparse bus --------------------------------------------------------------------------

// 16 MB of zeroes allocated once; only the bytes a test touches are cleared afterwards.
struct SparseBus : Bus {
    static constexpr uint32_t kMask = 0xFFFFFF;
    std::vector<uint8_t> mem = std::vector<uint8_t>(size_t(1) << 24, 0);
    std::vector<uint32_t> touched;  // every address stored since clear()
    std::vector<uint32_t> written;  // the subset written by the CPU

    void clear() {
        for (uint32_t a : touched) mem[a] = 0;
        touched.clear();
        written.clear();
    }
    void load(const RamList& ram) {
        for (const auto& e : ram) {
            const uint32_t a = e.first & kMask;
            mem[a] = e.second;
            touched.push_back(a);
        }
    }
    void put(uint32_t addr, uint8_t v) {
        const uint32_t a = addr & kMask;
        mem[a] = v;
        touched.push_back(a);
        written.push_back(a);
    }

    uint8_t read8(uint32_t addr) override { return mem[addr & kMask]; }
    uint16_t read16(uint32_t addr) override {
        return static_cast<uint16_t>((mem[addr & kMask] << 8) | mem[(addr + 1) & kMask]);
    }
    void write8(uint32_t addr, uint8_t v) override { put(addr, v); }
    void write16(uint32_t addr, uint16_t v) override {
        put(addr, static_cast<uint8_t>(v >> 8));
        put(addr + 1, static_cast<uint8_t>(v));
    }
};

}  // namespace

// ---- parser ------------------------------------------------------------------------------

bool parseHarte(const std::vector<uint8_t>& bytes, std::vector<HarteTest>& out, std::string& err) {
    out.clear();
    Reader r{bytes};
    if (bytes.size() < 12 || bytes[0] != 'M' || bytes[1] != '6' || bytes[2] != '8' || bytes[3] != 'T') {
        err = "not a .m68t file (bad header)";
        return false;
    }
    r.pos = 4;
    const uint32_t version = r.u32();
    const uint32_t count = r.u32();
    if (version != 1) {
        err = "unsupported .m68t version " + std::to_string(version);
        return false;
    }
    if (static_cast<uint64_t>(count) > r.left()) {  // every test is far larger than a byte
        err = "truncated file (test count exceeds the data)";
        return false;
    }
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        HarteTest t{};
        const uint16_t nameLen = r.u16();
        if (!r.need(nameLen)) break;
        t.name.assign(reinterpret_cast<const char*>(bytes.data() + r.pos), nameLen);
        r.pos += nameLen;
        readState(r, t.initial);
        readState(r, t.final);
        t.cycles = r.u32();
        if (!r.ok) break;
        out.push_back(std::move(t));
    }
    if (!r.ok) {
        err = "truncated file (test " + std::to_string(out.size()) + " of " + std::to_string(count) + ")";
        out.clear();
        return false;
    }
    if (r.left() != 0) {
        err = "trailing bytes after the last test";
        out.clear();
        return false;
    }
    return true;
}

// ---- runner ------------------------------------------------------------------------------

struct HarteRunner::Impl {
    SparseBus bus;
    Cpu cpu{bus};
};

HarteRunner::HarteRunner() : impl_(new Impl) {}
HarteRunner::~HarteRunner() = default;

HarteResult HarteRunner::run(const HarteTest& t, std::string& diff) {
    SparseBus& bus = impl_->bus;
    Cpu& cpu = impl_->cpu;
    diff.clear();
    bus.clear();
    cpu.reset();  // clears halted/stopped left by an earlier test (memory is zero here)
    bus.load(t.initial.ram);
    cpu.setRegs(t.initial.regs);
    std::array<uint32_t, 256> before;
    for (int v = 0; v < 256; ++v) before[static_cast<size_t>(v)] = cpu.exceptionCount(v);
    const int cycles = cpu.step();
    int vector = -1;
    for (int v = 0; v < 256; ++v) {
        if (cpu.exceptionCount(v) != before[static_cast<size_t>(v)]) {
            vector = v;
            break;
        }
    }
    RamList gotRam;
    std::vector<uint32_t> addrs = bus.written;
    for (const auto& e : t.final.ram) addrs.push_back(e.first & SparseBus::kMask);
    std::sort(addrs.begin(), addrs.end());
    addrs.erase(std::unique(addrs.begin(), addrs.end()), addrs.end());
    gotRam.reserve(addrs.size());
    for (uint32_t a : addrs) gotRam.emplace_back(a, bus.mem[a]);
    return classify(t, cpu.regs(), gotRam, static_cast<uint32_t>(cycles), cpu.instructionAddress(),
                    vector, &diff);
}

const char* manualDeviation(const std::string& testName) {
    for (const ManualDeviationEntry& e : kManualDeviations) {
        if (testName == e.name) return e.decision;
    }
    return nullptr;
}

HarteResult runHarteTest(const HarteTest& t, std::string& diff) {
    HarteRunner runner;
    return runner.run(t, diff);
}

HarteResult classifyHarteFailure(const HarteTest& t, const Registers& got, const RamList& gotRam,
                                 uint32_t gotCycles, uint32_t instrAddr, int vectorTaken) {
    return classify(t, got, gotRam, gotCycles, instrAddr, vectorTaken, nullptr);
}

}  // namespace ens::m68k
