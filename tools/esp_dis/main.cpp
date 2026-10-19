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
    std::fprintf(stderr, "usage: esp_dis <file> [--offset BYTES] [--count WORDS] [--be|--le]\n");
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
        if (!std::strcmp(argv[i], "--be")) {
            little = false;
        } else if (!std::strcmp(argv[i], "--le")) {
            little = true;
        } else if (!std::strcmp(argv[i], "--offset") || !std::strcmp(argv[i], "--count")) {
            const bool isOffset = argv[i][2] == 'o';
            long long v = 0;
            if (i + 1 >= argc || !parseNum(argv[i + 1], v)) {
                std::fprintf(stderr, "esp_dis: %s needs a non-negative number\n", argv[i]);
                return 2;
            }
            (isOffset ? offset : count) = v;
            ++i;
        } else if (argv[i][0] == '-' && argv[i][1] == '-') {
            std::fprintf(stderr, "esp_dis: unknown option %s\n", argv[i]);
            usage();
            return 2;
        } else if (!path) {
            path = argv[i];
        } else {
            usage();
            return 2;
        }
    }
    if (!path) {
        usage();
        return 2;
    }

    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "esp_dis: cannot open %s\n", path);
        return 1;
    }
    std::vector<unsigned char> data;
    unsigned char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);

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
