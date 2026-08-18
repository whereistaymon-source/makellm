// ─── MakeLLM — main.cpp ──────────────────────────────────────────────────────
// Генератор уровней Geometry Dash 2.2 через любой OpenAI-compatible LLM.
// Поток: промпт → LLM → MLS-парсер → физический валидатор (проходимость,
// anti-secret-way) → refinement-раунды → спавн в редактор на preview-слой →
// Accept / Deny. Паттерны работы с редактором проверены на кодовой базе
// EditorAI (MIT), адаптированы и переписаны под MakeLLM.

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include <Geode/Geode.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/utils/NodeIDs.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/EditorPauseLayer.hpp>
#include <matjson.hpp>

#include "Catalog.hpp"
#include "MLS.hpp"
#include "Validator.hpp"
#include "Prompt.hpp"
#include "LLMClient.hpp"

using namespace geode::prelude;

// ─── Глобальное состояние превью (одна генерация за раз — синглтон) ──────────
static std::vector<Ref<GameObject>>        s_previewObjects;
static std::vector<std::pair<short,short>> s_previewIntendedLayers;
static short s_previewLayer = -1;
static short s_editorLayerBeforePreview = -1;
static bool  s_inPreviewMode = false;
static std::string s_lastPlan;                 // narration модели вне ```mls блока

static void resetPreviewState() {
    s_previewObjects.clear();
    s_previewIntendedLayers.clear();
    s_previewLayer = -1;
    s_editorLayerBeforePreview = -1;
    s_inPreviewMode = false;
}

static void setEditorCurrentLayer(LevelEditorLayer* lel, short layer) {
    if (!lel) return;
    lel->m_currentLayer = layer;
    lel->updateOptions();
    if (lel->m_editorUI && lel->m_editorUI->m_currentLayerLabel) {
        lel->m_editorUI->m_currentLayerLabel->setString(
            layer < 0 ? "ALL" : fmt::format("{}", layer).c_str());
        lel->m_editorUI->m_currentLayerLabel->setVisible(layer >= 0);
    }
}

static bool parseHexColor(const std::string& hex, GLubyte& r, GLubyte& g, GLubyte& b) {
    std::string_view h = hex;
    if (!h.empty() && h[0] == '#') h.remove_prefix(1);
    if (h.length() < 6) return false;
    auto rr = utils::numFromString<GLubyte>(h.substr(0, 2), 16);
    auto gg = utils::numFromString<GLubyte>(h.substr(2, 2), 16);
    auto bb = utils::numFromString<GLubyte>(h.substr(4, 2), 16);
    if (!rr || !gg || !bb) return false;
    r = rr.unwrap(); g = gg.unwrap(); b = bb.unwrap();
    return true;
}

