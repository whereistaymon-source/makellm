// Тестовый драйвер для MLS-парсера и валидатора (без Geode).
#include <cstdio>
#include "../src/MLS.hpp"
#include "../src/Validator.hpp"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  OK   %s\n", msg); } \
    else { printf("  FAIL %s\n", msg); ++failures; } \
} while (0)

static void dumpIssues(const mll::validate::Report& rep) {
    for (auto& i : rep.issues)
        printf("    issue%s: [x=%.0f y=%.0f] %s\n", i.fatal ? "(FATAL)" : "(warn)", i.x, i.y, i.text.c_str());
}

int main() {
    const float G = 105.f;

    // ── 1. Базовый парсинг ──
    {
        auto r = mll::mls::parse(
            "META name=Test\n"
            "FLOOR 0..900\n"                              // 31 блок
            "SPIKE 300\n"                                 // 1
            "SPIKES 450 count=3\n"                        // 3
            "BLOCK 600 165 v=block_brick_square rot=90 scale=1.5 g=7\n"  // 1
            "ORB yellow 700 165\n"                        // 1
            "PORTAL ship 800\n"                           // 1
            "CORRIDOR 810..1500 floor=105 ceil=375\n"     // 24+24 = 48
            "PORTAL cube 1520\n"                          // 1
            "TRIG COLOR ch=1 hex=FF0000 at=100 dur=1\n"   // 1
            "TRIG MOVE g=7 at=200 dx=0 dy=60 dur=2\n", G);// 1  => 89
        printf("-- test 1: basic parse --\n");
        printf("  objects: %zu, warnings: %zu\n", r.objects.size(), r.warnings.size());
        CHECK(r.objects.size() == 89, "object count");
        bool sawTrig = false, sawMove = false;
        for (auto& o : r.objects) {
            if (o.id == 899) { sawTrig = true; CHECK(o.trig.at("channel") == "1", "color ch"); }
            if (o.id == 901) { sawMove = true; }
        }
        CHECK(sawTrig, "color trigger parsed");
        CHECK(sawMove, "move trigger parsed");
        bool foundGroupObj = false;
        for (auto& o : r.objects) if (!o.groups.empty() && o.groups[0] == 7) foundGroupObj = true;
        CHECK(foundGroupObj, "group assignment g=7");
    }

    // ── 2. Толерантность к мусору ──
    {
        auto r = mll::mls::parse(
            "```mls\n"
            "FLOOR 0..300\n"
            "SPIKE\n"                       // нет x — warning
            "FOOBAR 1 2 3\n"               // неизвестный глагол
            "SPIKE +150.0 y=105\n", G);
        printf("-- test 2: garbage tolerance --\n");
        CHECK(r.objects.size() == 11 + 1, "good lines survived");
        CHECK(r.warnings.size() == 2, "two warnings collected");
    }

    // ── 3. Проходимый уровень → бот доезжает без замечаний ──
    {
        auto r = mll::mls::parse(
            "FLOOR 0..3000\n"
            "SPIKE 600\n"
            "SPIKE 900\n"
            "SPIKE 1400\n", G);
        auto rep = mll::validate::check(r.objects, G, true);
        printf("-- test 3: passable level --\n");
        printf("  passed=%d reached=%.0f jumps=%d issues=%zu\n",
               (int)rep.passed, rep.reachedX, rep.jumpCount, rep.issues.size());
        dumpIssues(rep);
        CHECK(rep.passed, "greedy bot reaches the end");
        CHECK(rep.jumpCount >= 3, "bot actually jumped over spikes");
        CHECK(rep.ok(), "no fatal issues");
    }

    // ── 4. Secret-way: угроза есть, но висит высоко над трассой ──
    {
        auto r = mll::mls::parse("FLOOR 0..3000\nSPIKE 600 y=405\n", G);
        auto rep = mll::validate::check(r.objects, G, true);
        printf("-- test 4: secret way detection --\n");
        dumpIssues(rep);
        bool secretFound = false;
        for (auto& i : rep.issues)
            if (i.text.find("SECRET WAY") != std::string::npos) secretFound = true;
        CHECK(secretFound, "no-input run flagged as secret way");
    }

    // ── 5. Стена на пути → бот умирает ──
    {
        auto r = mll::mls::parse(
            "FLOOR 0..3000\n"
            "PILLAR 800 135..405\n", G);
        auto rep = mll::validate::check(r.objects, G, true);
        printf("-- test 5: unpassable wall --\n");
        printf("  passed=%d issues=%zu\n", (int)rep.passed, rep.issues.size());
        dumpIssues(rep);
        CHECK(!rep.ok(), "impossible wall flagged");
    }

    // ── 6. Полётная секция без потолка → sky-cover warning ──
    {
        auto r = mll::mls::parse(
            "FLOOR 0..3000\n"
            "PORTAL ship 500\n"
            "FLOOR 510..2000\n"
            "PORTAL cube 2100\n"
            "SPIKE 2400\n", G);
        auto rep = mll::validate::check(r.objects, G, true);
        printf("-- test 6: sky cover --\n");
        dumpIssues(rep);
        bool skyFound = false;
        for (auto& i : rep.issues)
            if (i.text.find("ceiling") != std::string::npos) skyFound = true;
        CHECK(skyFound, "ceiling-less ship section flagged");
    }

    // ── 7. Ship-коридор проходится ──
    {
        auto r = mll::mls::parse(
            "FLOOR 0..3000\n"
            "PORTAL ship 500\n"
            "CORRIDOR 510..2000 floor=105 ceil=375\n"
            "PORTAL cube 2100\n"
            "SPIKE 2400\n", G);
        auto rep = mll::validate::check(r.objects, G, true);
        printf("-- test 7: flyable corridor --\n");
        printf("  passed=%d issues=%zu\n", (int)rep.passed, rep.issues.size());
        dumpIssues(rep);
        CHECK(rep.passed, "corridor is flyable");
        CHECK(rep.ok(), "no fatal issues in corridor level");
    }

    // ── 8. Извлечение из markdown-ответа ──
    {
        std::string resp =
            "Sure! Here's your level:\n\n"
            "```mls\nFLOOR 0..600\nSPIKE 300\n```\n\n"
            "I made it extra spicy!";
        auto script = mll::mls::extractScript(resp);
        auto r = mll::mls::parse(script, G);
        printf("-- test 8: markdown extraction --\n");
        CHECK(r.objects.size() == 22, "fenced block extracted and parsed"); // 21 блок + 1 шип
    }

    // ── 9. Дыра в полу НЕ смертельна — дефолтная земля GD твёрдая ──
    {
        auto r = mll::mls::parse(
            "FLOOR 0..600\n"
            "FLOOR 900..3000\n", G);   // пропасть в блоках, но земля под ней
        auto rep = mll::validate::check(r.objects, G, true);
        printf("-- test 9: pit rides on default ground --\n");
        printf("  passed=%d issues=%zu\n", (int)rep.passed, rep.issues.size());
        dumpIssues(rep);
        CHECK(rep.passed, "bot crosses block-gap on the default ground");
    }

    printf("\n%s (%d failures)\n", failures ? "FAILURES PRESENT" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
