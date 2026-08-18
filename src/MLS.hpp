#pragma once
// ─── MLS — MakeLLM Script ────────────────────────────────────────────────────
// Компактный построчный DSL, который LLM генерирует вместо сырого GD
// level string. Одна строка = один глагол. Макросы (FLOOR, CORRIDOR, ...)
// раскрываются в десятки объектов. Парсер максимально толерантен: битая
// строка — предупреждение, а не провал всей генерации.
//
// Грамматика:
//   # комментарий
//   META name=... [speed=..]                        — метаданные (необязательно)
//   BLOCK x y [v=имя_варианта] [rot=deg] [scale=s] [flipx] [flipy]
//          [g=1,2] [c=канал] [z=B2] [notouch] [passable] [noglow] [hd]
//   SPIKE x [y=ground] [v=вариант]
//   SAW x y [size=small|medium|large]
//   ORB yellow|pink|red x y
//   PAD yellow|pink|red x y
//   PORTAL cube|ship|ball|ufo|wave|robot|spider|swing x [y]
//   PORTAL slow|normal|fast|faster|fastest x [y]      — скоростные
//   FLOOR x0..x1 [y=ground] [v=..]                  — сплошной ряд блоков
//   CEIL x0..x1 y=Y [v=..]                          — потолок
//   PILLAR x y0..y1 [v=..]                          — вертикальный столб
//   SPIKES x count=N [spacing=30] [v=..]            — ряд шипов
//   STAIRS-UP x steps=N [w=30] [h=30] [v=..]
//   STAIRS-DOWN x steps=N top=Y [w=30] [h=30] [v=..]
//   RUN x0..x1 y=Y [gap=G] [every=E] [v=..]         — платформы с пропусками
//   CORRIDOR x0..x1 floor=F ceil=C [v=..]           — пол + потолок (для ship/wave)
//   DECOR v=имя x y [scale] [rot] [z] [c=ch]
//   TRIG COLOR ch=N hex=RRGGBB at=X [dur=T] [blend] [op=0..1]
//   TRIG MOVE g=G at=X dx=DX dy=DY [dur=T] [ease=N]
//   TRIG PULSE ch=N hex=RRGGBB at=X [hold=T] [fade=T]
//   TRIG ALPHA g=G to=A at=X [dur=T]
//   TRIG ROTATE g=G deg=D at=X [dur=T] [center=G2]
//   TRIG SPAWN g=G at=X [delay=T]
//   TRIG TOGGLE g=G on=0|1 at=X
//   TRIG STOP g=G at=X

#include <algorithm>
#include <cmath>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

#include "Catalog.hpp"

namespace mll {

// Одна размещаемая единица после раскрытия макросов.
struct MObject {
    int         id     = 0;
    std::string type;                       // имя из каталога ("" для триггеров)
    float       x = 0, y = 0;
    float       rot = 0, scale = 1;
    bool        flipX = false, flipY = false;
    int         zLayer = -999;              // -999 = не трогать
    int         zOrder = 0;  bool hasZOrder = false;
    std::vector<int> groups;
    int         colorCh = 0;                // 0 = не назначать
    bool        noTouch = false, passable = false, noGlow = false;
    bool        highDetail = false, dontFade = false, dontEnter = false;
    bool        multiActivate = false;
    std::unordered_map<std::string, std::string> trig; // параметры триггеров
};

struct ParseResult {
    std::vector<MObject>     objects;
    std::vector<std::string> warnings;
    int                      badLines = 0;
};

namespace mls {

inline std::string trim(std::string s) {
    auto first = s.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    if (first > 0) s.erase(0, first);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    return s;
}

inline std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Безопасный float: парсит числовой префикс ("30," → 30), без исключений.
inline float tryFloat(const std::string& s, float dflt) {
    size_t i = 0, n = s.size();
    while (i < n && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i < n && s[i] == '+') ++i;
    if (i < n && s[i] == '-') ++i;
    bool digits = false, dot = false;
    for (; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (std::isdigit(c)) { digits = true; continue; }
        if (c == '.' && !dot) { dot = true; continue; }
        break;
    }
    if (!digits) return dflt;
    try { return std::stof(s.substr(0, i)); } catch (...) { return dflt; }
}

inline int tryInt(const std::string& s, int dflt) {
    return (int)std::lround(tryFloat(s, (float)dflt));
}

inline bool isNumericTok(const std::string& s) {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[i] == '+' || s[i] == '-') ++i;
    bool dig = false, dot = false;
    for (; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (std::isdigit(c)) { dig = true; continue; }
        if (c == '.' && !dot) { dot = true; continue; }
        return false;
    }
    return dig;
}

// Разбор одной строки: глагол + позиционные аргументы + key=value.
struct Line {
    std::string verb;
    std::vector<std::string> pos;
    std::unordered_map<std::string, std::string> kv;
    std::vector<std::string> flags;             // голые слова без '='

