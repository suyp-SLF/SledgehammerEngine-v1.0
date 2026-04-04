#include "menu_scene.h"
#include "ship_scene.h"
#include "route_select_scene.h"
#include "game_scene.h"
#include "character_editor_scene.h"
#include "voxel_scene.h"
#include "../../engine/core/context.h"
#include "../../engine/scene/scene_manager.h"
#include "../../engine/input/input_manager.h"
#include "../../engine/render/renderer.h"
#include "../../engine/render/opengl_renderer.h"
#include "../../engine/render/sdl_renderer.h"
#include "../../engine/core/context.h"
#include "../../engine/core/time.h"
#include "../../engine/input/input_manager.h"
#include "../locale/locale_manager.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <spdlog/spdlog.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

namespace game::scene
{
    // ── config.json 读写助手（内部使用）────────────────────────────────────
    static nlohmann::json loadMenuConfigJson()
    {
        std::ifstream f("assets/config.json");
        if (!f.is_open()) return {};
        try { return nlohmann::json::parse(f); } catch (...) { return {}; }
    }

    template <typename T>
    static void saveMenuConfigValue(const char* section, const char* key, T value)
    {
        nlohmann::json j = loadMenuConfigJson();
        if (!j.is_object()) j = nlohmann::json::object();
        if (!j.contains(section) || !j[section].is_object())
            j[section] = nlohmann::json::object();
        j[section][key] = value;
        std::ofstream out("assets/config.json");
        if (out.is_open()) out << j.dump(4);
    }

    // ImGuiKey → SDL 按键名（按键映射 UI 内部使用）
    static const char* menuKeyToSDLName(ImGuiKey key)
    {
        if (key >= ImGuiKey_A && key <= ImGuiKey_Z) {
            static const char* s[] = {
                "A","B","C","D","E","F","G","H","I","J","K","L","M",
                "N","O","P","Q","R","S","T","U","V","W","X","Y","Z"
            };
            return s[key - ImGuiKey_A];
        }
        if (key >= ImGuiKey_0 && key <= ImGuiKey_9) {
            static const char* s[] = {"0","1","2","3","4","5","6","7","8","9"};
            return s[key - ImGuiKey_0];
        }
        if (key >= ImGuiKey_F1 && key <= ImGuiKey_F12) {
            static const char* s[] = {
                "F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"
            };
            return s[key - ImGuiKey_F1];
        }
        switch (key) {
        case ImGuiKey_LeftArrow:    return "Left";
        case ImGuiKey_RightArrow:   return "Right";
        case ImGuiKey_UpArrow:      return "Up";
        case ImGuiKey_DownArrow:    return "Down";
        case ImGuiKey_Space:        return "Space";
        case ImGuiKey_Enter:        return "Return";
        case ImGuiKey_Escape:       return "Escape";
        case ImGuiKey_Tab:          return "Tab";
        case ImGuiKey_Backspace:    return "Backspace";
        case ImGuiKey_Delete:       return "Delete";
        case ImGuiKey_Insert:       return "Insert";
        case ImGuiKey_Home:         return "Home";
        case ImGuiKey_End:          return "End";
        case ImGuiKey_PageUp:       return "PageUp";
        case ImGuiKey_PageDown:     return "PageDown";
        case ImGuiKey_LeftShift:
        case ImGuiKey_RightShift:   return "Left Shift";
        case ImGuiKey_LeftCtrl:
        case ImGuiKey_RightCtrl:    return "Left Ctrl";
        case ImGuiKey_LeftAlt:
        case ImGuiKey_RightAlt:     return "Left Alt";
        case ImGuiKey_GraveAccent:  return "`";
        case ImGuiKey_Minus:        return "-";
        case ImGuiKey_Equal:        return "=";
        case ImGuiKey_LeftBracket:  return "[";
        case ImGuiKey_RightBracket: return "]";
        case ImGuiKey_Semicolon:    return ";";
        case ImGuiKey_Apostrophe:   return "'";
        case ImGuiKey_Comma:        return ",";
        case ImGuiKey_Period:       return ".";
        case ImGuiKey_Slash:        return "/";
        default:                    return nullptr;
        }
    }

    MenuScene::MenuScene(const std::string &name, engine::core::Context &context, engine::scene::SceneManager &sceneManager)
        : Scene(name, context, sceneManager)
    {
    }

