// esp_dis: disassemble a raw ES5510 ESP program image (6-byte words, big-endian by default).
//   esp_dis <file> [--offset BYTES] [--count WORDS] [--be|--le]
// Output per word: "<12 hex digits>  <PC  OP ...>" where PC counts words from --offset.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "esp/esp_isa.h"

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: esp_dis <file> [--offset BYTES] [--count WORDS] [--be|--le]\n"
                 "  Reads 6-byte words (big-endian unless --le) starting at byte BYTES and\n"
                 "  prints at most WORDS lines. A partial trailing word is ignored with a\n"
                 "  warning on stderr (exit status stays 0).\n");
}

int fail2(const char* msg, const char* arg) {
    std::fprintf(stderr, "esp_dis: %s%s%s\n", msg, arg ? " " : "", arg ? arg : "");
    usage();
    return 2;
}

bool parseNum(const char* s, long long& out) {
    char* end = nullptr;
    out = std::strtoll(s, &end, 0);
    return end != s && *end == '\0' && out >= 0;
}

}  // namespace

int main(int argc, char** argv) {
    const char* path = nullptr;
    long long offset = 0, count = -1;
    bool little = false;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (a[0] == '-') {
            if (!std::strcmp(a, "--be")) {
                little = false;
            } else if (!std::strcmp(a, "--le")) {
                little = true;
            } else if (!std::strcmp(a, "--offset") || !std::strcmp(a, "--count")) {
                long long v = 0;
                if (i + 1 >= argc || !parseNum(argv[i + 1], v))
                    return fail2("needs a non-negative number after", a);
                (!std::strcmp(a, "--offset") ? offset : count) = v;
                ++i;
            } else {
                return fail2("unknown option", a);
            }
        } else if (!path) {
            path = a;
        } else {
            return fail2("unexpected extra argument", a);
        }
    }
    if (!path) return fail2("no input file given", nullptr);

    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "esp_dis: cannot open %s\n", path);
        return 1;
    }
    std::vector<unsigned char> data;
    unsigned char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    const bool readError = std::ferror(f) != 0;
    std::fclose(f);
    if (readError) {
        std::fprintf(stderr, "esp_dis: error reading %s\n", path);
        return 1;
    }

    if (static_cast<unsigned long long>(offset) > data.size()) {
        std::fprintf(stderr, "esp_dis: offset %lld is past the end of the file (%zu bytes)\n",
                     offset, data.size());
        return 1;
    }
    const size_t avail = data.size() - static_cast<size_t>(offset);
    size_t words = avail / 6;
    if (count >= 0 && static_cast<unsigned long long>(count) < words)
        words = static_cast<size_t>(count);
    if (avail % 6 != 0 && (count < 0 || static_cast<size_t>(count) > avail / 6))
        std::fprintf(stderr, "esp_dis: warning: %zu trailing byte(s) ignored\n", avail % 6);

    for (size_t w = 0; w < words; ++w) {
        const unsigned char* p = &data[static_cast<size_t>(offset) + w * 6];
        uint64_t word = 0;
        for (int k = 0; k < 6; ++k) word |= uint64_t(p[little ? 5 - k : k]) << (8 * (5 - k));
        std::printf("%012llX  %s\n", static_cast<unsigned long long>(word),
                    ens::esp::disassemble(ens::esp::Instr::decode(word), static_cast<int>(w))
                        .c_str());
    }
    return 0;
}