    bool flag(const char* k) const {
        std::string kk = lower(k);
        for (auto& f : flags) if (f == kk) return true;
        auto it = kv.find(kk);
        return it != kv.end() && (it->second.empty() || it->second == "1" || it->second == "true");
    }
    std::string str(const char* k, const std::string& dflt = "") const {
        auto it = kv.find(lower(k));
        return it == kv.end() ? dflt : it->second;
    }
    float fnum(const char* k, float dflt) const {
        auto it = kv.find(lower(k));
        return it == kv.end() ? dflt : tryFloat(it->second, dflt);
    }
    int inum(const char* k, int dflt) const {
        auto it = kv.find(lower(k));
        return it == kv.end() ? dflt : tryInt(it->second, dflt);
    }
};

inline bool parseLine(const std::string& raw, Line& out) {
    std::string s = trim(raw);
    if (s.empty() || s[0] == '#') return false;
    // выкидываем markdown-ограждения, если модель их всё же прислала
    if (s.rfind("```", 0) == 0) return false;

    std::vector<std::string> toks;
    std::string cur;
    bool quoted = false;
    for (char c : s) {
        if (c == '"') { quoted = !quoted; continue; }
        if (!quoted && (c == ' ' || c == '\t')) {
            if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
        } else cur.push_back(c);
    }
    if (!cur.empty()) toks.push_back(cur);
    if (toks.empty()) return false;

    out.verb = lower(toks[0]);
    bool seenKv = false;
    for (size_t i = 1; i < toks.size(); ++i) {
        auto eq = toks[i].find('=');
        if (eq != std::string::npos && eq > 0) {
            seenKv = true;
            out.kv[lower(toks[i].substr(0, eq))] = toks[i].substr(eq + 1);
        } else if (!seenKv) {
            out.pos.push_back(toks[i]);
        } else {
            out.flags.push_back(lower(toks[i]));
        }
    }
    return true;
}

// "120..900" → {120, 900}; также принимает "120-900" и "120:900".
inline bool parseRange(const std::string& tok, float& a, float& b) {
    for (const char* sep : {"..", ":"}) {
        auto p = tok.find(sep);
        if (p != std::string::npos) {
            a = tryFloat(tok.substr(0, p), 0);
            b = tryFloat(tok.substr(p + std::string(sep).size()), 0);
            return true;
        }
    }
    // одиночное число — диапазон нулевой длины
    if (isNumericTok(tok)) { a = b = tryFloat(tok, 0); return true; }
    return false;
}

inline int zLayerFromName(const std::string& z) {
    std::string s = lower(z);
    if (s == "t4") return 11;
    if (s == "t3") return 9;
    if (s == "t2") return 7;
    if (s == "t1") return 5;
    if (s == "b1") return 3;
    if (s == "b2") return 1;
    if (s == "b3") return -1;
    if (s == "b4") return -3;
    if (s == "b5") return -5;
    if (s == "0" || s == "default") return 0;
    return -999;
}

struct Emit {
    ParseResult& res;
    float        groundY;

    void push(const MObject& o) { res.objects.push_back(o); }
    void warn(const std::string& w) { res.warnings.push_back(w); }

