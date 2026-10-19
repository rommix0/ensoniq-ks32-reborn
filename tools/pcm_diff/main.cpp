// pcm_diff: compare two raw interleaved int16 LE PCM files.
//   pcm_diff <a.pcm> <b.pcm> [--channels N]     (default 4: auxL auxR mainL mainR)
// Prints the frame counts, the first differing frame and per-channel max |diff| and RMS diff.
// Exit status: 0 identical, 1 different or unreadable, 2 bad arguments.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "trace/trace.h"

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: pcm_diff <a.pcm> <b.pcm> [--channels N]\n"
                 "  Compares raw int16 LE files with N interleaved channels (default 4).\n"
                 "  Exit 0 when identical, 1 when they differ or a file cannot be read.\n");
}

int fail2(const char* msg, const char* arg) {
    std::fprintf(stderr, "pcm_diff: %s%s%s\n", msg, arg ? " " : "", arg ? arg : "");
    usage();
    return 2;
}

bool readPcm(const char* path, std::vector<int16_t>& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "pcm_diff: cannot open %s\n", path);
        return false;
    }
    std::vector<unsigned char> bytes;
    unsigned char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    const bool readError = std::ferror(f) != 0;
    std::fclose(f);
    if (readError) {
        std::fprintf(stderr, "pcm_diff: error reading %s\n", path);
        return false;
    }
    if (bytes.size() % 2 != 0) {
        std::fprintf(stderr, "pcm_diff: %s has an odd byte count (not int16 data)\n", path);
        return false;
    }
    out.resize(bytes.size() / 2);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<int16_t>(static_cast<uint16_t>(bytes[2 * i] | (bytes[2 * i + 1] << 8)));
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const char* paths[2] = {nullptr, nullptr};
    int n = 0;
    long channels = 4;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!std::strcmp(a, "--channels")) {
            char* end = nullptr;
            if (i + 1 >= argc) return fail2("--channels needs a number", nullptr);
            channels = std::strtol(argv[i + 1], &end, 10);
            if (end == argv[i + 1] || *end != '\0' || channels < 1 || channels > 64)
                return fail2("--channels must be 1..64, got", argv[i + 1]);
            ++i;
        } else if (a[0] == '-' && a[1] != '\0') {
            return fail2("unknown option", a);
        } else if (n < 2) {
            paths[n++] = a;
        } else {
            return fail2("unexpected extra argument", a);
        }
    }
    if (n != 2) return fail2("expected two PCM files", nullptr);

    std::vector<int16_t> a, b;
    if (!readPcm(paths[0], a) || !readPcm(paths[1], b)) return 1;
    const ens::PcmCompare r = ens::pcmCompare(a, b, static_cast<int>(channels));

    std::printf("frames: a=%zu b=%zu\n", r.framesA, r.framesB);
    if (r.firstDiffFrame >= 0) std::printf("first differing frame: %lld\n", static_cast<long long>(r.firstDiffFrame));
    else std::printf("first differing frame: none%s\n", r.lengthDiffers ? " (within the common length)" : "");
    if (r.lengthDiffers) std::printf("lengths differ: a=%zu samples, b=%zu samples\n", a.size(), b.size());
    for (size_t c = 0; c < r.maxAbs.size(); ++c)
        std::printf("ch%zu: max|diff|=%d rms=%.4f\n", c, r.maxAbs[c], r.rms[c]);
    std::printf(r.identical ? "identical\n" : "DIFFERENT\n");
    return r.identical ? 0 : 1;
}
