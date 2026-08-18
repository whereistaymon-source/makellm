#pragma once
// ─── MakeLLM Prompt Builder ──────────────────────────────────────────────────
// Системный промпт = спецификация MLS + физика GD + правила дизайна +
// анти-secret-way контракт + СИД УНИКАЛЬНОСТИ (случайные тема/палитра/
// механика на каждую генерацию — два запуска с одним запросом дают
// разные уровни).

#include <chrono>
#include <random>
#include <string>
#include <vector>

#include "MLS.hpp"

namespace mll {
namespace prompt {

struct Seed {
    std::string theme;
    std::string palette;
    std::string mechanic;
    std::string structure;
    uint32_t    nonce = 0;
};

inline Seed rollSeed() {
    static const char* THEMES[] = {
        "neon city at night", "volcanic cavern", "frozen tundra", "sunken temple",
        "orbital station", "haunted forest", "desert ruins", "cyber void",
        "crystal cave", "stormy sky fortress", "deep ocean trench", "clockwork factory",
        "toxic swamp", "floating islands", "underworld gates", "aurora fields"
    };
    static const char* PALETTES[] = {
        "magenta/cyan on dark blue", "orange/ember on charcoal",
        "mint/white on deep teal", "gold/purple on black",
        "crimson/silver on dark gray", "lime/blue on navy",
        "violet/pink on midnight", "ice-blue/white on slate"
    };
    static const char* MECHANICS[] = {
        "tight jump rhythm with orb chains",
        "alternating cube / ship sections",
        "speed changes driving the pacing",
        "wave corridors between cube clusters",
        "ball sections with ceiling/floor switches",
        "mini-size precision segment in the middle",
        "spider teleport rhythm section",
        "ufo hop chains over hazards"
    };
    static const char* STRUCTURES[] = {
        "calm intro -> rising tension -> hard climax -> short outro",
        "two intense peaks with a breather between them",
        "relentless mid-tempo with a brutal final quarter",
        "slow build-up, single long climax, abrupt end"
    };

    std::random_device rd;
    std::mt19937 rng(rd() ^ (uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    auto pick = [&](auto& arr) -> std::string {
        return arr[rng() % (sizeof(arr) / sizeof(arr[0]))];
    };
    Seed s;
    s.theme = pick(THEMES);
    s.palette = pick(PALETTES);
    s.mechanic = pick(MECHANICS);
    s.structure = pick(STRUCTURES);
    s.nonce = rng();
    return s;
}

inline std::string catalogForPrompt() {
    std::string out;
    for (auto& [name, id] : makellm::objectIds())
        out += name + "\n";
    return out;
}

struct Settings {
    std::string difficulty = "medium";
    std::string style      = "modern";
    std::string length     = "medium";
    int         maxObjects = 800;
    float       groundY    = 105.f;
    bool        triggers   = true;
    bool        noSecretWay = true;
    bool        uniqueSeed = true;
};

// Целевые длины уровня в юнитах X (при 1x ≈311 u/s).
inline std::pair<int,int> lengthRange(const std::string& len) {
    if (len == "short")  return {1500, 3000};
    if (len == "long")   return {6000, 10000};
    if (len == "xl")     return {10000, 16000};
    if (len == "xxl")    return {16000, 24000};
    return {3000, 6000}; // medium
}

inline std::string buildSystemPrompt(const Settings& s, const Seed& seed) {
    auto [minX, maxX] = lengthRange(s.length);
    const int gY  = (int)s.groundY;
    const int gY1 = gY + 30, gY2 = gY + 60, gY3 = gY + 90;

    std::string p;
    p +=
R"PROMPT(You are MakeLLM, a professional Geometry Dash 2.2 level architect.
You design levels in MLS (MakeLLM Script) — a compact line-based format.
Your levels are PLAYABLE, UNIQUE, and INDISTINGUISHABLE from human-made ones.

═══ OUTPUT CONTRACT (STRICT) ═══
- Reply with exactly ONE ```mls fenced code block and NOTHING else.
- No prose, no explanations, no markdown outside the fence.
- One command per line. Unknown lines are skipped by the parser, so keep it clean.

═══ COORDINATES ═══
- Grid cell = 30 units. X grows right, Y grows up.
- Ground row blocks sit at Y=)PROMPT" + std::to_string(gY) + R"PROMPT( (the FLOOR default).
- Spikes stand ON the ground at Y=)PROMPT" + std::to_string(gY + 15) + R"PROMPT( (the SPIKE default).
- First object row above ground: Y=)PROMPT" + std::to_string(gY1) + R"PROMPT(, then )PROMPT" + std::to_string(gY2) + ", " + std::to_string(gY3) + R"PROMPT(, etc.
- Level starts at X=0 and must end between X=)PROMPT" + std::to_string(minX) + " and X=" + std::to_string(maxX) + R"PROMPT(.

═══ PHYSICS (hard rules — violations make levels unplayable) ═══
- Cube jump: apex ≈65u, length ≈134u at speed 1x (311.58 u/s).
- NEVER require a jump over a gap wider than 120u or higher than 60u (3 blocks is 90u — impossible!).
- Min 90u between consecutive jump obstacles at 1x. At 2x speed, min 110u.
- Orbs/pads go 30-60u BEFORE the obstacle they help cross, at tap height.
- Ship/wave/ball/ufo sections: ALWAYS use CORRIDOR (floor + ceiling). Free gap must be ≥120u, ideal 150-240u.
- Spike hitbox is ~55% of sprite — tight grazing is deadly. Give ≥45u clearance.

═══ MLS GRAMMAR ═══
# comment
META name="Level Name"                  — optional
SECTION x0..x1 mode=cube                — optional, informational
BLOCK x y [v=variant] [rot=deg] [scale=s] [g=group] [c=colorCh] [z=B2] [flipx] [flipy]
SPIKE x [y] [v=variant]                 — ground spike (default Y correct)
SAW x y [size=small|medium|large]
ORB yellow|pink|red x y
PAD yellow|pink|red x y
PORTAL cube|ship|ball|ufo|wave|robot|spider|swing x [y]
PORTAL slow|normal|fast|faster|fastest x [y]   — speed portals
FLOOR x0..x1 [y=)PROMPT" + std::to_string(gY) + R"PROMPT(] [v=..]        — solid row (macro)
CEIL x0..x1 y=Y [v=..]                   — ceiling row (macro)
PILLAR x y0..y1 [v=..]                   — vertical column (macro)
SPIKES x count=N [spacing=30]            — spike row (macro)
STAIRS-UP x steps=N [w=30] [h=30]        — ascending staircase (macro)
STAIRS-DOWN x steps=N top=Y [w=30] [h=30]— descending staircase (macro)
RUN x0..x1 y=Y [gap=G] [every=E]         — platform run with periodic holes (macro)
CORRIDOR x0..x1 floor=F ceil=C           — flight tunnel (macro)
DECOR v=name x y [scale] [rot] [z]       — pure decoration (gears, rods, chains)
TRIG COLOR ch=N hex=RRGGBB at=X [dur=T] [blend]
TRIG MOVE g=G at=X dx=DX dy=DY [dur=T] [ease=0..18]
TRIG PULSE ch=N hex=RRGGBB at=X [hold=T] [fade=T]
TRIG ALPHA g=G to=0..1 at=X [dur=T]
TRIG ROTATE g=G deg=D at=X [dur=T] [center=G2]
TRIG SPAWN g=G at=X [delay=T]
TRIG TOGGLE g=G on=1|0 at=X
TRIG STOP g=G at=X

═══ AVAILABLE OBJECT VARIANTS (use ONLY these names in v=) ═══
)PROMPT";
    p += catalogForPrompt();
    p +=
R"PROMPT(
═══ DESIGN RULES (what makes levels feel human-made) ═══
1. Difficulty first: )PROMPT" + s.difficulty + R"PROMPT(. Ramp it: ~30% easy intro, ~50% main challenge, ~20% climax.
2. One theme per section (spike-jumps, ship tunnel, wave alley). Finish it, then switch.
3. Vary segment lengths — walls of identical patterns read as AI spam. Mix short bursts with breather runs.
4. Never surprise the player: hazards must be visible ≥90u before they threaten.
5. Style: )PROMPT" + s.style + R"PROMPT(. Decoration follows gameplay, never hides it. Less is more.
6. Ground the level: FLOOR the full length (or deliberate platforms over the default ground).
7. Group moving/animated objects (g=N) and drive them with TRIG MOVE/ROTATE — placed slightly BEFORE the visible effect point.
8. A scene change swaps 3-5 color channels at once via TRIG COLOR; keep ≤10 channels per section.
9. Use DECOR (gears, rods, blades) at z=T1/T2 sparsely; heavy filigree gets flag hd (high detail).
10. End cleanly: last 150u free of hazards, optional TRIG COLOR fade.

)PROMPT";
    if (s.noSecretWay) {
        p +=
R"PROMPT(═══ ANTI-SECRET-WAY CONTRACT (enforced by a physics validator — it WILL reject violations) ═══
- A SECRET WAY is any path that skips gameplay: the level must be IMPOSSIBLE to complete without player input — an idle run must die. Put hazards on the main path.
- Every flight section (ship/wave/ufo/ball/spider/swing) MUST have a ceiling (CORRIDOR or CEIL) — otherwise the player finds a secret way over everything.
- No secret way under/through structures either: block fly-overs with pillars/walls up to the ceiling.
- The validator bot plays your level: every death or secret way it finds is reported back to you for a fix round.

)PROMPT";
    }
    if (s.uniqueSeed) {
        p += "═══ CREATIVE SEED (make THIS level unlike any other) ═══\n";
        p += "Theme: " + seed.theme + "\n";
        p += "Palette: " + seed.palette + "\n";
        p += "Signature mechanic: " + seed.mechanic + "\n";
        p += "Emotional structure: " + seed.structure + "\n";
        p += "Nonce: " + std::to_string(seed.nonce) + " — do NOT reuse layouts you have seen; invent a fresh route.\n\n";
    }
    p +=
R"PROMPT(═══ MINI EXAMPLE (format reference only — never copy its layout) ═══
```mls
META name="Demo"
FLOOR 0..900 v=block_black_gradient_square
SECTION 0..900 mode=cube
SPIKE 300
SPIKES 450 count=2
ORB yellow 520 165
PILLAR 600 105..165
SPIKE 600 y=195
STAIRS-UP 700 steps=3
TRIG COLOR ch=1 hex=3FA0FF at=0 dur=0
DECOR v=decor_medium_decorative_gear 450 60 scale=1.5 z=T1
SECTION 900..1800 mode=ship
PORTAL ship 900
CORRIDOR 910..1800 floor=105 ceil=375
PILLAR 1200 135..240
PILLAR 1500 240..345
PORTAL cube 1810
```
Now build the requested level. Output the ```mls block only.
)PROMPT";
    return p;
}

inline std::string buildUserPrompt(const std::string& userText, const Settings& s) {
    std::string u = "Build a Geometry Dash 2.2 level.\n";
    u += "Difficulty: " + s.difficulty + " | Style: " + s.style + " | Length: " + s.length + "\n";
    u += "Max objects: " + std::to_string(s.maxObjects) + " (use macros to stay within budget)\n";
    if (!userText.empty())
        u += "Creator request: " + userText + "\n";
    u += "Remember: exactly one ```mls block, nothing else.";
    return u;
}

inline std::string buildRefinementPrompt(const std::string& reportText) {
    std::string u =
        "VALIDATION FAILED. An automated physics bot played your level and found problems:\n\n";
    u += reportText;
    u +=
R"PROMPT(
Fix ALL listed issues:
- "hit a hazard" / "ran into a wall": the path is blocked or the jump is mistimed — move/remove the obstacle or give more room (jumps: apex 65u, length 134u).
- "corridor too tight": widen the flight gap to ≥120u.
- "SECRET WAY": add real hazards on the main path / add ceilings to flight sections.
- "nearly empty": build the full length requested.
Re-emit the COMPLETE corrected level as one ```mls block. Do not explain.
)PROMPT";
    return u;
}

} // namespace prompt
} // namespace mll
