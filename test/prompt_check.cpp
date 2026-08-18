// Компиляционная проверка Prompt.hpp (чистый C++).
#include "../src/Prompt.hpp"
#include <cstdio>
int main() {
    mll::prompt::Settings s;
    auto seed = mll::prompt::rollSeed();
    auto p = mll::prompt::buildSystemPrompt(s, seed);
    printf("system prompt: %zu bytes\n", p.size());
    printf("user prompt: %zu bytes\n", mll::prompt::buildUserPrompt("тест", s).size());
    printf("refine prompt: %zu bytes\n", mll::prompt::buildRefinementPrompt("- x\n").size());
    auto seed2 = mll::prompt::rollSeed();
    printf("unique seeds: %d\n", (int)(seed.nonce != seed2.nonce || seed.theme != seed2.theme));
    if (p.find("mls") == std::string::npos || p.find("FLOOR") == std::string::npos) {
        printf("FAIL: grammar missing\n");
        return 1;
    }
    if (p.find("SECRET WAY") == std::string::npos) {
        printf("FAIL: anti-secret-way contract missing\n");
        return 1;
    }
    printf("PROMPT CHECK OK\n");
    return 0;
}
