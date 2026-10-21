#include "player/sample_map.h"

#include "player/json.h"

namespace ens::player {

const Wave* SampleMap::find(int set, int number) const {
    for (const Wave& w : waves)
        if (w.set == set && w.number == number) return &w;
    return nullptr;
}

const Zone& zoneForKey(const Wave& w, int key) {
    for (const Zone& z : w.zones)
        if (key <= z.topKey) return z;
    return w.zones.back();
}

namespace {

bool hexField(const JsonValue& obj, const char* name, uint64_t& out) {
    const JsonValue* v = obj.find(name);
    return v && v->type == JsonValue::Type::String && parseHex(v->str, out);
}

bool parseZone(const JsonValue& zj, Zone& z) {
    if (zj.type != JsonValue::Type::Object) return false;
    int start = 0, loopStart = 0, end = 0;
    uint64_t cr = 0;
    if (!jsonInt(zj.find("top_key"), 0, 127, z.topKey)) return false;
    if (!jsonInt(zj.find("pitch_word"), -32768, 65535, z.pitchWord)) return false;
    if (!jsonInt(zj.find("bank"), 0, 3, z.bank)) return false;
    if (!jsonInt(zj.find("start"), 0, 0x3FFFFFFF, start)) return false;
    if (!jsonInt(zj.find("loop_start"), 0, 0x3FFFFFFF, loopStart)) return false;
    if (!jsonInt(zj.find("end"), 0, 0x3FFFFFFF, end)) return false;
    if (!jsonInt(zj.find("end_frac16"), 0, 0xFFFF, z.endFrac)) return false;
    if (!hexField(zj, "cr_low_byte", cr) || cr > 0xFF) return false;
    z.start = static_cast<uint32_t>(start);
    z.loopStart = static_cast<uint32_t>(loopStart);
    z.end = static_cast<uint32_t>(end);
    z.crLow = static_cast<uint8_t>(cr);
    return true;
}

bool parseSet(const JsonValue& root, const char* key, int set, SampleMap& out, std::string& err) {
    const JsonValue* list = root.find(key);
    if (!list || list->type != JsonValue::Type::Array) {
        err = std::string("sample map: missing array ") + key;
        return false;
    }
    for (const JsonValue& wj : list->arr) {
        Wave w;
        w.set = set;
        if (wj.type != JsonValue::Type::Object || !jsonInt(wj.find("wave"), 0, 0xFFFF, w.number)) {
            err = std::string("sample map: bad wave entry in ") + key;
            return false;
        }
        if (const JsonValue* n = wj.find("name"); n && n->type == JsonValue::Type::String) w.name = n->str;
        if (const JsonValue* n = wj.find("note"); n && n->type == JsonValue::Type::String) w.note = n->str;
        const JsonValue* zones = wj.find("zones");
        if (!zones) continue;  // wave without key zones: not playable
        if (zones->type != JsonValue::Type::Array) {
            err = "sample map: zones is not an array in wave " + std::to_string(w.number);
            return false;
        }
        for (const JsonValue& zj : zones->arr) {
            Zone z;
            if (!parseZone(zj, z)) {
                err = "sample map: bad zone in wave " + std::to_string(w.number) + " of " + key;
                return false;
            }
            w.zones.push_back(z);
        }
        if (!w.zones.empty()) out.waves.push_back(std::move(w));
    }
    return true;
}

}  // namespace

bool parseSampleMap(const std::string& json, SampleMap& out, std::string& err) {
    out = SampleMap();
    JsonValue root;
    if (!parseJson(json, root, err)) return false;
    if (root.type != JsonValue::Type::Object) {
        err = "sample map: top level is not an object";
        return false;
    }
    err.clear();
    if (!parseSet(root, "internal_waves", 0, out, err)) return false;
    if (!parseSet(root, "expansion_waves_rom3", 1, out, err)) return false;
    return true;
}

}  // namespace ens::player
