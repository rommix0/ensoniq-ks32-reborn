#include <cmath>
#include <cstdint>
#include <utility>

#include "trace/trace.h"

namespace ens {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

std::vector<std::string> tokens(const std::string& line) {
    std::vector<std::string> t;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && isSpace(line[i])) ++i;
        size_t j = i;
        while (j < line.size() && !isSpace(line[j])) ++j;
        if (j > i) t.push_back(line.substr(i, j - i));
        i = j;
    }
    return t;
}

// Whole token must be digits (decimal) or hex digits with an optional 0x prefix.
bool parseNum(const std::string& s, int base, uint64_t maxv, uint64_t& out) {
    size_t i = 0;
    if (base == 16 && s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) i = 2;
    if (i >= s.size() || s.size() - i > 16) return false;
    uint64_t v = 0;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        if (v > (UINT64_MAX - static_cast<uint64_t>(d)) / static_cast<uint64_t>(base)) return false;
        v = v * static_cast<uint64_t>(base) + static_cast<uint64_t>(d);
    }
    if (v > maxv) return false;
    out = v;
    return true;
}

bool isAbsolute(const std::string& p) {
    return !p.empty() && (p[0] == '/' || p[0] == '\\' || (p.size() > 1 && p[1] == ':'));
}

std::string joinPath(const std::string& dir, const std::string& p) {
    if (dir.empty() || isAbsolute(p)) return p;
    const char last = dir[dir.size() - 1];
    return (last == '/' || last == '\\') ? dir + p : dir + "/" + p;
}

}  // namespace

bool parseTrace(const std::string& text, const std::string& baseDir, std::vector<TraceCmd>& out,
                TraceError& err) {
    err = TraceError{};
    int lineNo = 0;
    uint64_t totalFrames = 0;
    bool framesSeen = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        ++lineNo;

        const std::vector<std::string> t = tokens(line);
        if (t.empty() || t[0][0] == '#') continue;

        auto fail = [&](const std::string& msg) {
            err.line = lineNo;
            err.msg = msg;
            return false;
        };
        auto need = [&](size_t n, const char* form) {
            if (t.size() == n) return true;
            fail(std::string("expected '") + form + "'");
            return false;
        };
        auto num = [&](size_t idx, int base, uint64_t maxv, const char* what, uint64_t& v) {
            if (parseNum(t[idx], base, maxv, v)) return true;
            fail(std::string("bad ") + what + " '" + t[idx] + "'");
            return false;
        };

        TraceCmd c;
        c.line = lineNo;
        uint64_t v1 = 0, v2 = 0;
        const std::string& cmd = t[0];
        if (cmd == "wave") {
            // The path is the rest of the line (it may contain spaces).
            size_t p = line.find("wave") + 4;
            while (p < line.size() && isSpace(line[p])) ++p;
            while (p < line.size() && !isSpace(line[p])) ++p;  // the bank token
            while (p < line.size() && isSpace(line[p])) ++p;
            size_t e = line.size();
            while (e > p && isSpace(line[e - 1])) --e;
            if (t.size() < 3) { fail("expected 'wave <bank 0|1> <path>'"); return false; }
            if (!num(1, 10, 1, "wave bank (0 or 1)", v1)) return false;
            if (framesSeen) { fail("wave must come before the first 'frames'"); return false; }
            c.op = TraceOp::Wave;
            c.a = static_cast<uint32_t>(v1);
            c.path = joinPath(baseDir, line.substr(p, e - p));
        } else if (cmd == "dram") {
            if (!need(2, "dram <words>") || !num(1, 10, kTraceMaxDramWords, "dram size", v1))
                return false;
            if (v1 == 0 || (v1 & (v1 - 1)) != 0) {
                fail("dram size must be a power of two (1.." + std::to_string(kTraceMaxDramWords) + ")");
                return false;
            }
            if (framesSeen) { fail("dram must come before the first 'frames'"); return false; }
            c.op = TraceOp::Dram;
            c.a = static_cast<uint32_t>(v1);
        } else if (cmd == "otis") {
            if (!need(3, "otis <reg 0-15> <hex16>") || !num(1, 10, 15, "otis register", v1) ||
                !num(2, 16, 0xFFFF, "16-bit hex value", v2))
                return false;
            c.op = TraceOp::OtisWrite;
            c.a = static_cast<uint32_t>(v1);
            c.b = static_cast<uint32_t>(v2);
        } else if (cmd == "esp") {
            if (!need(3, "esp <addr hex8> <hex8>") || !num(1, 16, 0xFF, "8-bit hex address", v1) ||
                !num(2, 16, 0xFF, "8-bit hex value", v2))
                return false;
            c.op = TraceOp::EspWrite;
            c.a = static_cast<uint32_t>(v1);
            c.b = static_cast<uint32_t>(v2);
        } else if (cmd == "esphalt") {
            if (!need(2, "esphalt <0|1>") || !num(1, 10, 1, "halt level (0 or 1)", v1)) return false;
            c.op = TraceOp::EspHalt;
            c.a = static_cast<uint32_t>(v1);
        } else if (cmd == "espidle") {
            if (!need(2, "espidle <n>") || !num(1, 10, kTraceMaxIdleSteps, "idle step count", v1))
                return false;
            c.op = TraceOp::EspIdle;
            c.a = static_cast<uint32_t>(v1);
        } else if (cmd == "frames") {
            if (!need(2, "frames <n>") || !num(1, 10, kTraceMaxTotalFrames, "frame count", v1))
                return false;
            totalFrames += v1;
            if (totalFrames > kTraceMaxTotalFrames) {
                fail("total frame count exceeds " + std::to_string(kTraceMaxTotalFrames));
                return false;
            }
            framesSeen = true;
            c.op = TraceOp::Frames;
            c.a = static_cast<uint32_t>(v1);
        } else {
            return fail("unknown command '" + cmd + "'");
        }
        out.push_back(std::move(c));
    }
    return true;
}

PcmCompare pcmCompare(const std::vector<int16_t>& a, const std::vector<int16_t>& b, int channels) {
    if (channels < 1) channels = 1;
    const size_t ch = static_cast<size_t>(channels);
    PcmCompare r;
    r.framesA = a.size() / ch;
    r.framesB = b.size() / ch;
    r.lengthDiffers = a.size() != b.size();
    r.maxAbs.assign(ch, 0);
    std::vector<double> sumSq(ch, 0.0);
    std::vector<size_t> count(ch, 0);
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    bool anyDiff = false;
    for (size_t i = 0; i < n; ++i) {
        const size_t c = i % ch;
        const int d = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
        if (d != 0 && !anyDiff) {
            anyDiff = true;
            r.firstDiffFrame = static_cast<int64_t>(i / ch);
        }
        if (d > r.maxAbs[c]) r.maxAbs[c] = d;
        sumSq[c] += static_cast<double>(d) * d;
        ++count[c];
    }
    r.rms.assign(ch, 0.0);
    for (size_t c = 0; c < ch; ++c)
        if (count[c]) r.rms[c] = std::sqrt(sumSq[c] / static_cast<double>(count[c]));
    r.identical = !anyDiff && !r.lengthDiffers;
    return r;
}

}  // namespace ens
