// m68k_harte: run Tom Harte's 68000 per-instruction tests (converted to .m68t) against ens_m68k.
//   m68k_harte <dir> [--only NAME[,NAME]] [--max-fail N]
// One line per file: "NAME: pass/total (smp a, aef b, dev c)" where pass includes the three
// allowed classes (DECISION M1: self-modifying prefetch, address-error frame, manual deviation
// by name). The first --max-fail (default 3) failing tests of a file are listed with their
// first 3 differences; a manual-deviation test that passes is reported as a stale entry.
// Exit codes: 0 all tests pass (or the data is missing: "skip"), 1 any failure, 2 usage or
// corrupt data.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "m68k/harte_format.h"

namespace fs = std::filesystem;
using namespace ens::m68k;

namespace {

int usage() {
    std::fprintf(stderr, "usage: m68k_harte <dir> [--only NAME[,NAME]] [--max-fail N]\n");
    return 2;
}

bool readFile(const fs::path& p, std::vector<uint8_t>& out) {
    std::FILE* f = std::fopen(p.string().c_str(), "rb");
    if (!f) return false;
    uint8_t buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

void printFirstLines(const std::string& diff, int maxLines) {
    size_t pos = 0;
    for (int i = 0; i < maxLines && pos < diff.size(); ++i) {
        size_t nl = diff.find('\n', pos);
        if (nl == std::string::npos) nl = diff.size();
        std::printf("    %s\n", diff.substr(pos, nl - pos).c_str());
        pos = nl + 1;
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string dir;
    std::vector<std::string> only;
    int maxFail = 3;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--only" && i + 1 < argc) {
            const std::string list = argv[++i];
            size_t pos = 0;
            while (pos <= list.size()) {
                size_t comma = list.find(',', pos);
                if (comma == std::string::npos) comma = list.size();
                if (comma > pos) only.push_back(list.substr(pos, comma - pos));
                pos = comma + 1;
            }
        } else if (a == "--max-fail" && i + 1 < argc) {
            maxFail = std::atoi(argv[++i]);
        } else if (!a.empty() && a[0] != '-' && dir.empty()) {
            dir = a;
        } else {
            return usage();
        }
    }
    if (dir.empty()) return usage();

    std::vector<fs::path> files;
    std::error_code ec;
    if (fs::is_directory(dir, ec)) {
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (e.path().extension() == ".m68t") files.push_back(e.path());
        }
    }
    if (files.empty()) {
        std::printf("skip: test data not found (%s)\n", dir.c_str());
        return 0;
    }
    std::sort(files.begin(), files.end());
    if (!only.empty()) {
        std::vector<fs::path> keep;
        for (const std::string& name : only) {
            bool found = false;
            for (const fs::path& f : files) {
                if (f.stem().string() == name) {
                    keep.push_back(f);
                    found = true;
                }
            }
            if (!found) {
                std::fprintf(stderr, "m68k_harte: no test file named %s in %s\n", name.c_str(), dir.c_str());
                return 2;
            }
        }
        files = keep;
    }

    HarteRunner runner;
    uint64_t totalTests = 0, totalPass = 0, totalSmp = 0, totalAef = 0, totalDev = 0;
    for (const fs::path& f : files) {
        std::vector<uint8_t> bytes;
        std::vector<HarteTest> tests;
        std::string err;
        if (!readFile(f, bytes) || !parseHarte(bytes, tests, err)) {
            std::fprintf(stderr, "m68k_harte: %s: %s\n", f.string().c_str(),
                         err.empty() ? "cannot read file" : err.c_str());
            return 2;
        }
        uint64_t pass = 0, smp = 0, aef = 0, dev = 0;
        int listed = 0;
        std::string diff;
        for (const HarteTest& t : tests) {
            const HarteResult r = runner.run(t, diff);
            // DECISION M1: a test passes when it is exact or falls in one of the three allowed
            // classes. A listed manual deviation that no longer fails is a stale table entry.
            if (r != HarteResult::ManualDeviation) {
                if (const char* id = manualDeviation(t.name)) {
                    std::printf("  WARNING stale manual deviation (%s): %s no longer fails\n", id,
                                t.name.c_str());
                }
            }
            if (r == HarteResult::ManualDeviation) {
                ++pass;
                ++dev;
            } else if (r == HarteResult::Pass) {
                ++pass;
            } else if (r == HarteResult::SelfModifyingPrefetch) {
                ++pass;
                ++smp;
            } else if (r == HarteResult::AddressErrorFrame) {
                ++pass;
                ++aef;
            } else if (listed < maxFail) {
                ++listed;
                std::printf("  FAIL %s\n", t.name.c_str());
                printFirstLines(diff, 3);
            }
        }
        std::printf("%s: %llu/%llu (smp %llu, aef %llu, dev %llu)\n", f.stem().string().c_str(),
                    static_cast<unsigned long long>(pass), static_cast<unsigned long long>(tests.size()),
                    static_cast<unsigned long long>(smp), static_cast<unsigned long long>(aef),
                    static_cast<unsigned long long>(dev));
        totalTests += tests.size();
        totalPass += pass;
        totalSmp += smp;
        totalAef += aef;
        totalDev += dev;
    }
    std::printf("total: %llu/%llu (smp %llu, aef %llu, dev %llu)\n",
                static_cast<unsigned long long>(totalPass), static_cast<unsigned long long>(totalTests),
                static_cast<unsigned long long>(totalSmp), static_cast<unsigned long long>(totalAef),
                static_cast<unsigned long long>(totalDev));
    return totalPass == totalTests ? 0 : 1;
}