    void MenuScene::init()
    {
        Scene::init(); // 设置 _is_initialized = true

        // 初始化多语言系统，读取上次保存的语言
        auto &lm = locale::LocaleManager::getInstance();
        const auto &langs = locale::LocaleManager::SUPPORTED_LANGUAGES;
        const std::string &cur = lm.getCurrentLanguage();
        for (int i = 0; i < (int)langs.size(); ++i)
        {
            if (langs[i].first == cur)
            {
                m_selectedLangIndex = i;
                break;
            }
        }

        SDL_Window *window = _context.getRenderer().getWindow();
        if (window)
        {
            m_glContext = SDL_GL_GetCurrentContext();
            if (m_glContext)
            {
                IMGUI_CHECKVERSION();
                ImGui::CreateContext();

                // 加载支持中文的字体，解决中文显示为问号的问题
                ImGuiIO &io = ImGui::GetIO();
                io.Fonts->AddFontFromFileTTF(
                    "assets/fonts/VonwaonBitmap-16px.ttf",
                    16.0f, nullptr,
                    io.Fonts->GetGlyphRangesChineseSimplifiedCommon());

                ImGui_ImplSDL3_InitForOpenGL(window, m_glContext);
                ImGui_ImplOpenGL3_Init("#version 330");
                spdlog::info("MenuScene: ImGui 初始化完成（含中文字体）");
            }
        }
        // 宇宙编辑器默认关闭，仅在点击"地图编辑器"按钮时打开
        m_universeEditor.setOpen(false);

        // 读取已保存的图形设置并立即应用
        {
            auto j = loadMenuConfigJson();
            if (j.contains("performance") && j["performance"].is_object() && j["performance"].contains("target_fps"))
                m_maxFpsSlider = j["performance"]["target_fps"].get<int>();
            if (j.contains("graphics") && j["graphics"].is_object() && j["graphics"].contains("vsync"))
                m_vsyncEnabled = j["graphics"]["vsync"].get<bool>();
        }
        _context.getTime().setTargetFPS(m_maxFpsSlider);
        _context.getTime().setFrameLimitEnabled(!m_vsyncEnabled);
        if (m_glContext)
            SDL_GL_SetSwapInterval(m_vsyncEnabled ? -1 : 0);

        spdlog::info("MenuScene 初始化完成");
    }

    void MenuScene::update(float delta_time)
    {
    }

    void MenuScene::render()
    {
        if (m_glContext)
        {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            renderPerformanceOverlay();

            if (m_showSettings)
                renderSettings();
            else
                renderMainMenu();

            // 宇宙编辑器（地图编辑器）悬浮于主菜单上方
            if (m_universeEditor.isOpen())
                m_universeEditor.render();

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
    }

    void MenuScene::renderMainMenu()
    {
        ImGui::SetNextWindowPos(ImVec2(540, 280), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(260, 300), ImGuiCond_Always);
        ImGui::Begin(locale::T("menu.title").c_str(), nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);

        if (ImGui::Button("开始游戏", ImVec2(230, 40)))
            startGame();

        if (ImGui::Button("设置", ImVec2(230, 40)))
            m_showSettings = true;

        if (ImGui::Button("结束游戏", ImVec2(230, 40)))
            _context.getInputManager().setShouldQuit(true);

        ImGui::Separator();
        ImGui::TextDisabled("开发功能");

        if (ImGui::Button("角色编辑器", ImVec2(230, 36)))
            openCharacterEditor();

        if (ImGui::Button("地图编辑器", ImVec2(230, 36)))
            openMapEditor();

        ImGui::End();
    }

    void MenuScene::renderSettings()
    {
        // 居中大设置窗口（逻辑分辨率 1280×720）
        ImGui::SetNextWindowPos(ImVec2(290.0f, 30.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(700.0f, 660.0f), ImGuiCond_Always);
        if (!ImGui::Begin("设置", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove))
        {
            ImGui::End();
            return;
        }

        // 提前声明（后续多处使用）
        const auto& langs     = locale::LocaleManager::SUPPORTED_LANGUAGES;
        auto&       inputMgr  = _context.getInputManager();
        const auto& bindings  = inputMgr.getActionBindings();

        // ── 语言 ─────────────────────────────────────────────────────────
        ImGui::SeparatorText("语言");
        ImGui::Text("界面语言:");
        ImGui::SameLine();
        {
            std::vector<const char*> ln;
            ln.reserve(langs.size());
            for (const auto& [code, name] : langs) ln.push_back(name.c_str());
            ImGui::SetNextItemWidth(200.0f);
            ImGui::Combo("##lang", &m_selectedLangIndex, ln.data(), (int)ln.size());
        }

        // ── 图形 & 性能 ──────────────────────────────────────────────────
        ImGui::SeparatorText("图形设置");
        if (ImGui::Checkbox("垂直同步 VSync", &m_vsyncEnabled))
        {
            SDL_GL_SetSwapInterval(m_vsyncEnabled ? -1 : 0);
            _context.getTime().setFrameLimitEnabled(!m_vsyncEnabled);
            saveMenuConfigValue("graphics", "vsync", m_vsyncEnabled);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("开启时帧率由显示器刷新率决定");
        {
            constexpr int kP[] = {30, 60, 120, 144, 240};
            ImGui::Text("最大帧率:");
            ImGui::SameLine();
            for (int p : kP)
            {
                bool sel = (m_maxFpsSlider == p);
                if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.55f, 1.0f, 1.0f));
                char buf[8]; snprintf(buf, sizeof(buf), "%d", p);
                if (ImGui::Button(buf)) {
                    m_maxFpsSlider = p;
                    _context.getTime().setTargetFPS(p);
                    _context.getTime().setFrameLimitEnabled(!m_vsyncEnabled);
                    saveMenuConfigValue("performance", "target_fps", p);
                }
                if (sel) ImGui::PopStyleColor();
                ImGui::SameLine();
            }
            {
                bool sel = (m_maxFpsSlider == 0);
                if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.55f, 1.0f, 1.0f));
                if (ImGui::Button("不限")) {
                    m_maxFpsSlider = 0;
                    _context.getTime().setTargetFPS(0);
                    _context.getTime().setFrameLimitEnabled(!m_vsyncEnabled);
                    saveMenuConfigValue("performance", "target_fps", 0);
                }
                if (sel) ImGui::PopStyleColor();
            }
        }