// Перенос полей MObject на живой GameObject. Поля проверены по GD 2.2081 bindings.
static void applyObjectProperties(GameObject* obj, const mll::MObject& data, bool triggersEnabled) {
    if (!obj) return;

    if (data.rot != 0.f && data.rot >= -360.f && data.rot <= 360.f)
        obj->setRotation(data.rot);
    if (data.scale != 1.f) {
        float s = std::clamp(data.scale, 0.1f, 10.f);
        obj->updateCustomScaleX(s);
        obj->updateCustomScaleY(s);
    }
    if (data.flipX) obj->setFlipX(true);
    if (data.flipY) obj->setFlipY(true);

    if (data.zLayer != -999) {
        static constexpr int kValid[] = {-5, -3, -1, 0, 1, 3, 5, 7, 9, 11};
        int best = 0, bestDist = 99;
        for (int v : kValid) {
            int d = std::abs(data.zLayer - v);
            if (d < bestDist) { bestDist = d; best = v; }
        }
        obj->setCustomZLayer(best);   // сам перепарентит спрайт в нужный batch
    }
    if (data.hasZOrder)
        obj->m_zOrder = std::clamp(data.zOrder, -999, 999);

    if (data.colorCh > 0 && obj->m_baseColor)
        obj->m_baseColor->m_colorID = std::clamp(data.colorCh, 1, 1010);

    if (data.noTouch)    obj->m_isNoTouch = true;
    if (data.passable)   obj->m_isPassable = true;
    if (data.noGlow)     obj->m_hasNoGlow = true;
    if (data.highDetail) obj->m_isHighDetail = true;
    if (data.dontFade)   obj->m_isDontFade = true;
    if (data.dontEnter)  obj->m_isDontEnter = true;

    // Триггеры (EffectGameObject).
    if (triggersEnabled && !data.trig.empty()) {
        if (auto* fx = typeinfo_cast<EffectGameObject*>(obj)) {
            auto fi = [&](const char* k, float dflt = 0.f) -> float {
                auto it = data.trig.find(k);
                return it == data.trig.end() ? dflt : mll::mls::tryFloat(it->second, dflt);
            };
            auto has = [&](const char* k) { return data.trig.count(k) > 0; };

            if (has("target_group"))
                fx->m_targetGroupID = std::clamp((int)fi("target_group"), 1, 9999);
            if (has("duration"))
                fx->m_duration = std::clamp(fi("duration"), 0.f, 30.f);

            switch (data.id) {
            case mll::mls::trigid::COLOR:
                if (has("channel"))
                    fx->m_targetColor = std::clamp((int)fi("channel", 1), 1, 1010);
                if (has("hex")) {
                    GLubyte r = 255, g = 255, b = 255;
                    auto it = data.trig.find("hex");
                    if (it != data.trig.end() && parseHexColor(it->second, r, g, b))
                        fx->m_triggerTargetColor = {r, g, b};
                }
                if (has("blending")) fx->m_usesBlending = true;
                if (has("opacity")) fx->m_opacity = std::clamp(fi("opacity", 1.f), 0.f, 1.f);
                break;
            case mll::mls::trigid::MOVE:
                fx->m_moveOffset = CCPoint(
                    std::clamp(fi("move_x"), -32767.f, 32767.f),
                    std::clamp(fi("move_y"), -32767.f, 32767.f));
                if (has("easing"))
                    fx->m_easingType = (EasingType)std::clamp((int)fi("easing"), 0, 18);
                if (has("lock_to_player_x")) fx->m_lockToPlayerX = true;
                if (has("lock_to_player_y")) fx->m_lockToPlayerY = true;
                break;
            case mll::mls::trigid::PULSE:
                if (has("target_color_channel")) {
                    fx->m_targetColor = std::clamp((int)fi("target_color_channel", 1), 1, 1010);
                    fx->m_pulseTargetType = 0;  // цветовой канал
                }
                if (has("hex")) {
                    GLubyte r = 255, g = 255, b = 255;
                    auto it = data.trig.find("hex");
                    if (it != data.trig.end() && parseHexColor(it->second, r, g, b))
                        fx->m_triggerTargetColor = {r, g, b};
                }
                fx->m_fadeInDuration  = std::clamp(fi("fade_in", 0.1f), 0.f, 10.f);
                fx->m_holdDuration    = std::clamp(fi("hold", 0.2f), 0.f, 10.f);
                fx->m_fadeOutDuration = std::clamp(fi("fade_out", 0.3f), 0.f, 10.f);
                break;
            case mll::mls::trigid::ALPHA:
                fx->m_opacity = std::clamp(fi("opacity", 1.f), 0.f, 1.f);
                break;
            case mll::mls::trigid::TOGGLE:
                fx->m_activateGroup = has("activate_group") && fi("activate_group") != 0.f;
                break;
            case mll::mls::trigid::SPAWN:
                fx->m_spawnTriggerDelay = std::clamp(fi("delay"), 0.f, 30.f);
                break;
            case mll::mls::trigid::ROTATE:
                fx->m_rotationDegrees = fi("degrees", 90.f);
                if (has("center_group"))
                    fx->m_centerGroupID = std::clamp((int)fi("center_group"), 1, 9999);
                break;
            case mll::mls::trigid::STOP:
                break;   // target_group достаточно
            default: break;
            }
            if (data.multiActivate) fx->m_isMultiTriggered = true;
        }
    }
}

// Мост к хуку EditorUI (определён после класса хука).
static void showTrayOnEditorUI(EditorUI* ui);

// ─── Попап генерации ─────────────────────────────────────────────────────────
class MLLPopup : public Popup {
protected:
    LevelEditorLayer*        m_editorLayer = nullptr;
    TextInput*               m_promptInput = nullptr;
    CCLabelBMFont*           m_statusLabel = nullptr;
    CCMenuItemSpriteExtra*   m_generateBtn = nullptr;
    async::TaskHolder<web::WebResponse> m_listener;