    // Общие kv-модификаторы для любого объекта.
    void applyCommon(MObject& o, const Line& ln) {
        o.rot   = ln.fnum("rot", 0);
        o.scale = std::clamp(ln.fnum("scale", 1.f), 0.05f, 10.f);
        o.flipX = ln.flag("flipx");
        o.flipY = ln.flag("flipy");
        o.noTouch = ln.flag("notouch");
        o.passable = ln.flag("passable");
        o.noGlow = ln.flag("noglow");
        o.highDetail = ln.flag("hd");
        o.dontFade = ln.flag("dontfade");
        o.dontEnter = ln.flag("dontenter");
        o.multiActivate = ln.flag("multi");
        int zl = zLayerFromName(ln.str("z", ""));
        if (zl != -999) o.zLayer = zl;
        o.colorCh = std::clamp(ln.inum("c", 0), 0, 1010);
        if (ln.kv.count("zo")) { o.zOrder = ln.inum("zo", 0); o.hasZOrder = true; }
        std::string gs = ln.str("g", "");
        if (!gs.empty()) {
            std::string cur;
            for (char c : gs + ',') {
                if (c == ',') { if (!cur.empty()) o.groups.push_back(std::clamp(tryInt(cur, 0), 0, 9999)); cur.clear(); }
                else cur.push_back(c);
            }
        }
    }

    int resolve(const std::string& name, const Line& ln) {
        int id = makellm::idFor(name);
        if (id == 0) warn("unknown object '" + name + "' (line skipped part)");
        (void)ln;
        return id;
    }

    void object(const std::string& name, float x, float y, const Line& ln) {
        int id = resolve(name, ln);
        if (!id) return;
        MObject o;
        o.id = id; o.type = name; o.x = x; o.y = y;
        applyCommon(o, ln);
        push(o);
    }

