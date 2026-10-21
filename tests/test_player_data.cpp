#include <fstream>
#include <sstream>
#include <string>

#include "check.h"
#include "player/effects.h"
#include "player/json.h"
#include "player/sample_map.h"

using namespace ens::player;

namespace {

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Minimal effect_defaults for every program index 0..10 (same format as the real file).
std::string inlineDefaults(int skipIndex) {
    std::string s = R"({"source":"test","fade":{"gpr":"7F","frames":595},"programs":[)";
    bool first = true;
    for (int i = 0; i <= 10; ++i) {
        if (i == skipIndex) continue;
        if (!first) s += ",";
        first = false;
        s += R"({"index":)" + std::to_string(i) + R"(,"name":")" + (i == 0 ? "Chorus" : "Fx" + std::to_string(i)) +
             R"(","gpr":{"7F":"400000","7C":"200000"},"basis":{}})";
    }
    return s + "]}";
}

}  // namespace

TEST(json_parses_nested_and_escapes) {
    const std::string text = R"({"a":[1,-12.5e2,true,null],"s":"q\"\\\nA"})";
    JsonValue v;
    std::string err;
    CHECK(parseJson(text, v, err));
    const JsonValue* a = v.find("a");
    CHECK(a != nullptr);
    if (!a) return;
    CHECK_EQ(a->arr.size(), 4u);
    CHECK_EQ(a->arr[1].num, -1250.0);
    CHECK(a->arr[2].type == JsonValue::Type::Bool && a->arr[2].b);
    CHECK(a->arr[3].type == JsonValue::Type::Null);
    const JsonValue* s = v.find("s");
    CHECK(s != nullptr);
    if (s) CHECK(s->str == std::string("q\"\\\nA"));
    CHECK(v.find("missing") == nullptr);
    CHECK(a->find("x") == nullptr);  // not an object
}

TEST(json_unicode_escape_and_depth_limit) {
    JsonValue v;
    std::string err;
    CHECK(parseJson(R"("éA")", v, err));
    CHECK(v.str == "\xC3\xA9" "A");
    CHECK(!parseJson(std::string(200, '[') + std::string(200, ']'), v, err));
    CHECK(!err.empty());
    CHECK(parseJson(std::string(60, '[') + std::string(60, ']'), v, err));
    CHECK(!parseJson("[1,]", v, err));
    CHECK(!parseJson("{} x", v, err));
    CHECK(!parseJson("", v, err));
}

TEST(json_rejects_truncations_without_crash) {
    const std::string text = R"({"a":[1,-12.5e2,true,null],"s":"q\"\\\nA"})";
    for (size_t n = 0; n < text.size(); ++n) {
        JsonValue v;
        std::string err;
        CHECK(!parseJson(text.substr(0, n), v, err));
        CHECK(!err.empty());
    }
}

TEST(sample_map_real_file) {
    SampleMap m;
    std::string err;
    CHECK(parseSampleMap(readFile(ENS_DATA_DIR "/sample_map.json"), m, err));
    const Wave* w0 = m.find(0, 0);
    CHECK(w0 != nullptr);
    if (!w0) return;
    CHECK_EQ(w0->zones.size(), 3u);
    const Zone& z = w0->zones[0];
    CHECK_EQ(z.topKey, 54);
    CHECK_EQ(z.pitchWord, -12304);
    CHECK_EQ(z.bank, 0);
    CHECK_EQ(z.start, 318965u);
    CHECK_EQ(z.loopStart, 334150u);
    CHECK_EQ(z.end, 349177u);
    CHECK_EQ(z.endFrac, 0);
    CHECK_EQ(z.crLow, 0x1b);

    const Wave* r0 = m.find(1, 0);
    CHECK(r0 != nullptr);
    if (r0) {
        CHECK(r0->name == "VIOLA SECT");
        CHECK_EQ(r0->zones.size(), 3u);
        CHECK_EQ(r0->zones[0].bank, 1);
    }
    CHECK(m.find(0, 126) == nullptr);
    CHECK(m.find(0, 127) == nullptr);
    CHECK(m.find(0, 125) != nullptr);
    CHECK(m.find(0, 0x78) != nullptr);

    CHECK(&zoneForKey(*w0, 54) == &w0->zones[0]);
    CHECK(&zoneForKey(*w0, 55) == &w0->zones[1]);
    CHECK(&zoneForKey(*w0, 127) == &w0->zones[2]);
    CHECK(&zoneForKey(*w0, 0) == &w0->zones[0]);
}