    std::vector<mll::ChatMessage> m_history;
    mll::LLMClient            m_client;
    mll::prompt::Seed         m_seed;

    std::vector<mll::MObject> m_parsed;       // последний распарсенный набор
    size_t                   m_spawnIndex = 0;
    bool                     m_isGenerating = false;
    bool                     m_isSpawning = false;
    int                      m_round = 0;
    int                      m_maxRounds = 2;
    int                      m_spawnBatch = 8;
    bool                     m_triggersEnabled = true;
    float                    m_groundY = 105.f;
    int                      m_maxObjects = 800;

    bool init(LevelEditorLayer* lel) {
        constexpr float W = 380.f, H = 220.f;
        if (!Popup::init(W, H)) return false;
        m_editorLayer = lel;
        this->setID("makellm-popup"_spr);
        this->setTitle("MakeLLM");

        auto& M = Mod::get();
        m_client.baseUrl = M->getSettingValue<std::string>("base-url");
        m_client.apiKey  = M->getSettingValue<std::string>("api-key");
        m_client.model   = M->getSettingValue<std::string>("model");
        m_client.temperature = (float)M->getSettingValue<double>("temperature");
        m_maxRounds    = (int)M->getSettingValue<int64_t>("refinement-rounds");
        m_spawnBatch   = (int)M->getSettingValue<int64_t>("spawn-batch-size");
        m_groundY      = (float)M->getSettingValue<int64_t>("ground-y");
        m_maxObjects   = (int)M->getSettingValue<int64_t>("max-objects");
        m_triggersEnabled = M->getSettingValue<bool>("enable-triggers");

        std::string ph = m_client.baseUrl;
        if (ph.size() > 34) ph = ph.substr(0, 31) + "...";

        m_promptInput = TextInput::create(500.f, "Describe your level... (RU/EN)", "bigFont.fnt");
        m_promptInput->setPosition({W / 2.f, H - 78.f});
        m_promptInput->setScale(0.65f);
        m_mainLayer->addChild(m_promptInput);

        auto endpointLbl = CCLabelBMFont::create(
            fmt::format("{}  |  {}", ph, m_client.model).c_str(), "chatFont.fnt");
        endpointLbl->setScale(0.5f);
        endpointLbl->setColor({140, 140, 160});
        endpointLbl->setPosition({W / 2.f, H - 52.f});
        m_mainLayer->addChild(endpointLbl);

        m_statusLabel = CCLabelBMFont::create("Ready", "chatFont.fnt");
        m_statusLabel->setScale(0.55f);
        m_statusLabel->setPosition({W / 2.f, 84.f});
        m_statusLabel->limitLabelWidth(340.f, 0.55f, 0.2f);
        m_mainLayer->addChild(m_statusLabel);

        auto genSpr = ButtonSprite::create("Generate", "bigFont.fnt", "GJ_button_01.png", 0.65f);
        m_generateBtn = CCMenuItemSpriteExtra::create(
            genSpr, this, menu_selector(MLLPopup::onGenerate));
        m_generateBtn->setID("generate-btn"_spr);
        m_generateBtn->setPosition({W / 2.f, 44.f});
        m_buttonMenu->addChild(m_generateBtn);

        this->schedule(schedule_selector(MLLPopup::updateSpawning), 0.05f);
        return true;
    }

    void status(const std::string& s, ccColor3B col = {220, 220, 220}) {
        if (m_statusLabel) {
            m_statusLabel->setString(s.c_str());
            m_statusLabel->setColor(col);
        }
        log::info("MakeLLM: {}", s);
    }

    mll::prompt::Settings currentSettings() {
        auto& M = Mod::get();
        mll::prompt::Settings s;
        s.difficulty = M->getSettingValue<std::string>("difficulty");
        s.style      = M->getSettingValue<std::string>("style");
        s.length     = M->getSettingValue<std::string>("length");
        s.maxObjects = m_maxObjects;
        s.groundY    = m_groundY;
        s.triggers   = m_triggersEnabled;
        s.noSecretWay = M->getSettingValue<bool>("prevent-secret-way");
        s.uniqueSeed = M->getSettingValue<bool>("unique-seed");
        return s;
    }

