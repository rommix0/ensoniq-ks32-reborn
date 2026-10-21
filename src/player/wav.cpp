#include "player/wav.h"

namespace ens::player {

namespace {

void put32(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void put16(std::vector<uint8_t>& o, uint32_t v) {
    o.push_back(static_cast<uint8_t>(v));
    o.push_back(static_cast<uint8_t>(v >> 8));
}
void putTag(std::vector<uint8_t>& o, const char* tag) { o.insert(o.end(), tag, tag + 4); }

}  // namespace

std::vector<uint8_t> makeWav16(const std::vector<int16_t>& interleaved, int channels, uint32_t rate) {
    const uint32_t ch = static_cast<uint32_t>(channels);
    const uint32_t dataBytes = static_cast<uint32_t>(interleaved.size() * 2);
    std::vector<uint8_t> o;
    o.reserve(44 + dataBytes);
    putTag(o, "RIFF");
    put32(o, 36 + dataBytes);
    putTag(o, "WAVE");
    putTag(o, "fmt ");
    put32(o, 16);
    put16(o, 1);  // PCM
    put16(o, ch);
    put32(o, rate);
    put32(o, rate * ch * 2);
    put16(o, ch * 2);
    put16(o, 16);
    putTag(o, "data");
    put32(o, dataBytes);
    for (int16_t s : interleaved) put16(o, static_cast<uint16_t>(s));
    return o;
}

}  // namespace ens::player
