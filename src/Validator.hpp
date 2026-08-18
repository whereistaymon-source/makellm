#pragma once
// ─── MakeLLM Validator ───────────────────────────────────────────────────────
// Физический бот, прогоняющий сгенерированный уровень ДО вставки в редактор.
//   1. ПРОХОДИМОСТЬ — жадный бот обязан доехать до конца.
//   2. NO-INPUT RUN — прогон «без нажатий» обязан УМЕРЕТЬ (иначе это
//      авто-уровень / secret way).
//   3. SKY-COVER — у полётных секций обязан быть потолок, иначе игрок
//      перелетает контент по верху.
// Нарушения возвращаются списком с координатами и скармливаются LLM
// в refinement-раунде.
// Константы физики — замеры сообщества GD: 1x = 311.58 u/s, прыжок куба
// v0≈603.7 u/s, g≈2794 u/s² (апекс ≈65 юнитов).

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "MLS.hpp"

namespace mll {
namespace validate {

struct Issue {
    float       x = 0, y = 0;
    std::string text;
    bool        fatal = true;
};

struct Report {
    std::vector<Issue> issues;
    float reachedX = 0;
    float levelEndX = 0;
    int   jumpCount = 0;
    bool  passed = false;

    bool ok() const {
        for (auto& i : issues) if (i.fatal) return false;
        return true;
    }
    std::string toPromptText(size_t maxIssues = 12) const {
        std::string out;
        size_t shown = 0;
        for (auto& i : issues) {
            if (!i.fatal) continue;
            if (shown >= maxIssues) {
                out += "... and " + std::to_string(issues.size() - shown) + " more\n";
                break;
            }
            out += "- [x=" + std::to_string((int)i.x) + "] " + i.text + "\n";
            ++shown;
        }
        return out;
    }
};

namespace detail {

constexpr float VX     = 311.58f;
constexpr float V_JUMP = 603.72f;
constexpr float GRAV   = 2794.11f;
constexpr float DT     = 1.f / 60.f;
constexpr float HALF   = 15.f;
constexpr float CEIL   = 540.f;

struct Box { float x, y, halfW, halfH; };

enum class Mode { Cube, Ship, Ball, Ufo, Wave, Robot, Spider, Swing };

inline bool isFlight(Mode m) {
    return m == Mode::Ship || m == Mode::Ufo || m == Mode::Wave ||
           m == Mode::Ball || m == Mode::Spider || m == Mode::Swing;
}
inline const char* modeName(Mode m) {
    switch (m) {
        case Mode::Ship: return "ship"; case Mode::Ufo: return "ufo";
        case Mode::Wave: return "wave"; case Mode::Ball: return "ball";
        case Mode::Spider: return "spider"; case Mode::Swing: return "swing";
        case Mode::Robot: return "robot"; default: return "cube";
    }
}

struct World {
    std::vector<Box> solids, hazards;
    std::vector<std::pair<float, Mode>>  modes;
    std::vector<std::pair<float, float>> speeds;
    struct Boost { float x, y, imp; bool pad; };
    std::vector<Boost> boosts;
    float maxX = 0;
    float floorTop = 0;
    bool  anyHazards = false;
};

inline bool isHazardType(const std::string& t) {
    return t.rfind("spike_", 0) == 0 || t.rfind("hazard_", 0) == 0 ||
           t.find("sawblade") != std::string::npos;
}
inline bool isSolidType(const std::string& t) {
    return t.rfind("block_", 0) == 0;
}

inline bool modeForPortal(const std::string& t, Mode& out) {
    if (t == "portal_cube_portal")   { out = Mode::Cube;   return true; }
    if (t == "portal_ship_portal")   { out = Mode::Ship;   return true; }
    if (t == "portal_ball_portal")   { out = Mode::Ball;   return true; }
    if (t == "portal_ufo_portal")    { out = Mode::Ufo;    return true; }
    if (t == "portal_wave_portal")   { out = Mode::Wave;   return true; }
    if (t == "portal_robot_portal")  { out = Mode::Robot;  return true; }
    if (t == "portal_spider_portal") { out = Mode::Spider; return true; }
    if (t == "portal_swing_portal")  { out = Mode::Swing;  return true; }
    return false;
}

inline bool speedForPortal(const std::string& t, float& out) {
    if (t == "portal_yellow_slow_speed_portal")   { out = 0.8061f; return true; }
    if (t == "portal_blue_normal_speed_portal")   { out = 1.0f;    return true; }
    if (t == "portal_green_fast_speed_portal")    { out = 1.2434f; return true; }
    if (t == "portal_pink_fast_speed_portal")     { out = 1.5020f; return true; }
    if (t == "portal_red_fast_speed_portal")      { out = 1.8486f; return true; }
    return false;
}

inline World buildWorld(const std::vector<MObject>& objs, float groundY) {
    World w;
    w.floorTop = groundY + 15.f;
    for (auto& o : objs) {
        if (!std::isfinite(o.x) || !std::isfinite(o.y)) continue;
        if (o.x < -500.f || o.x > 300000.f || o.y < -1000.f || o.y > 6000.f) continue;
        const std::string& t = o.type;
        if (t.empty()) continue;
        float sc = std::clamp(std::isfinite(o.scale) ? o.scale : 1.f, 0.05f, 50.f);

        Mode m; float sp;
        if (modeForPortal(t, m)) { w.modes.push_back({o.x, m}); continue; }
        if (speedForPortal(t, sp)) { w.speeds.push_back({o.x, sp}); continue; }

        if (t.rfind("jump_orb_", 0) == 0 || t.rfind("jump_pad_", 0) == 0) {
            float imp = t.find("red") != std::string::npos ? 1.35f
                      : t.find("pink") != std::string::npos ? 0.75f : 1.0f;
            w.boosts.push_back({o.x, o.y, imp, t.rfind("jump_pad_", 0) == 0});
            continue;
        }

        bool hz = isHazardType(t);
        bool so = !hz && isSolidType(t);
        if (!hz && !so) continue;
        if (hz && o.noTouch) continue;
        if (so && o.passable) continue;
        if (hz) w.anyHazards = true;
        float half = 15.f * sc * (hz ? 0.55f : 1.f);
        (hz ? w.hazards : w.solids).push_back({o.x, o.y, half, half});
        if (o.x > w.maxX) w.maxX = o.x;
    }
    auto byX = [](const Box& a, const Box& b) { return a.x < b.x; };
    std::sort(w.solids.begin(), w.solids.end(), byX);
    std::sort(w.hazards.begin(), w.hazards.end(), byX);
    std::sort(w.modes.begin(), w.modes.end(), [](const std::pair<float,Mode>& a, const std::pair<float,Mode>& b){ return a.first < b.first; });
    std::sort(w.speeds.begin(), w.speeds.end(), [](const std::pair<float,float>& a, const std::pair<float,float>& b){ return a.first < b.first; });
    std::sort(w.boosts.begin(), w.boosts.end(), [](const World::Boost& a, const World::Boost& b){ return a.x < b.x; });
    return w;
}

struct RunResult {
    bool  finished = false;
    float reachedX = 0;
    int   jumps = 0;
    std::vector<Issue> deaths;
};

// noInput=true: прыжки и орбы запрещены (пады — автоматические, как в игре).
inline RunResult run(const World& w, bool noInput) {
    RunResult rr;
    if (w.maxX <= 0.f) return rr;

    size_t sWin = 0, hWin = 0, mWin = 0, spWin = 0, bWin = 0;
    float x = 0.f, y = w.floorTop + HALF, vy = 0.f;
    bool grounded = true;
    Mode mode = Mode::Cube;
    float vxMult = 1.f;
    float lastPadX = -1e9f;

    auto surfaceAt = [&](float px, float py) {
        float best = w.floorTop;
        for (size_t i = sWin; i < w.solids.size() && w.solids[i].x - w.solids[i].halfW <= px + HALF; ++i) {
            const Box& b = w.solids[i];
            if (px + HALF < b.x - b.halfW || px - HALF > b.x + b.halfW) continue;
            float top = b.y + b.halfH;
            if (top <= py - HALF + 6.f && top > best) best = top;
        }
        return best;
    };

    for (int tick = 0; tick < 20000; ++tick) {
        float stepMult = vxMult;
        while (mWin < w.modes.size() && w.modes[mWin].first <= x) {
            Mode prev = mode;
            mode = w.modes[mWin++].second;
            if (!detail::isFlight(mode)) {
                vy = 0.f;
                if (detail::isFlight(prev)) { y = surfaceAt(x, y) + HALF; grounded = true; }
                else grounded = false;
            }
        }
        while (spWin < w.speeds.size() && w.speeds[spWin].first <= x)
            vxMult = w.speeds[spWin++].second;
        while (bWin < w.boosts.size() && w.boosts[bWin].x < x - 100.f) ++bWin;

        x += VX * stepMult * DT;
        if (x > w.maxX + 200.f) { rr.finished = true; break; }
        while (sWin < w.solids.size()  && w.solids[sWin].x  + w.solids[sWin].halfW  < x - 200.f) ++sWin;
        while (hWin < w.hazards.size() && w.hazards[hWin].x + w.hazards[hWin].halfW < x - 200.f) ++hWin;

        if (detail::isFlight(mode)) {
            std::vector<std::pair<float,float>> occ;
            for (size_t i = sWin; i < w.solids.size() && w.solids[i].x - w.solids[i].halfW <= x + HALF; ++i) {
                const Box& b = w.solids[i];
                if (x + HALF < b.x - b.halfW || x - HALF > b.x + b.halfW) continue;
                occ.push_back({b.y - b.halfH, b.y + b.halfH});
            }
            for (size_t i = hWin; i < w.hazards.size() && w.hazards[i].x - w.hazards[i].halfW <= x + HALF; ++i) {
                const Box& h = w.hazards[i];
                if (x + HALF < h.x - h.halfW || x - HALF > h.x + h.halfW) continue;
                occ.push_back({h.y - h.halfH, h.y + h.halfH});
            }
            std::sort(occ.begin(), occ.end());
            float lo = w.floorTop, bestGap = 0.f, bestLo = w.floorTop;
            for (auto& iv : occ) {
                if (iv.first > lo) {
                    float gap = std::min(iv.first, CEIL) - lo;
                    if (gap > bestGap) { bestGap = gap; bestLo = lo; }
                }
                lo = std::max(lo, iv.second);
                if (lo >= CEIL) break;
            }
            if (CEIL > lo && CEIL - lo > bestGap) { bestGap = CEIL - lo; bestLo = lo; }

            if (noInput) {
                y = w.floorTop + HALF;
                if (bestGap < 40.f && bestLo <= w.floorTop + 1.f) {
                    rr.deaths.push_back({x, y, "no-input: floor of flight corridor is blocked"});
                    if (rr.deaths.size() >= 8) break;
                    x += 60.f;
                }
            } else {
                if (bestGap < 40.f) {
                    rr.deaths.push_back({x, y, std::string("corridor too tight for ") + modeName(mode) +
                        " (" + std::to_string((int)bestGap) + "u free)"});
                    if (rr.deaths.size() >= 8) break;
                    x += 60.f;
                    while (sWin < w.solids.size()  && w.solids[sWin].x  + w.solids[sWin].halfW  < x - 200.f) ++sWin;
                    while (hWin < w.hazards.size() && w.hazards[hWin].x + w.hazards[hWin].halfW < x - 200.f) ++hWin;
                } else {
                    y = bestLo + bestGap * 0.5f;
                }
            }
            rr.reachedX = x;
            continue;
        }

        // ── Куб/робот ──
        for (size_t i = bWin; i < w.boosts.size() && w.boosts[i].x <= x + 20.f; ++i) {
            const World::Boost& bo = w.boosts[i];
            if (bo.pad && bo.x != lastPadX && std::abs(bo.x - x) < 20.f && std::abs(bo.y - y) < 30.f) {
                vy = V_JUMP * bo.imp * 1.15f;
                grounded = false;
                lastPadX = bo.x;
                break;
            }
        }
        if (grounded && !noInput) {
            bool wantJump = false;
            for (size_t i = hWin; i < w.hazards.size() && w.hazards[i].x < x + 95.f; ++i) {
                const Box& h = w.hazards[i];
                if (h.x > x + 20.f && h.y - h.halfH < y + 25.f && h.y + h.halfH > y - HALF - 25.f) {
                    wantJump = true; break;
                }
            }
            if (!wantJump) {
                for (size_t i = sWin; i < w.solids.size() && w.solids[i].x < x + 55.f; ++i) {
                    const Box& b = w.solids[i];
                    if (b.x > x + HALF && b.y + b.halfH > y - HALF + 6.f && b.y - b.halfH < y + HALF) {
                        wantJump = true; break;
                    }
                }
            }
            if (wantJump) { vy = V_JUMP; grounded = false; ++rr.jumps; }
        }
        if (!grounded) {
            vy -= GRAV * DT;
            y += vy * DT;
            float surf = surfaceAt(x, y);
            if (!noInput && vy < 0.f) {
                for (size_t i = bWin; i < w.boosts.size() && w.boosts[i].x <= x + 30.f; ++i) {
                    const World::Boost& bo = w.boosts[i];
                    if (bo.pad) continue;
                    if (std::abs(bo.x - x) < 28.f && std::abs(bo.y - y) < 45.f) {
                        bool danger = surf <= w.floorTop + 1.f;
                        for (size_t k = hWin; !danger && k < w.hazards.size() && w.hazards[k].x < x + 100.f; ++k)
                            if (w.hazards[k].x > x - 20.f && std::abs(w.hazards[k].y - y) < 90.f) danger = true;
                        if (danger) { vy = V_JUMP * bo.imp; break; }
                    }
                }
            }
            if (vy <= 0.f && y - HALF <= surf) {
                y = surf + HALF; vy = 0.f; grounded = true;
                lastPadX = -1e9f;
            }
        } else {
            float surf = surfaceAt(x, y);
            if (surf < y - HALF - 1.f) { grounded = false; vy = 0.f; }
            else y = surf + HALF;
        }

        for (size_t i = sWin; i < w.solids.size() && w.solids[i].x - w.solids[i].halfW <= x + HALF; ++i) {
            const Box& b = w.solids[i];
            if (x + HALF > b.x - b.halfW && x - HALF < b.x + b.halfW &&
                y + HALF - 6.f > b.y - b.halfH && y - HALF + 6.f < b.y + b.halfH) {
                rr.deaths.push_back({x, y, std::string(noInput ? "no-input: " : "") + "ran into a wall"});
                goto died;
            }
        }
        for (size_t i = hWin; i < w.hazards.size() && w.hazards[i].x - w.hazards[i].halfW <= x + HALF; ++i) {
            const Box& h = w.hazards[i];
            if (x + HALF * 0.7f > h.x - h.halfW && x - HALF * 0.7f < h.x + h.halfW &&
                y + HALF * 0.7f > h.y - h.halfH && y - HALF * 0.7f < h.y + h.halfH) {
                rr.deaths.push_back({x, y, std::string(noInput ? "no-input: " : "") + "hit a hazard"});
                goto died;
            }
        }
        goto survived;
    died:
        if (rr.deaths.size() >= 8) break;
        x += 60.f;
        y = surfaceAt(x, y) + HALF;
        vy = 0.f; grounded = true;
        while (sWin < w.solids.size()  && w.solids[sWin].x  + w.solids[sWin].halfW  < x - 200.f) ++sWin;
        while (hWin < w.hazards.size() && w.hazards[hWin].x + w.hazards[hWin].halfW < x - 200.f) ++hWin;
    survived:
        rr.reachedX = x;
    }
    return rr;
}

// Доля колонн полётной секции без какой-либо массы выше floorTop+150 —
// там игрок может лететь над контентом.
inline float openSkyRatio(const World& w, float x0, float x1) {
    if (x1 <= x0) return 0.f;
    int open = 0, total = 0;
    for (float x = x0; x <= x1; x += 15.f) {
        bool blockedAbove = false;
        for (auto& b : w.solids) {
            if (b.x + b.halfW < x - HALF || b.x - b.halfW > x + HALF) continue;
            if (b.y - b.halfH > w.floorTop + 150.f) { blockedAbove = true; break; }
        }
        ++total;
        if (!blockedAbove) ++open;
    }
    return total ? (float)open / (float)total : 0.f;
}

} // namespace detail

inline Report check(const std::vector<MObject>& objs, float groundY,
                    bool forbidSecretWay) {
    Report rep;
    auto w = detail::buildWorld(objs, groundY);
    rep.levelEndX = w.maxX;

    if (w.maxX < 300.f) {
        rep.issues.push_back({0, 0, "level is nearly empty (end X < 300) — build a real level"});
        return rep;
    }

    // 1. Проходимость.
    auto main = detail::run(w, false);
    rep.reachedX = main.reachedX;
    rep.jumpCount = main.jumps;
    rep.passed = main.finished;
    for (auto& d : main.deaths) rep.issues.push_back(d);
    if (!main.finished)
        rep.issues.push_back({main.reachedX, 0, "bot could not reach the end of the level"});

    // 2. Secret-way: no-input прогон обязан умереть.
    if (forbidSecretWay && w.anyHazards) {
        bool startsAsCube = w.modes.empty() || w.modes.front().first > 60.f;
        if (startsAsCube) {
            auto idle = detail::run(w, true);
            // Secret way = no-input прогон доезжает до конца БЕЗ единой смерти.
            if (idle.finished && idle.deaths.empty())
                rep.issues.push_back({0, 0,
                    "SECRET WAY: the level completes itself with zero player input — "
                    "add hazards on the main path so doing nothing is lethal"});
        }
    }

    // 3. Sky-cover для полётных секций.
    if (forbidSecretWay) {
        for (size_t i = 0; i < w.modes.size(); ++i) {
            if (!detail::isFlight(w.modes[i].second)) continue;
            float x0 = w.modes[i].first;
            float x1 = (i + 1 < w.modes.size()) ? w.modes[i + 1].first : w.maxX;
            if (x1 - x0 < 400.f) continue;
            float ratio = detail::openSkyRatio(w, x0, x1);
            if (ratio > 0.85f)
                rep.issues.push_back({x0, 0,
                    std::string("SECRET WAY RISK: ") + detail::modeName(w.modes[i].second) +
                    " section has no ceiling — the player can fly over everything. "
                    "Add a CORRIDOR or ceiling blocks"});
        }
    }

    // 4. Мягкое предупреждение о скучном уровне.
    if (w.anyHazards && main.finished && main.jumps == 0)
        rep.issues.push_back({0, 0, "level never requires a single jump — add gameplay", false});

    return rep;
}

} // namespace validate
} // namespace mll
