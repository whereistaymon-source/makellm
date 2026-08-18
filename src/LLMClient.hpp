#pragma once
// ─── MakeLLM LLM Client ──────────────────────────────────────────────────────
// Минимальный OpenAI-compatible клиент: POST {baseUrl}/chat/completions.
// Работает с Omniroute, Ollama (/v1), LM Studio, llama.cpp, OpenAI,
// OpenRouter, DeepSeek, Groq — всем, что говорит на OpenAI-протоколе.
// Сетевые вызовы — через geode::utils::web (асинхронно, не блокируя игру).

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <matjson.hpp>
#include <chrono>
#include <string>

namespace mll {

struct ChatMessage {
    std::string role;
    std::string content;
};

struct LLMResult {
    bool        ok = false;
    std::string text;        // контент ответа либо текст ошибки
    int         httpCode = 0;
    bool        transient = false; // 429/5xx — можно ретраить
};

class LLMClient {
public:
    std::string baseUrl;     // напр. http://localhost:20128/v1
    std::string apiKey;      // может быть пустым (локальные серверы)
    std::string model;
    float       temperature = 0.9f;
    int         timeoutSec  = 300;

    std::string chatUrl() const {
        std::string base = baseUrl;
        while (!base.empty() && base.back() == '/') base.pop_back();
        return base + "/chat/completions";
    }
    std::string modelsUrl() const {
        std::string base = baseUrl;
        while (!base.empty() && base.back() == '/') base.pop_back();
        return base + "/models";
    }

    // Собрать JSON тела запроса.
    std::string buildBody(const std::vector<ChatMessage>& messages) const {
        matjson::Value body = matjson::Value::object();
        body["model"] = model;
        body["temperature"] = temperature;
        matjson::Value msgs = matjson::Value::array();
        for (auto& m : messages) {
            matjson::Value msg = matjson::Value::object();
            msg["role"] = m.role;
            msg["content"] = m.content;
            msgs.push(std::move(msg));
        }
        body["messages"] = std::move(msgs);
        return body.dump();
    }

    void applyAuth(web::WebRequest& req) const {
        req.header("Content-Type", "application/json");
        if (!apiKey.empty())
            req.header("Authorization", fmt::format("Bearer {}", apiKey));
    }

    // Разбор OpenAI-ответа → текст ассистента.
    static LLMResult parseResponse(web::WebResponse& resp) {
        LLMResult out;
        out.httpCode = resp.code();
        if (!resp.ok()) {
            out.transient = (resp.code() == 429 || resp.code() >= 500);
            std::string body = resp.string().unwrapOr("");
            // вытащить message из тела ошибки, если есть
            auto jr = matjson::parse(body);
            if (jr) {
                auto j = jr.unwrap();
                auto msg = j["error"]["message"].asString();
                if (msg) out.text = msg.unwrap();
            }
            if (out.text.empty())
                out.text = fmt::format("HTTP {} — {}", resp.code(),
                                       body.size() > 300 ? body.substr(0, 300) + "..." : body);
            return out;
        }
        auto jr = resp.json();
        if (!jr) {
            out.text = "Provider returned non-JSON (is the Base URL correct?)";
            return out;
        }
        auto j = jr.unwrap();
        auto& choices = j["choices"];
        if (!choices.isArray() || choices.size() == 0) {
            out.text = "Provider returned JSON without choices[]";
            return out;
        }
        auto content = choices[0]["message"]["content"].asString();
        if (!content) {
            out.text = "Empty message content from provider";
            return out;
        }
        out.ok = true;
        out.text = content.unwrap();
        return out;
    }
};

} // namespace mll