TEST(sample_map_bad_input_is_error) {
    SampleMap m;
    std::string err;
    CHECK(!parseSampleMap("not json", m, err));
    CHECK(!err.empty());
    CHECK(!parseSampleMap("{}", m, err));
    CHECK(!parseSampleMap(R"({"internal_waves":[{"wave":0,"zones":[{"top_key":1}]}],"expansion_waves_rom3":[]})", m, err));
}

TEST(effects_real_files) {
    EffectLibrary lib;
    std::string err;
    CHECK(parseEffects(readFile(ENS_DATA_DIR "/esp/esp_programs.json"), inlineDefaults(-1), lib, err));
    CHECK_EQ(lib.programs.size(), 11u);
    if (lib.programs.size() != 11u) return;
    CHECK_EQ(lib.fadeFrames, 595);

    const EspProgram& p0 = lib.programs[0];
    CHECK_EQ(p0.index, 0);
    CHECK(p0.name == "Chorus");
    CHECK_EQ(p0.instr.size(), 89u);
    CHECK(p0.instr[0] == 0xFFFFFFFF9040ull);
    CHECK(p0.instr[0x58] == 0xFFFFFFFFF040ull);
    CHECK_EQ(p0.type1Count, 47);
    CHECK_EQ(p0.type2Count, 42);
    CHECK_EQ(p0.gpr[0].first, 0x00);
    CHECK_EQ(p0.gpr[0].second, 0x039900u);

    CHECK(lib.programs[2].instr == lib.programs[1].instr);

    const EspProgram& p10 = lib.programs[10];
    CHECK_EQ(p10.tables.size(), 2u);
    if (p10.tables.size() == 2u) {
        CHECK_EQ(p10.tables[0].wordAddr, 0x1FBFu);
        CHECK_EQ(p10.tables[0].words.size(), 65u);
        CHECK_EQ(p10.tables[0].words[0], 0x8001);
        CHECK_EQ(p10.tables[0].words[64], 0x7FFF);
    }
    bool found = false;
    for (const auto& g : p10.gpr)
        if (g.first == 0xF5) {
            found = true;
            CHECK_EQ(g.second, 0x1F7DFFu);
        }
    CHECK(found);

    for (const EspProgram& p : lib.programs) {
        bool has7F = false;
        for (size_t i = 0; i < p.mix.size(); ++i) {
            if (p.mix[i].first == 0x7F) has7F = true;
            if (i) CHECK(p.mix[i - 1].first < p.mix[i].first);
        }
        CHECK(has7F);
        for (size_t i = 1; i < p.gpr.size(); ++i) CHECK(p.gpr[i - 1].first < p.gpr[i].first);
        if (p.index != 10) CHECK(p.tables.empty());
    }

    CHECK(lib.find(5) == &lib.programs[5]);
    CHECK(lib.find(11) == nullptr);
    CHECK(lib.find(-1) == nullptr);
}

TEST(effects_defaults_missing_program_is_error) {
    EffectLibrary lib;
    std::string err;
    CHECK(!parseEffects(readFile(ENS_DATA_DIR "/esp/esp_programs.json"), inlineDefaults(5), lib, err));
    CHECK(err.find('5') != std::string::npos);
}

TEST(effects_malformed_input_is_error) {
    EffectLibrary lib;
    std::string err;
    CHECK(!parseEffects("{", inlineDefaults(-1), lib, err));
    CHECK(!err.empty());
    CHECK(!parseEffects("{}", inlineDefaults(-1), lib, err));
    CHECK(!parseEffects(readFile(ENS_DATA_DIR "/esp/esp_programs.json"), "[]", lib, err));
}