    void onGenerate(CCObject*) {
        if (m_isGenerating) return;
        if (s_inPreviewMode) {
            status("Accept or deny the current preview first!", {255, 180, 80});
            return;
        }
        if (m_client.baseUrl.empty() || m_client.model.empty()) {
            status("Set Base URL and Model in mod settings (Geode -> Mods -> MakeLLM)",
                   {255, 120, 120});
            return;
        }
        if (!m_editorLayer) {
            status("No editor layer", {255, 120, 120});
            return;
        }

        m_isGenerating = true;
        m_round = 0;
        if (m_generateBtn) m_generateBtn->setEnabled(false);
        m_seed = mll::prompt::rollSeed();

        auto s = currentSettings();
        m_history.clear();
        m_history.push_back({"system", mll::prompt::buildSystemPrompt(s, m_seed)});
        m_history.push_back({"user", mll::prompt::buildUserPrompt(m_promptInput->getString(), s)});
        sendRound();
    }

    void sendRound() {
        ++m_round;
        status(m_round == 1 ? "Generating..." :
               fmt::format("Refining (round {}/{})...", m_round - 1, m_maxRounds),
               {120, 200, 255});

        auto req = web::WebRequest();
        m_client.applyAuth(req);
        req.timeout(std::chrono::seconds(m_client.timeoutSec));
        std::string body = m_client.buildBody(m_history);
        req.bodyString(body);
        std::string url = m_client.chatUrl();
        log::info("MakeLLM: POST {} ({} bytes)", url, body.size());
        m_listener.spawn(
            req.post(url),
            [this](web::WebResponse resp) { this->onResponse(std::move(resp)); });
    }

    void retrySend() {
        if (m_isGenerating) sendRound();
    }

    void onResponse(web::WebResponse resp) {
        auto parsed = mll::LLMClient::parseResponse(resp);
        if (!parsed.ok) {
            // один ретрай на транзиентные ошибки (429/5xx)
            if (parsed.transient && m_round == 1) {
                status(fmt::format("HTTP {} — retrying...", parsed.httpCode), {255, 200, 100});
                --m_round;
                this->runAction(CCSequence::create(
                    CCDelayTime::create(2.f),
                    CCCallFunc::create(this, callfunc_selector(MLLPopup::retrySend)),
                    nullptr));
                return;
            }
            fail(parsed.text);
            return;
        }

        std::string text = parsed.text;
        m_history.push_back({"assistant", text});

        auto fence = text.find("```");
        if (fence != 0 && fence != std::string::npos)
            s_lastPlan = mll::mls::trim(text.substr(0, fence));

        std::string script = mll::mls::extractScript(text);
        auto res = mll::mls::parse(script, m_groundY);

        if (res.objects.empty()) {
            fail("The model produced no objects. Try a clearer prompt or another model.");
            return;
        }
        if ((int)res.objects.size() > m_maxObjects) {
            res.objects.resize(m_maxObjects);
            res.warnings.push_back("truncated to max-objects");
        }

        // ── Физическая валидация ──
        auto s = currentSettings();
        auto report = mll::validate::check(res.objects, m_groundY, s.noSecretWay);
        log::info("MakeLLM: validation — passed={} jumps={} issues={}",
                  (int)report.passed, report.jumpCount, report.issues.size());

        if (!report.ok() && (m_round - 1) < m_maxRounds) {
            m_history.push_back({"user", mll::prompt::buildRefinementPrompt(report.toPromptText())});
            sendRound();
            return;
        }

        if (!report.ok())
            status("Validation issues remain — staging anyway (review!)", {255, 200, 100});
        else if (m_round > 1)
            status(fmt::format("Validated after {} round(s)", m_round), {120, 255, 140});

        m_parsed = std::move(res.objects);
        startSpawning();
    }

    void fail(const std::string& msg) {
        m_isGenerating = false;
        if (m_generateBtn) m_generateBtn->setEnabled(true);
        status(msg, {255, 120, 120});
        log::error("MakeLLM: {}", msg);
    }

    // ── Спавн в редактор батчами (не фризит UI) ──
    void startSpawning() {
        if (!m_editorLayer) { fail("Editor gone"); return; }

        short maxLayer = 0;
        if (m_editorLayer->m_objects) {
            for (auto* raw : CCArrayExt<CCObject*>(m_editorLayer->m_objects)) {
                auto* go = typeinfo_cast<GameObject*>(raw);
                if (!go) continue;
                maxLayer = std::max({maxLayer, go->m_editorLayer, go->m_editorLayer2});
            }
        }
        s_previewObjects.clear();
        s_previewIntendedLayers.clear();
        s_previewLayer = (short)std::min<int>(maxLayer + 1, 999);
        s_editorLayerBeforePreview = m_editorLayer->m_currentLayer;
        setEditorCurrentLayer(m_editorLayer, s_previewLayer);

        m_spawnIndex = 0;
        m_isSpawning = true;
        status(fmt::format("Placing {} objects...", m_parsed.size()), {120, 200, 255});
    }