    void rowOfBlocks(const std::string& name, float x0, float x1, float y, const Line& ln) {
        if (x1 < x0) std::swap(x0, x1);
        for (float x = x0; x <= x1 + 1.f; x += 30.f)
            object(name, x, y, ln);
    }
    void columnOfBlocks(const std::string& name, float x, float y0, float y1, const Line& ln) {
        if (y1 < y0) std::swap(y0, y1);
        for (float y = y0; y <= y1 + 1.f; y += 30.f)
            object(name, x, y, ln);
    }
};

// Имена вариантов по умолчанию.
inline const char* DEFAULT_BLOCK = "block_black_gradient_square";
inline const char* DEFAULT_SPIKE = "spike_black_gradient_spike";

inline const std::unordered_map<std::string, std::string>& orbNames() {
    static const std::unordered_map<std::string, std::string> m = {
        {"yellow", "jump_orb_yellow_jump_orb"},
        {"pink",   "jump_orb_pink_jump_orb"},
        {"red",    "jump_orb_red_jump_orb"},
    };
    return m;
}
inline const std::unordered_map<std::string, std::string>& padNames() {
    static const std::unordered_map<std::string, std::string> m = {
        {"yellow", "jump_pad_yellow_jump_pad"},
        {"pink",   "jump_pad_pink_jump_pad"},
        {"red",    "jump_pad_red_jump_pad"},
    };
    return m;
}
inline const std::unordered_map<std::string, std::string>& portalNames() {
    static const std::unordered_map<std::string, std::string> m = {
        {"cube", "portal_cube_portal"}, {"ship", "portal_ship_portal"},
        {"ball", "portal_ball_portal"}, {"ufo", "portal_ufo_portal"},
        {"wave", "portal_wave_portal"}, {"robot", "portal_robot_portal"},
        {"spider", "portal_spider_portal"}, {"swing", "portal_swing_portal"},
        {"slow", "portal_yellow_slow_speed_portal"},
        {"normal", "portal_blue_normal_speed_portal"},
        {"fast", "portal_green_fast_speed_portal"},
        {"faster", "portal_pink_fast_speed_portal"},
        {"fastest", "portal_red_fast_speed_portal"},
    };
    return m;
}
inline const std::unordered_map<std::string, std::string>& sawNames() {
    static const std::unordered_map<std::string, std::string> m = {
        {"small", "spike_small_black_sawblade"},
        {"medium", "spike_medium_black_sawblade"},
        {"large", "spike_large_black_sawblade"},
    };
    return m;
}

// ID триггеров (сырые, не из каталога имён).
namespace trigid {
    constexpr int COLOR = 899, MOVE = 901, PULSE = 1006, ALPHA = 1007,
                  TOGGLE = 1049, SPAWN = 1268, ROTATE = 1346, STOP = 1616;
}

inline void handleTrigger(Emit& em, const Line& ln) {
    // TRIG <SUB> ... — подвид берём из первого позиционного токена.
    if (ln.pos.empty()) { em.warn("TRIG without subtype"); return; }
    std::string sub = lower(ln.pos[0]);
    MObject o;
    o.x = ln.fnum("at", 0);
    o.y = ln.fnum("y", em.groundY + 150.f);   // триггеры парят над геймплеем
    o.type = "trig_" + sub;

    auto needGroup = [&](const char* key) -> int {
        int g = ln.inum(key, 0);
        if (g < 1 || g > 9999) { em.warn(std::string("TRIG ") + sub + ": bad/missing group"); return 0; }
        return g;
    };

    if (sub == "color") {
        o.id = trigid::COLOR;
        o.trig["channel"] = std::to_string(std::clamp(ln.inum("ch", 1), 1, 1010));
        o.trig["hex"] = ln.str("hex", "FFFFFF");
        o.trig["duration"] = std::to_string(std::clamp(ln.fnum("dur", 0.5f), 0.f, 30.f));
        if (ln.flag("blend")) o.trig["blending"] = "1";
        if (ln.kv.count("op")) o.trig["opacity"] = std::to_string(std::clamp(ln.fnum("op", 1.f), 0.f, 1.f));
    } else if (sub == "move") {
        int g = needGroup("g"); if (!g) return;
        o.id = trigid::MOVE;
        o.trig["target_group"] = std::to_string(g);
        o.trig["move_x"] = std::to_string(ln.fnum("dx", 0));
        o.trig["move_y"] = std::to_string(ln.fnum("dy", 0));
        o.trig["duration"] = std::to_string(std::clamp(ln.fnum("dur", 1.f), 0.f, 30.f));
        o.trig["easing"] = std::to_string(std::clamp(ln.inum("ease", 0), 0, 18));
        if (ln.flag("lockx")) o.trig["lock_to_player_x"] = "1";
        if (ln.flag("locky")) o.trig["lock_to_player_y"] = "1";
    } else if (sub == "pulse") {
        o.id = trigid::PULSE;
        o.trig["target_color_channel"] = std::to_string(std::clamp(ln.inum("ch", 1), 1, 1010));
        o.trig["hex"] = ln.str("hex", "FFFFFF");
        o.trig["fade_in"] = std::to_string(std::clamp(ln.fnum("fade", 0.1f), 0.f, 10.f));
        o.trig["hold"] = std::to_string(std::clamp(ln.fnum("hold", 0.2f), 0.f, 10.f));
        o.trig["fade_out"] = std::to_string(std::clamp(ln.fnum("fade", 0.3f), 0.f, 10.f));
    } else if (sub == "alpha") {
        int g = needGroup("g"); if (!g) return;
        o.id = trigid::ALPHA;
        o.trig["target_group"] = std::to_string(g);
        o.trig["opacity"] = std::to_string(std::clamp(ln.fnum("to", 1.f), 0.f, 1.f));
        o.trig["duration"] = std::to_string(std::clamp(ln.fnum("dur", 0.5f), 0.f, 30.f));
    } else if (sub == "rotate") {
        int g = needGroup("g"); if (!g) return;
        o.id = trigid::ROTATE;
        o.trig["target_group"] = std::to_string(g);
        o.trig["degrees"] = std::to_string(ln.fnum("deg", 90));
        o.trig["duration"] = std::to_string(std::clamp(ln.fnum("dur", 1.f), 0.f, 30.f));
        int cg = ln.inum("center", 0);
        if (cg >= 1 && cg <= 9999) o.trig["center_group"] = std::to_string(cg);
    } else if (sub == "spawn") {
        int g = needGroup("g"); if (!g) return;
        o.id = trigid::SPAWN;
        o.trig["target_group"] = std::to_string(g);
        o.trig["delay"] = std::to_string(std::clamp(ln.fnum("delay", 0.f), 0.f, 30.f));
    } else if (sub == "toggle") {
        int g = needGroup("g"); if (!g) return;
        o.id = trigid::TOGGLE;
        o.trig["target_group"] = std::to_string(g);
        o.trig["activate_group"] = ln.flag("on") ? "1" : "0";
    } else if (sub == "stop") {
        int g = needGroup("g"); if (!g) return;
        o.id = trigid::STOP;
        o.trig["target_group"] = std::to_string(g);
    } else {
        em.warn("unknown TRIG subtype '" + sub + "'");
        return;
    }
    em.applyCommon(o, ln);
    em.push(o);
}

// Главный вход: многострочный MLS-текст → список объектов.
inline ParseResult parse(const std::string& script, float groundY) {
    ParseResult res;
    Emit em{res, groundY};

    size_t start = 0;
    int lineNo = 0;
    auto process = [&](const std::string& rawLine) {
        ++lineNo;
        Line ln;
        if (!parseLine(rawLine, ln)) return;
        const std::string& v = ln.verb;
        size_t before = res.objects.size();

        if (v == "meta" || v == "section") return;   // информационные строки

        if (v == "block" || v == "decor" || v == "obj") {
            std::string name = ln.str("v", v == "block" ? DEFAULT_BLOCK : "");
            if (v == "obj" && !ln.pos.empty() && !isNumericTok(ln.pos[0])) name = ln.pos[0];
            if (v == "decor" && name.empty()) { res.warnings.push_back("DECOR without v=name"); return; }
            float x = ln.fnum("x", 1e9f), y = ln.fnum("y", 1e9f);
            if (x == 1e9f || y == 1e9f) {
                // позиционные: BLOCK 150 105 / DECOR decor_tall_rod 150 105
                std::vector<std::string> nums;
                for (auto& p : ln.pos) if (isNumericTok(p)) nums.push_back(p);
                if (nums.size() >= 2) { x = tryFloat(nums[nums.size()-2], 0); y = tryFloat(nums.back(), 0); }
                else { res.warnings.push_back("line " + std::to_string(lineNo) + ": " + v + " missing x/y"); return; }
            }
            em.object(name, x, y, ln);
        }
        else if (v == "spike") {
            std::string name = ln.str("v", DEFAULT_SPIKE);
            float x = ln.fnum("x", 1e9f);
            float y = ln.fnum("y", groundY + 15.f);   // шип стоит НА поверхности земли
            if (x == 1e9f) {
                if (!ln.pos.empty() && isNumericTok(ln.pos[0])) x = tryFloat(ln.pos[0], 0);
                else { res.warnings.push_back("line " + std::to_string(lineNo) + ": SPIKE missing x"); return; }
            }
            em.object(name, x, y, ln);
        }
        else if (v == "saw") {
            std::string sz = lower(ln.str("size", "medium"));
            auto& sm = sawNames();
            auto it = sm.count(sz) ? sm.find(sz) : sm.find("medium");
            float x = ln.fnum("x", 1e9f), y = ln.fnum("y", 1e9f);
            std::vector<std::string> nums;
            for (auto& p : ln.pos) if (isNumericTok(p)) nums.push_back(p);
            if (x == 1e9f && nums.size() >= 1) x = tryFloat(nums[0], 0);
            if (y == 1e9f && nums.size() >= 2) y = tryFloat(nums[1], 0);
            if (x == 1e9f || y == 1e9f) { res.warnings.push_back("line " + std::to_string(lineNo) + ": SAW missing x/y"); return; }
            em.object(it->second, x, y, ln);
        }
        else if (v == "orb" || v == "pad") {
            auto& table = v == "orb" ? orbNames() : padNames();
            std::string color;
            std::vector<std::string> nums;
            for (auto& p : ln.pos) {
                if (isNumericTok(p)) nums.push_back(p);
                else if (color.empty()) color = lower(p);
            }
            if (color.empty()) color = "yellow";
            auto it = table.find(color);
            if (it == table.end()) it = table.find("yellow");
            float x = ln.fnum("x", 1e9f), y = ln.fnum("y", 1e9f);
            if (x == 1e9f && nums.size() >= 1) x = tryFloat(nums[0], 0);
            if (y == 1e9f && nums.size() >= 2) y = tryFloat(nums[1], 0);
            if (y == 1e9f) y = groundY + 60.f;
            if (x == 1e9f) { res.warnings.push_back("line " + std::to_string(lineNo) + ": " + v + " missing x"); return; }
            em.object(it->second, x, y, ln);
        }
        else if (v == "portal") {
            std::string kind;
            std::vector<std::string> nums;
            for (auto& p : ln.pos) {
                if (isNumericTok(p)) nums.push_back(p);
                else if (kind.empty()) kind = lower(p);
            }
            auto& pm = portalNames();
            auto it = pm.find(kind);
            if (it == pm.end()) { res.warnings.push_back("line " + std::to_string(lineNo) + ": PORTAL unknown kind '" + kind + "'"); return; }
            float x = ln.fnum("x", 1e9f), y = ln.fnum("y", 1e9f);
            if (x == 1e9f && nums.size() >= 1) x = tryFloat(nums[0], 0);
            if (y == 1e9f && nums.size() >= 2) y = tryFloat(nums[1], 0);
            if (y == 1e9f) y = groundY + 60.f;
            if (x == 1e9f) { res.warnings.push_back("line " + std::to_string(lineNo) + ": PORTAL missing x"); return; }
            em.object(it->second, x, y, ln);
        }
        else if (v == "floor" || v == "ceil") {
            std::string range = !ln.pos.empty() ? ln.pos[0] : ln.str("x", "");
            float a, b;
            if (!parseRange(range, a, b)) { res.warnings.push_back("line " + std::to_string(lineNo) + ": " + v + " bad range"); return; }
            std::string name = ln.str("v", DEFAULT_BLOCK);
            float y = ln.fnum("y", v == "floor" ? groundY : groundY + 270.f);
            em.rowOfBlocks(name, a, b, y, ln);
        }
        else if (v == "pillar" || v == "wall") {
            float x = ln.fnum("x", 1e9f);
            std::string range;
            for (auto& p : ln.pos) {
                if (isNumericTok(p) && x == 1e9f) x = tryFloat(p, 0);
                else if (p.find("..") != std::string::npos || p.find(':') != std::string::npos) range = p;
            }
            if (range.empty()) range = ln.str("y", "");
            float a, b;
            if (x == 1e9f || !parseRange(range, a, b)) { res.warnings.push_back("line " + std::to_string(lineNo) + ": PILLAR bad args"); return; }
            std::string name = ln.str("v", DEFAULT_BLOCK);
            em.columnOfBlocks(name, x, a, b, ln);
        }
        else if (v == "spikes") {
            float x = ln.fnum("x", 1e9f);
            if (x == 1e9f && !ln.pos.empty() && isNumericTok(ln.pos[0])) x = tryFloat(ln.pos[0], 0);
            if (x == 1e9f) { res.warnings.push_back("line " + std::to_string(lineNo) + ": SPIKES missing x"); return; }
            int count = std::clamp(ln.inum("count", 3), 1, 50);
            float spacing = std::clamp(ln.fnum("spacing", 30.f), 15.f, 300.f);
            std::string name = ln.str("v", DEFAULT_SPIKE);
            float y = ln.fnum("y", groundY + 15.f);
            for (int i = 0; i < count; ++i) em.object(name, x + i * spacing, y, ln);
        }
        else if (v == "stairs-up" || v == "stairs-down") {
            float x = ln.fnum("x", 1e9f);
            if (x == 1e9f && !ln.pos.empty() && isNumericTok(ln.pos[0])) x = tryFloat(ln.pos[0], 0);
            int steps = std::clamp(ln.inum("steps", 4), 1, 30);
            float w = std::clamp(ln.fnum("w", 30.f), 15.f, 120.f);
            float h = std::clamp(ln.fnum("h", 30.f), 15.f, 120.f);
            std::string name = ln.str("v", DEFAULT_BLOCK);
            if (x == 1e9f) { res.warnings.push_back("line " + std::to_string(lineNo) + ": STAIRS missing x"); return; }
            if (v == "stairs-up") {
                for (int i = 0; i < steps; ++i)
                    em.columnOfBlocks(name, x + i * w, groundY, groundY + i * h, ln);
            } else {
                float top = ln.fnum("top", groundY + steps * h);
                for (int i = 0; i < steps; ++i)
                    em.columnOfBlocks(name, x + i * w, groundY, top - i * h, ln);
            }
        }
        else if (v == "run") {
            std::string range = !ln.pos.empty() ? ln.pos[0] : ln.str("x", "");
            float a, b;
            if (!parseRange(range, a, b)) { res.warnings.push_back("line " + std::to_string(lineNo) + ": RUN bad range"); return; }
            float y = ln.fnum("y", groundY);
            float gap = std::clamp(ln.fnum("gap", 0.f), 0.f, 300.f);
            int every = std::clamp(ln.inum("every", 4), 2, 50);
            std::string name = ln.str("v", DEFAULT_BLOCK);
            int idx = 0;
            for (float x = a; x <= b + 1.f; x += 30.f, ++idx) {
                if (gap > 0.f && (idx % every) == (every - 1)) continue;   // пропуск = дыра
                em.object(name, x, y, ln);
            }
        }
        else if (v == "corridor") {
            std::string range = !ln.pos.empty() ? ln.pos[0] : ln.str("x", "");
            float a, b;
            if (!parseRange(range, a, b)) { res.warnings.push_back("line " + std::to_string(lineNo) + ": CORRIDOR bad range"); return; }
            float floorY = ln.fnum("floor", groundY);
            float ceilY  = ln.fnum("ceil", groundY + 270.f);
            std::string name = ln.str("v", DEFAULT_BLOCK);
            em.rowOfBlocks(name, a, b, floorY, ln);
            em.rowOfBlocks(name, a, b, ceilY, ln);
        }
        else if (v == "trig") {
            handleTrigger(em, ln);
        }
        else {
            res.warnings.push_back("line " + std::to_string(lineNo) + ": unknown verb '" + v + "'");
        }

        (void)before;
    };

    // Разбиение по строкам вручную (без исключений, CRLF-толерантно).
    std::string text = script;
    while (start <= text.size()) {
        auto nl = text.find('\n', start);
        if (nl == std::string::npos) { process(text.substr(start)); break; }
        process(text.substr(start, nl - start));
        start = nl + 1;
    }

    res.badLines = (int)res.warnings.size();
    return res;
}

// Вытаскивает MLS из ответа модели: предпочтительно fenced-блок, иначе
// все строки, похожие на MLS (начинающиеся с известного глагола).
inline std::string extractScript(const std::string& response) {
    auto fence = response.find("```");
    if (fence != std::string::npos) {
        auto contentStart = response.find('\n', fence);
        auto fenceEnd = response.find("```", fence + 3);
        if (contentStart != std::string::npos && fenceEnd != std::string::npos && fenceEnd > contentStart)
            return response.substr(contentStart + 1, fenceEnd - contentStart - 1);
    }
    // Fallback: фильтруем по известным глаголам.
    static const char* verbs[] = {
        "meta","section","block","decor","obj","spike","saw","orb","pad","portal",
        "floor","ceil","pillar","wall","spikes","stairs-up","stairs-down","run","corridor","trig"
    };
    std::string out;
    size_t pos = 0;
    while (pos <= response.size()) {
        auto nl = response.find('\n', pos);
        std::string line = nl == std::string::npos ? response.substr(pos) : response.substr(pos, nl - pos);
        pos = nl == std::string::npos ? response.size() + 1 : nl + 1;
        std::string t = lower(trim(line));
        if (t.empty() || t[0] == '#') { out += line; out += '\n'; continue; }
        for (auto vb : verbs) {
            std::string vs(vb);
            if (t == vs || t.rfind(vs + " ", 0) == 0 || t.rfind(vs + "\t", 0) == 0) {
                out += line; out += '\n';
                break;
            }
        }
    }
    return out;
}

} // namespace mls
} // namespace mll
