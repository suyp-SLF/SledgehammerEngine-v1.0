#include "menu_scene.h"
#include "game_scene.h"
#include "../../engine/core/context.h"
#include "../../engine/scene/scene_manager.h"
#include "../../engine/input/input_manager.h"
#include "../../engine/render/renderer.h"
#include "../locale/locale_manager.h"
#include <spdlog/spdlog.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

namespace game::scene
{
    MenuScene::MenuScene(const std::string &name, engine::core::Context &context, engine::scene::SceneManager &sceneManager)
        : Scene(name, context, sceneManager)
    {
    }

    void MenuScene::init()
    {
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

            if (m_showSettings)
                renderSettings();
            else
                renderMainMenu();

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
    }

    void MenuScene::renderMainMenu()
    {
        ImGui::SetNextWindowPos(ImVec2(540, 280), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(200, 155), ImGuiCond_Always);
        ImGui::Begin(locale::T("menu.title").c_str(), nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);

        if (ImGui::Button(locale::T("menu.start_game").c_str(), ImVec2(180, 40)))
            startGame();

        if (ImGui::Button(locale::T("menu.settings").c_str(), ImVec2(180, 40)))
            m_showSettings = true;

        if (ImGui::Button(locale::T("menu.quit").c_str(), ImVec2(180, 40)))
            _context.getInputManager().setShouldQuit(true);

        ImGui::End();
    }

    void MenuScene::renderSettings()
    {
        ImGui::SetNextWindowPos(ImVec2(490, 260), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(300, 160), ImGuiCond_Always);
        ImGui::Begin(locale::T("settings.title").c_str(), nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);

        // 语言选择
        ImGui::Text("%s:", locale::T("settings.language").c_str());
        ImGui::SameLine();

        const auto &langs = locale::LocaleManager::SUPPORTED_LANGUAGES;
        std::vector<const char *> lang_names;
        lang_names.reserve(langs.size());
        for (const auto &[code, name] : langs)
            lang_names.push_back(name.c_str());

        ImGui::SetNextItemWidth(160.0f);
        ImGui::Combo("##lang", &m_selectedLangIndex, lang_names.data(), (int)lang_names.size());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button(locale::T("settings.save").c_str(), ImVec2(135, 36)))
        {
            auto &lm = locale::LocaleManager::getInstance();
            lm.loadLanguage(langs[m_selectedLangIndex].first);
            lm.saveSettings();
            m_showSettings = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(locale::T("settings.back").c_str(), ImVec2(135, 36)))
        {
            // 取消选择，恢复当前语言的索引
            const std::string &cur = locale::LocaleManager::getInstance().getCurrentLanguage();
            for (int i = 0; i < (int)langs.size(); ++i)
                if (langs[i].first == cur) { m_selectedLangIndex = i; break; }
            m_showSettings = false;
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
        spdlog::info("开始游戏按钮被点击");
        auto gameScene = std::make_unique<GameScene>("GameScene123", _context, _scene_manager);
        _scene_manager.requestReplaceScene(std::move(gameScene));
    }
}