    void updateSpawning(float) {
        if (!m_isSpawning || !m_editorLayer) return;
        for (int i = 0; i < m_spawnBatch && m_spawnIndex < m_parsed.size(); ++i)
            spawnOne(m_parsed[m_spawnIndex++]);
        if (m_spawnIndex >= m_parsed.size()) {
            m_isSpawning = false;
            finishSpawning();
        }
    }

    void spawnOne(const mll::MObject& d) {
        GameObject* obj = m_editorLayer->createObject(d.id, CCPoint(d.x, d.y), false);
        if (!obj || !obj->m_objectID) return;
        applyObjectProperties(obj, d, m_triggersEnabled);
        s_previewIntendedLayers.emplace_back(obj->m_editorLayer, obj->m_editorLayer2);
        obj->m_editorLayer  = s_previewLayer;
        obj->m_editorLayer2 = s_previewLayer;
        s_previewObjects.emplace_back(obj);
    }

    void finishSpawning() {
        m_isGenerating = false;
        if (m_generateBtn) m_generateBtn->setEnabled(true);
        s_inPreviewMode = true;
        if (m_editorLayer && m_editorLayer->m_editorUI)
            m_editorLayer->m_editorUI->updateButtons();
        showTrayOnEditorUI(m_editorLayer ? m_editorLayer->m_editorUI : nullptr);
        Notification::create(
            fmt::format("MakeLLM: {} objects on layer {} — Accept or Deny",
                        s_previewObjects.size(), s_previewLayer),
            NotificationIcon::Info)->show();
        status(fmt::format("Staged {} objects — Accept/Deny in the editor",
                           s_previewObjects.size()), {120, 255, 140});
    }

public:
    static MLLPopup* create(LevelEditorLayer* lel) {
        auto ret = new MLLPopup();
        if (ret && ret->init(lel)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// ─── Хук EditorUI: кнопка LLM + трей Accept/Deny ─────────────────────────────
class $modify(MLLEditorUI, EditorUI) {
    struct Fields {
        CCNode* m_tray = nullptr;
    };

    bool init(LevelEditorLayer* layer) {
        if (!EditorUI::init(layer)) return false;
        // Новый редактор → превью из мёртвой сцены недействительно.
        resetPreviewState();

        NodeIDs::provideFor(this);
        this->runAction(CCSequence::create(
            CCDelayTime::create(0.1f),
            CCCallFunc::create(this, callfunc_selector(MLLEditorUI::addAIButton)),
            nullptr));
        return true;
    }

    void addAIButton() {
        if (auto menu = this->getChildByID("editor-buttons-menu")) {
            if (menu->getChildByID("makellm-button"_spr)) return;
            auto spr = ButtonSprite::create("LLM", 30, true, "bigFont.fnt",
                                            "GJ_button_04.png", 32.f, 0.7f);
            auto btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(MLLEditorUI::onAIButton));
            btn->setID("makellm-button"_spr);
            menu->addChild(btn);
            menu->updateLayout();
        }
    }

    void onAIButton(CCObject*) {
        if (s_inPreviewMode) {
            Notification::create("Accept or deny the preview first!",
                                 NotificationIcon::Warning)->show();
            return;
        }
        if (auto popup = MLLPopup::create(this->m_editorLayer))
            popup->show();
    }

    void showPreviewTray() {
        if (m_fields->m_tray) return;
        auto menu = CCMenu::create();
        menu->setID("makellm-preview-menu"_spr);

        auto mkBtn = [&](const char* text, const char* bg, SEL_MenuHandler sel, const char* id) {
            auto spr = ButtonSprite::create(text, "bigFont.fnt", bg, 0.55f);
            auto b = CCMenuItemSpriteExtra::create(spr, this, sel);
            b->setID(id);
            return b;
        };
        auto accept = mkBtn("Accept", "GJ_button_01.png", menu_selector(MLLEditorUI::onAccept), "makellm-accept-btn"_spr);
        auto deny   = mkBtn("Deny",   "GJ_button_06.png", menu_selector(MLLEditorUI::onDeny),   "makellm-deny-btn"_spr);
        accept->setPosition({0.f, 25.f});
        deny->setPosition({0.f, -15.f});
        menu->addChild(accept);
        menu->addChild(deny);

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        menu->setPosition({60.f, winSize.height - 120.f});
        this->addChild(menu, 1000);
        m_fields->m_tray = menu;
    }

    void removePreviewTray() {
        if (m_fields->m_tray) {
            m_fields->m_tray->removeFromParent();
            m_fields->m_tray = nullptr;
        }
    }

    void onAccept(CCObject*) {
        if (!m_editorLayer) return;

        // Вернуть объектам изначальные слои и редактору — его слой.
        for (size_t i = 0; i < s_previewObjects.size(); ++i) {
            if (GameObject* obj = s_previewObjects[i]) {
                auto intended = i < s_previewIntendedLayers.size()
                    ? s_previewIntendedLayers[i] : std::pair<short, short>{0, 0};
                obj->m_editorLayer  = intended.first;
                obj->m_editorLayer2 = intended.second;
            }
        }
        setEditorCurrentLayer(m_editorLayer, s_editorLayerBeforePreview);

        // Весь принятый пакет — одним шагом Undo (как Paste).
        if (m_editorLayer->m_undoObjects && !s_previewObjects.empty()) {
            auto batch = CCArray::create();
            for (auto& ref : s_previewObjects)
                if (GameObject* obj = ref)
                    if (obj->getParent()) batch->addObject(obj);
            if (batch->count() > 0) {
                if (auto* undo = UndoObject::createWithArray(batch, UndoCommand::Paste)) {
                    m_editorLayer->m_undoObjects->addObject(undo);
                    if (m_editorLayer->m_redoObjects)
                        m_editorLayer->m_redoObjects->removeAllObjects();
                }
            }
        }

        s_previewObjects.clear();
        s_previewIntendedLayers.clear();
        s_previewLayer = -1;
        s_inPreviewMode = false;
        removePreviewTray();
        this->updateButtons();
        Notification::create("MakeLLM: level accepted!", NotificationIcon::Success)->show();
    }

    void onDeny(CCObject*) {
        if (m_editorLayer) {
            for (auto& ref : s_previewObjects) {
                if (GameObject* obj = ref)
                    if (obj->getParent())
                        m_editorLayer->removeObject(obj, true);
            }
            setEditorCurrentLayer(m_editorLayer, s_editorLayerBeforePreview);
        }
        s_previewObjects.clear();
        s_previewIntendedLayers.clear();
        s_previewLayer = -1;
        s_inPreviewMode = false;
        removePreviewTray();
        if (m_editorLayer && m_editorLayer->m_editorUI)
            m_editorLayer->m_editorUI->updateButtons();
        Notification::create("MakeLLM: preview denied", NotificationIcon::Warning)->show();
    }
};

static void showTrayOnEditorUI(EditorUI* ui) {
    if (ui) static_cast<MLLEditorUI*>(ui)->showPreviewTray();
}

// ─── Блокировка плейтеста во время превью ────────────────────────────────────
class $modify(MLLLevelEditorLayer, LevelEditorLayer) {
    void onPlaytest() {
        if (s_inPreviewMode) {
            Notification::create("Accept or deny the AI preview first!",
                                 NotificationIcon::Warning)->show();
            return;
        }
        LevelEditorLayer::onPlaytest();
    }
};

// ─── Пауза: прячем трей, чтобы не плавал поверх меню паузы ───────────────────
class $modify(MLLEditorPauseLayer, EditorPauseLayer) {
    bool init(LevelEditorLayer* layer) {
        if (!EditorPauseLayer::init(layer)) return false;
        if (layer && layer->m_editorUI)
            if (auto tray = layer->m_editorUI->getChildByID("makellm-preview-menu"_spr))
                tray->setVisible(false);
        return true;
    }
    void onResume(CCObject* sender) {
        EditorPauseLayer::onResume(sender);
        if (m_editorLayer && m_editorLayer->m_editorUI)
            if (auto tray = m_editorLayer->m_editorUI->getChildByID("makellm-preview-menu"_spr))
                tray->setVisible(true);
    }
};

$on_mod(Loaded) {
    geode::log::info("MakeLLM loaded — LLM level generator ready");
}
