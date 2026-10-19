// ens_trace: run a trace (docs/trace-format.md) on our OTIS + ESP cores and write raw PCM.
//   ens_trace <trace> <out.pcm>
// Output: 4 little-endian int16 per frame: auxL auxR mainL mainR.
// Exit status: 0 ok, 1 file/trace error, 2 bad arguments.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "trace/trace.h"

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: ens_trace <trace> <out.pcm>\n"
                 "  Runs the trace on the OTIS/ESP cores and writes int16 LE auxL auxR mainL mainR\n"
                 "  per frame to <out.pcm>. Relative wave paths are resolved against the trace's\n"
                 "  directory. Exit 1 on an unreadable file or a trace error (reported as\n"
                 "  <trace>:<line>: message).\n");
}

int fail2(const char* msg, const char* arg) {
    std::fprintf(stderr, "ens_trace: %s%s%s\n", msg, arg ? " " : "", arg ? arg : "");
    usage();
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    const char* paths[2] = {nullptr, nullptr};
    int n = 0;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') return fail2("unknown option", argv[i]);
        if (n == 2) return fail2("unexpected extra argument", argv[i]);
        paths[n++] = argv[i];
    }
    if (n != 2) return fail2("expected a trace file and an output file", nullptr);

    std::FILE* f = std::fopen(paths[0], "rb");
    if (!f) {
        std::fprintf(stderr, "ens_trace: cannot open %s\n", paths[0]);
        return 1;
    }
    std::string text;
    char buf[4096];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, got);
    const bool readError = std::ferror(f) != 0;
    std::fclose(f);
    if (readError) {
        std::fprintf(stderr, "ens_trace: error reading %s\n", paths[0]);
        return 1;
    }

    const std::string tracePath = paths[0];
    const size_t slash = tracePath.find_last_of("/\\");
    const std::string baseDir = slash == std::string::npos ? std::string() : tracePath.substr(0, slash);

    ens::TraceRunner runner;
    ens::TraceError err;
    if (!runner.load(text, baseDir, err)) {
        std::fprintf(stderr, "ens_trace: %s:%d: %s\n", paths[0], err.line, err.msg.c_str());
        return 1;
    }
    std::vector<int16_t> pcm;
    runner.run(pcm);

    std::vector<unsigned char> bytes(pcm.size() * 2);
    for (size_t i = 0; i < pcm.size(); ++i) {
        const uint16_t u = static_cast<uint16_t>(pcm[i]);
        bytes[2 * i] = static_cast<unsigned char>(u & 0xFF);
        bytes[2 * i + 1] = static_cast<unsigned char>(u >> 8);
    }
    std::FILE* o = std::fopen(paths[1], "wb");
    if (!o) {
        std::fprintf(stderr, "ens_trace: cannot create %s\n", paths[1]);
        return 1;
    }
    const bool ok = (bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), o) == bytes.size());
    const bool closed = std::fclose(o) == 0;
    if (!ok || !closed) {
        std::fprintf(stderr, "ens_trace: error writing %s\n", paths[1]);
        return 1;
    }
    return 0;
}