        // ── 按键映射 ─────────────────────────────────────────────────────
        ImGui::SeparatorText("按键映射");
        struct ActionLabel { const char* id; const char* label; };
        static constexpr ActionLabel kActions[] = {
            { "move_left",      "向左移动"    },
            { "move_right",     "向右移动"    },
            { "move_up",        "向上/上坡"   },
            { "move_down",      "向下/蹲下"   },
            { "jump",           "跳跃"        },
            { "attack",         "攻击"        },
            { "skill_use",      "技能"        },
            { "interact",       "交互"        },
            { "open_inventory", "背包"        },
            { "open_map",       "地图"        },
            { "evacuate",       "撤离"        },
            { "possess",        "附身"        },
            { "pause",          "暂停"        },
            { "open_settings",  "打开设置"    },
        };
        if (ImGui::BeginTable("##mkbind", 4,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("动作", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("主键", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("副键", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("操作", ImGuiTableColumnFlags_WidthFixed,  58.0f);
            ImGui::TableHeadersRow();
            for (auto& [aid, alabel] : kActions)
            {
                auto it = bindings.find(aid);
                const std::string k0 = (it != bindings.end() && it->second.size() > 0) ? it->second[0] : "";
                const std::string k1 = (it != bindings.end() && it->second.size() > 1) ? it->second[1] : "";
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(alabel);
                ImGui::TableSetColumnIndex(1);
                {
                    char bi[64]; snprintf(bi, sizeof(bi), "%s##mk0_%s", k0.empty() ? "—" : k0.c_str(), aid);
                    bool lsn = (m_keyListeningAction == aid && m_keyListeningSlot == 0);
                    if (lsn) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
                    if (ImGui::Button(bi, ImVec2(126.0f, 0)))
                    { m_keyListeningAction = aid; m_keyListeningSlot = 0; m_keyListeningFrames = 0; ImGui::OpenPopup("##mkcap"); }
                    if (lsn) ImGui::PopStyleColor();
                }
                ImGui::TableSetColumnIndex(2);
                {
                    char bi[64]; snprintf(bi, sizeof(bi), "%s##mk1_%s", k1.empty() ? "—" : k1.c_str(), aid);
                    bool lsn = (m_keyListeningAction == aid && m_keyListeningSlot == 1);
                    if (lsn) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
                    if (ImGui::Button(bi, ImVec2(126.0f, 0)))
                    { m_keyListeningAction = aid; m_keyListeningSlot = 1; m_keyListeningFrames = 0; ImGui::OpenPopup("##mkcap"); }
                    if (lsn) ImGui::PopStyleColor();
                }
                ImGui::TableSetColumnIndex(3);
                if (!k0.empty() || !k1.empty())
                {
                    char bi[32]; snprintf(bi, sizeof(bi), "重置##mr_%s", aid);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.10f, 0.10f, 1.0f));
                    if (ImGui::Button(bi, ImVec2(50.0f, 0)))
                    { inputMgr.rebindAction(aid, {}); saveMenuConfigValue("input_mapping", aid, std::vector<std::string>{}); }
                    ImGui::PopStyleColor();
                }
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("点击槽位后按下新按键以重新绑定；Escape 取消。");

        // ── 底部按钮 ─────────────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button("保存语言 & 返回", ImVec2(220.0f, 32.0f)))
        {
            auto& lm = locale::LocaleManager::getInstance();
            lm.loadLanguage(langs[m_selectedLangIndex].first);
            lm.saveSettings();
            m_showSettings = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(100.0f, 32.0f)))
        {
            const std::string& cur = locale::LocaleManager::getInstance().getCurrentLanguage();
            for (int i = 0; i < (int)langs.size(); ++i)
                if (langs[i].first == cur) { m_selectedLangIndex = i; break; }
            m_showSettings = false;
        }

        // ── 按键捕获弹窗 ─────────────────────────────────────────────────
        ImGui::SetNextWindowSize(ImVec2(320, 130), ImGuiCond_Always);
        if (ImGui::BeginPopupModal("##mkcap", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
        {
            m_keyListeningFrames++;
            const char* actionLabel = m_keyListeningAction.c_str();
            for (auto& [aid, alabel] : kActions)
                if (m_keyListeningAction == aid) { actionLabel = alabel; break; }
            ImGui::Spacing();
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize("请按下新按键...").x) * 0.5f);
            ImGui::TextUnformatted("请按下新按键...");
            ImGui::Spacing();
            ImGui::Text("  动作: %s  |  槽位: %s", actionLabel, m_keyListeningSlot == 0 ? "主键" : "副键");
            ImGui::Spacing();

            const char* capturedKey = nullptr;
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END && !capturedKey; ++k)
            {
                if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false))
                {
                    if (k == ImGuiKey_Escape) { m_keyListeningAction.clear(); ImGui::CloseCurrentPopup(); break; }
                    capturedKey = menuKeyToSDLName(static_cast<ImGuiKey>(k));
                }
            }
            if (!capturedKey && m_keyListeningFrames >= 2)
            {
                if      (ImGui::IsMouseClicked(ImGuiMouseButton_Left))   capturedKey = "MouseLeft";
                else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))  capturedKey = "MouseRight";
                else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) capturedKey = "MouseMiddle";
            }
            if (capturedKey)
            {
                auto it2 = bindings.find(m_keyListeningAction);
                std::vector<std::string> nk = (it2 != bindings.end()) ? it2->second : std::vector<std::string>{};
                if (m_keyListeningSlot == 0) { if (nk.empty()) nk.push_back(capturedKey); else nk[0] = capturedKey; }
                else { if (nk.size() < 2) nk.resize(2); nk[1] = capturedKey; }
                nk.erase(std::remove_if(nk.begin(), nk.end(), [](const std::string& s){ return s.empty(); }), nk.end());
                inputMgr.rebindAction(m_keyListeningAction, nk);
                saveMenuConfigValue("input_mapping", m_keyListeningAction.c_str(), nk);
                m_keyListeningAction.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - 80.0f) * 0.5f);
            if (ImGui::Button("取消##mkcap_cancel", ImVec2(80.0f, 0)))
            { m_keyListeningAction.clear(); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        ImGui::End();
    }

    void MenuScene::handleInput()
    {
    }

    void MenuScene::clean()
    {
        if (m_glContext)
        {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            m_glContext = nullptr;
        }
    }

    void MenuScene::startGame()
    {
        spdlog::info("开始游戏 → 星球选择");
        auto scene = std::make_unique<RouteSelectScene>("RouteSelectScene", _context, _scene_manager);
        _scene_manager.requestReplaceScene(std::move(scene));
    }

    void MenuScene::openCharacterEditor()
    {
        auto scene = std::make_unique<CharacterEditorScene>("CharacterEditorScene", _context, _scene_manager);
        _scene_manager.requestReplaceScene(std::move(scene));
    }

    void MenuScene::openMapEditor()
    {
        m_universeEditor.reload();
        m_universeEditor.setOpen(true);
    }

    void MenuScene::renderPerformanceOverlay() const
    {
        ImGui::SetNextWindowPos({10.0f, 10.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.45f);
        ImGui::Begin("##fps_menu", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::End();
    }
}
