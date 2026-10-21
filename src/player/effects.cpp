#include "player/effects.h"

#include <algorithm>

#include "player/json.h"

namespace ens::player {

const EspProgram* EffectLibrary::find(int index) const {
    for (const EspProgram& p : programs)
        if (p.index == index) return &p;
    return nullptr;
}

namespace {

constexpr size_t kInstrWords = 89;  // pc 0x00..0x58

bool isObject(const JsonValue* v) { return v && v->type == JsonValue::Type::Object; }
bool isArray(const JsonValue* v) { return v && v->type == JsonValue::Type::Array; }
bool isString(const JsonValue* v) { return v && v->type == JsonValue::Type::String; }

// {"7F": "400000", ...} -> (address, value) pairs sorted by address. Duplicate addresses are an error.
bool parseGprMap(const JsonValue* obj, std::vector<std::pair<uint8_t, uint32_t>>& out) {
    out.clear();
    if (!isObject(obj)) return false;
    for (const auto& kv : obj->obj) {
        uint64_t addr = 0, val = 0;
        if (!parseHex(kv.first, addr) || addr > 0xFF) return false;
        if (!isString(&kv.second) || !parseHex(kv.second.str, val) || val > 0xFFFFFF) return false;
        out.emplace_back(static_cast<uint8_t>(addr), static_cast<uint32_t>(val));
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (size_t i = 1; i < out.size(); ++i)
        if (out[i - 1].first == out[i].first) return false;
    return true;
}

bool parseTables(const JsonValue* list, std::vector<DramTable>& out) {
    if (!isArray(list)) return false;
    for (const JsonValue& tj : list->arr) {
        const JsonValue* a = isObject(&tj) ? tj.find("dram_word_address") : nullptr;
        const JsonValue* ws = isObject(&tj) ? tj.find("words") : nullptr;
        uint64_t addr = 0;
        if (!isString(a) || !parseHex(a->str, addr) || addr > 0xFFFFFF || !isArray(ws)) return false;
        DramTable t;
        t.wordAddr = static_cast<uint32_t>(addr);
        for (const JsonValue& wj : ws->arr) {
            uint64_t w = 0;
            if (!isString(&wj) || !parseHex(wj.str, w) || w > 0xFFFF) return false;
            t.words.push_back(static_cast<uint16_t>(w));
        }
        out.push_back(std::move(t));
    }
    return true;
}

bool parseProgram(const JsonValue& pj, EspProgram& p, std::string& err) {
    if (!isObject(&pj) || !jsonInt(pj.find("index"), 0, 255, p.index)) {
        err = "esp programs: bad program entry";
        return false;
    }
    const std::string who = "esp programs: program " + std::to_string(p.index) + ": ";

    // Header byte 5 is the effect id: "4502e00000005900" -> 0x00.
    const JsonValue* hdr = pj.find("header_hex");
    uint64_t header = 0;
    if (!isString(hdr) || hdr->str.size() != 16 || !parseHex(hdr->str, header)) {
        err = who + "bad header_hex";
        return false;
    }
    p.effectId = static_cast<int>((header >> 16) & 0xFF);

    const JsonValue* instr = pj.find("instr");
    if (!isObject(instr) || instr->obj.size() != kInstrWords) {
        err = who + "instr must hold 89 words";
        return false;
    }
    p.instr.assign(kInstrWords, 0);
    std::vector<bool> seen(kInstrWords, false);
    for (const auto& kv : instr->obj) {
        uint64_t pc = 0, word = 0;
        if (!parseHex(kv.first, pc) || pc >= kInstrWords || seen[pc] || !isString(&kv.second) ||
            !parseHex(kv.second.str, word) || word > 0xFFFFFFFFFFFFull) {
            err = who + "bad instr entry " + kv.first;
            return false;
        }
        seen[pc] = true;
        p.instr[pc] = word;
    }

    if (!parseGprMap(pj.find("gpr"), p.gpr)) {
        err = who + "bad gpr map";
        return false;
    }

    const JsonValue* recs = pj.find("records");
    if (!isArray(recs)) {
        err = who + "missing records";
        return false;
    }
    p.type1Count = p.type2Count = 0;
    for (const JsonValue& r : recs->arr) {
        int type = 0, count = 0;
        if (!isObject(&r) || !jsonInt(r.find("type"), 0, 255, type) || !jsonInt(r.find("count"), 0, 65535, count)) {
            err = who + "bad record";
            return false;
        }
        if (type == 1) p.type1Count += count;
        if (type == 2) p.type2Count += count;
    }
    return true;
}

}  // namespace

bool parseEffects(const std::string& programsJson, const std::string& defaultsJson, EffectLibrary& out,
                  std::string& err) {
    out = EffectLibrary();
    JsonValue pr, df;
    if (!parseJson(programsJson, pr, err)) {
        err = "esp programs: " + err;
        return false;
    }
    if (!parseJson(defaultsJson, df, err)) {
        err = "effect defaults: " + err;
        return false;
    }
    err.clear();

    const JsonValue* plist = pr.find("programs");
    if (!isArray(plist)) {
        err = "esp programs: missing programs array";
        return false;
    }
    const JsonValue* dlist = df.find("programs");
    if (!isArray(dlist)) {
        err = "effect defaults: missing programs array";
        return false;
    }
    if (const JsonValue* fade = df.find("fade"); isObject(fade)) {
        if (!jsonInt(fade->find("frames"), 0, 1 << 24, out.fadeFrames)) {
            err = "effect defaults: bad fade frames";
            return false;
        }
    }

    for (const JsonValue& pj : plist->arr) {
        EspProgram p;
        if (!parseProgram(pj, p, err)) return false;
        if (out.find(p.index)) {
            err = "esp programs: duplicate program " + std::to_string(p.index);
            return false;
        }

        const JsonValue* match = nullptr;
        for (const JsonValue& dj : dlist->arr) {
            int idx = 0;
            if (isObject(&dj) && jsonInt(dj.find("index"), 0, 255, idx) && idx == p.index) {
                match = &dj;
                break;
            }
        }
        if (!match) {
            err = "effect defaults: no entry for program " + std::to_string(p.index);
            return false;
        }
        if (const JsonValue* n = match->find("name"); isString(n)) p.name = n->str;
        if (!parseGprMap(match->find("gpr"), p.mix)) {
            err = "effect defaults: bad gpr map for program " + std::to_string(p.index);
            return false;
        }
        out.programs.push_back(std::move(p));
    }

    // Wave-table contents belong to program 10 only.
    EspProgram* p10 = nullptr;
    for (EspProgram& p : out.programs)
        if (p.index == 10) p10 = &p;
    if (const JsonValue* tj = pr.find("dram_tables_for_index_10"); tj && p10) {
        if (!parseTables(tj, p10->tables)) {
            err = "esp programs: bad dram_tables_for_index_10";
            return false;
        }
    }
    return true;
}

}  // namespace ens::player
