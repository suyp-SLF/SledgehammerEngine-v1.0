#include "character_editor_scene.h"

#include "menu_scene.h"
#include "../../engine/core/context.h"
#include "../../engine/input/input_manager.h"
#include "../../engine/render/renderer.h"
#include "../../engine/resource/resource_manager.h"
#include "../../engine/scene/scene_manager.h"
#include "../locale/locale_manager.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

namespace game::scene
{
    template <size_t N>
    static bool strDiff(const std::array<char, N>& buf, const std::string& value)
    {
        return std::string(buf.data()) != value;
    }

    static bool hasImageExt(const std::filesystem::path& p)
    {
        const std::string ext = p.extension().string();
        return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".bmp";
    }

    CharacterEditorScene::CharacterEditorScene(const std::string& name,
                                               engine::core::Context& context,
                                               engine::scene::SceneManager& sceneManager)
        : Scene(name, context, sceneManager)
    {
    }

    void CharacterEditorScene::init()
    {
        Scene::init();

        SDL_Window* window = _context.getRenderer().getWindow();
        if (window)
        {
            m_glContext = SDL_GL_GetCurrentContext();
            if (m_glContext)
            {
                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.Fonts->AddFontFromFileTTF(
                    "assets/fonts/VonwaonBitmap-16px.ttf",
                    16.0f,
                    nullptr,
                    io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
                ImGui_ImplSDL3_InitForOpenGL(window, m_glContext);
                ImGui_ImplOpenGL3_Init("#version 330");
            }
        }

        loadProfiles();
        scanTextureAssets();
    }

    void CharacterEditorScene::update(float)
    {
    }

    void CharacterEditorScene::render()
    {
        if (!m_glContext)
            return;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        renderPerformanceOverlay();
        renderToolbar();

        if (m_showSettings)
            renderSettings();
        else
            renderProfilePanel();


        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void CharacterEditorScene::handleInput()
    {
    }

    void CharacterEditorScene::clean()
    {
        if (m_glContext)
        {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            m_glContext = nullptr;
        }
    }

    void CharacterEditorScene::renderToolbar()
    {
        const float toolbarW = ImGui::GetIO().DisplaySize.x - 24.0f;
        ImGui::SetNextWindowPos({12.0f, 42.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({toolbarW, 86.0f}, ImGuiCond_Always);
        if (!ImGui::Begin("角色编辑器入口", nullptr,
                          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove))
        {
            ImGui::End();
            return;
        }

        if (ImGui::Button("返回开始界面", ImVec2(130, 36)))
        {
            auto scene = std::make_unique<MenuScene>("MenuScene", _context, _scene_manager);
            _scene_manager.requestReplaceScene(std::move(scene));
        }
        ImGui::SameLine();
        if (ImGui::Button("保存当前角色文件", ImVec2(145, 36)))
            saveCurrentProfile();
        ImGui::SameLine();
        if (ImGui::Button("新建角色", ImVec2(100, 36)))
        {
            m_showCreatePopup = true;
            ImGui::OpenPopup("新建角色文件");
        }
        ImGui::SameLine();
        if (ImGui::Button("设置", ImVec2(100, 36)))
            m_showSettings = !m_showSettings;


        if (ImGui::BeginPopupModal("新建角色文件", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("输入角色 ID 和显示名后，将自动创建文件。\n路径: assets/characters/<id>.character.json");
            ImGui::Spacing();
            ImGui::InputText("角色ID", m_newCharacterIdBuf.data(), m_newCharacterIdBuf.size());
            ImGui::InputText("显示名", m_newCharacterNameBuf.data(), m_newCharacterNameBuf.size());

            if (ImGui::Button("创建", ImVec2(120, 0)))
            {
                createNewCharacterFile();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("取消", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (m_hasUnsavedChanges)
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.3f, 1.0f), "有未保存修改 *");
        else
            ImGui::TextDisabled("当前已保存");

        ImGui::End();
    }

    void CharacterEditorScene::renderProfilePanel()

        {
            bool pendingTextChanges = false;
            if (m_selectedProfile >= 0 && m_selectedProfile < static_cast<int>(m_profiles.size()))
            {
                const auto& p = m_profiles[static_cast<size_t>(m_selectedProfile)];
                pendingTextChanges = (std::string(m_editIdBuf.data()) != p.id) ||
                                     (std::string(m_editNameBuf.data()) != p.displayName) ||
                                     strDiff(m_editFrameJsonBuf, p.frameJsonPath) ||
                                     strDiff(m_editSmJsonBuf, p.stateMachineJsonPath) ||
                                     strDiff(m_editTextureBuf, p.texturePath) ||
                                     (std::string(m_editMapRoleBuf.data()) != p.mapRole);
            }

            const std::string panelTitle = (m_hasUnsavedChanges || pendingTextChanges) ? "角色文件 *" : "角色文件";
            const ImGuiIO& io = ImGui::GetIO();
            const float winW  = io.DisplaySize.x - 24.0f;
            const float winH  = io.DisplaySize.y - 136.0f;  // 从 toolbar 底部到屏幕底部

            ImGui::SetNextWindowPos({12.0f, 136.0f}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({winW, winH}, ImGuiCond_Always);
            if (!ImGui::Begin(panelTitle.c_str(), nullptr,
                              ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove))
            {
                ImGui::End();
                return;
            }

            constexpr float kListW    = 200.0f;
            constexpr float kPreviewW = 320.0f;

            if (ImGui::BeginTable("##ce_main", 3,
                    ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("##ce_col_list",    ImGuiTableColumnFlags_WidthFixed,   kListW);
                ImGui::TableSetupColumn("##ce_col_tabs",    ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("##ce_col_preview", ImGuiTableColumnFlags_WidthFixed,   kPreviewW);

                // ── 第一列：角色列表 ─────────────────────────────────────────
                ImGui::TableNextColumn();
                ImGui::TextDisabled("角色列表 (%d)", static_cast<int>(m_profiles.size()));
                ImGui::Separator();
                ImGui::BeginChild("##ce_char_list", ImVec2(0.0f, 0.0f), false);
                for (int i = 0; i < static_cast<int>(m_profiles.size()); ++i)
                {
                    const bool selFlag = (i == m_selectedProfile);
                    const auto& pr = m_profiles[static_cast<size_t>(i)];
                    const std::string label = pr.displayName + "##cel" + pr.id;
                    if (ImGui::Selectable(label.c_str(), selFlag))
                    {
                        m_selectedProfile = i;
                        syncEditBuffersFromSelection();
                    }
                }
                ImGui::EndChild();

                // ── 第二列：Tab 区域 ──────────────────────────────────────────
                ImGui::TableNextColumn();
                ImGui::BeginChild("##ce_tab_area", ImVec2(0.0f, 0.0f), false,
                    ImGuiWindowFlags_NoScrollbar);

                if (m_selectedProfile >= 0 && m_selectedProfile < static_cast<int>(m_profiles.size()))
                {
                    auto& p = m_profiles[static_cast<size_t>(m_selectedProfile)];
                    if (m_editingProfile != m_selectedProfile)
                        syncEditBuffersFromSelection();

                    // 根据贴图路径推导建议的默认保存路径（无文件时使用），与动画编辑器保持一致
                    auto texStem = [](const std::string& texPath) -> std::string {
                        if (texPath.empty()) return {};
                        return std::filesystem::path(texPath).stem().string();
                    };
                    const std::string stem = texStem(p.texturePath);
                    const std::string charDir = "assets/textures/Characters/";

                    const std::string suggestedFramePath = p.frameJsonPath.empty()
                        ? (stem.empty() ? charDir + p.id + ".json"
                                        : charDir + stem + ".json")
                        : p.frameJsonPath;
                    const std::string suggestedSmPath = p.stateMachineJsonPath.empty()
                        ? (stem.empty() ? charDir + p.id + ".sm.json"
                                        : charDir + stem + ".sm.json")
                        : p.stateMachineJsonPath;

                    auto bindEditorsFromBaseFields = [&]() {
                        const std::string framePath = m_editFrameJsonBuf.data();
                        const std::string texPath   = m_editTextureBuf.data();
                        const std::string smPath    = m_editSmJsonBuf.data();

                        if (framePath != m_boundFrameJsonPath || texPath != m_boundTexturePath)
                        {
                            m_boundFrameJsonPath = framePath;
                            m_boundTexturePath   = texPath;
                            m_frameEditor.openWithJson(framePath, texPath, suggestedFramePath);
                        }
                        if (smPath != m_boundSmJsonPath)
                        {
                            m_boundSmJsonPath = smPath;
                            m_smEditor.openWithJson(smPath, suggestedSmPath);
                        }
                    };

                    // 保存后自动回写路径到 profile
                    auto syncSavedPaths = [&]() {
                        if (m_frameEditor.popJustSaved())
                        {
                            const std::string sp = m_frameEditor.getSavePath();
                            p.frameJsonPath = sp;
                            std::snprintf(m_editFrameJsonBuf.data(), m_editFrameJsonBuf.size(), "%s", sp.c_str());
                            m_boundFrameJsonPath = sp;
                            saveCurrentProfile();
                        }
                        if (m_smEditor.popJustSaved())
                        {
                            const std::string sp = m_smEditor.getSavePath();
                            p.stateMachineJsonPath = sp;
                            std::snprintf(m_editSmJsonBuf.data(), m_editSmJsonBuf.size(), "%s", sp.c_str());
                            m_boundSmJsonPath = sp;
                            saveCurrentProfile();
                        }
                    };

                    if (ImGui::BeginTabBar("##ce_tabbar"))
                    {
                        // ── Tab 0: 基础 & 资源 (合并) ─────────────────────────
                        if (ImGui::BeginTabItem("基础 & 资源"))
                        {
                            m_activeTabIndex = 0;
                            m_hasUnsavedChanges |= ImGui::InputText("ID", m_editIdBuf.data(), m_editIdBuf.size());
                            m_hasUnsavedChanges |= ImGui::InputText("显示名", m_editNameBuf.data(), m_editNameBuf.size());
                            {
                                static const char* kRoleLabels[] = {"玩家", "机甲", "傀儡", "怪物"};
                                static const char* kRoleValues[] = {"player", "mech", "puppet", "monster"};
                                const std::string curRole = m_editMapRoleBuf.data();
                                int curIdx = 0;
                                for (int ri = 0; ri < 4; ++ri)
                                    if (curRole == kRoleValues[ri]) { curIdx = ri; break; }
                                if (ImGui::BeginCombo("角色标签##ce_role", kRoleLabels[curIdx]))
                                {
                                    for (int ri = 0; ri < 4; ++ri)
                                    {
                                        bool sel = (ri == curIdx);
                                        if (ImGui::Selectable(kRoleLabels[ri], sel))
                                        {
                                            std::snprintf(m_editMapRoleBuf.data(), m_editMapRoleBuf.size(), "%s", kRoleValues[ri]);
                                            m_hasUnsavedChanges = true;
                                        }
                                        if (sel) ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }
                            }
                            ImGui::Separator();

                            // 贴图字段 + 选择图片按钮
                            ImGui::TextDisabled("贴图路径");
                            ImGui::SetNextItemWidth(-108.0f);
                            if (ImGui::InputText("##ce_tex", m_editTextureBuf.data(), m_editTextureBuf.size()))
                                m_hasUnsavedChanges = true;
                            ImGui::SameLine();
                            if (ImGui::Button("选择图片##ce_pick", ImVec2(100.0f, 0.0f)))
                            {
                                scanTextureAssets();
                                ImGui::OpenPopup("选择角色贴图##apicker");
                            }
                            renderActorImagePicker(p);

                            ImGui::SetNextItemWidth(-1.0f);
                            m_hasUnsavedChanges |= ImGui::InputText("帧动画JSON", m_editFrameJsonBuf.data(), m_editFrameJsonBuf.size());
                            m_hasUnsavedChanges |= ImGui::InputText("状态机JSON", m_editSmJsonBuf.data(), m_editSmJsonBuf.size());

                            if (ImGui::Button("编辑帧动画##ce_go_fe", ImVec2(130.0f, 0.0f)))
                                m_requestedTab = 3;
                            ImGui::SameLine();
                            if (ImGui::Button("编辑状态机##ce_go_sm", ImVec2(130.0f, 0.0f)))
                                m_requestedTab = 4;

                            ImGui::SeparatorText("动画配置检查");
                            const std::string framePath = m_editFrameJsonBuf.data();
                            const std::string smPath    = m_editSmJsonBuf.data();
                            const std::string texPath   = m_editTextureBuf.data();
                            if (framePath.empty())
                                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.3f, 1.0f), "[缺失] 帧动画 JSON 路径为空");
                            else if (!std::filesystem::exists(framePath))
                                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "[异常] 帧动画 JSON 不存在: %s", framePath.c_str());
                            else
                                ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "[OK] 帧动画 JSON 可用");

                            if (smPath.empty())
                                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.3f, 1.0f), "[缺失] 状态机 JSON 路径为空");
                            else if (!std::filesystem::exists(smPath))
                                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "[异常] 状态机 JSON 不存在: %s", smPath.c_str());
                            else
                                ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "[OK] 状态机 JSON 可用");

                            if (texPath.empty())
                                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.3f, 1.0f), "[缺失] 贴图路径为空");
                            else if (!std::filesystem::exists(texPath))
                                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "[异常] 贴图文件不存在: %s", texPath.c_str());
                            else
                                ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "[OK] 贴图文件可用");

                            ImGui::Separator();
                            if (ImGui::Button("应用文本字段到当前角色", ImVec2(-1.0f, 28.0f)))
                            {
                                applyEditBuffersToSelection();
                                m_hasUnsavedChanges = true;
                                loadPreviewFromProfile(p);
                            }
                            ImGui::TextDisabled("文件: %s", p.filePath.c_str());
                            ImGui::EndTabItem();
                        }

                        // ── Tab 1: 碰撞 ────────────────────────────────────────
                        if (ImGui::BeginTabItem("碰撞"))
                        {
                            m_activeTabIndex = 1;
                            m_hasUnsavedChanges |= ImGui::DragFloat("碰撞半宽 (px)", &p.collisionHalfW, 0.1f, 1.0f, 200.0f, "%.1f");
                            m_hasUnsavedChanges |= ImGui::DragFloat("碰撞半深 (px)", &p.collisionHalfD, 0.1f, 0.5f, 100.0f, "%.1f");
                            m_hasUnsavedChanges |= ImGui::DragFloat("机甲高度 (px)", &p.mechHeightPx, 0.2f, 0.0f, 500.0f, "%.1f");
                            p.collisionHalfW = std::max(1.0f, p.collisionHalfW);
                            p.collisionHalfD = std::max(0.5f, p.collisionHalfD);
                            p.mechHeightPx   = std::max(0.0f, p.mechHeightPx);
                            ImGui::Spacing();
                            ImGui::TextDisabled("右侧实时显示碰撞盒、影子和机甲高度标注。");
                            ImGui::EndTabItem();
                        }

                        // ── Tab 2: 属性 ────────────────────────────────────────
                        if (ImGui::BeginTabItem("属性"))
                        {
                            m_activeTabIndex = 2;
                            m_hasUnsavedChanges |= ImGui::DragFloat("移动速度 (px/s)", &p.moveSpeed, 1.0f, 20.0f, 2000.0f, "%.1f");
                            m_hasUnsavedChanges |= ImGui::DragFloat("跳跃速度 (px/s)", &p.jumpVelocity, 1.0f, -2000.0f, -10.0f, "%.1f");
                            m_hasUnsavedChanges |= ImGui::InputInt("最大生命", &p.maxHp, 1, 10);
                            m_hasUnsavedChanges |= ImGui::InputInt("最大星能", &p.maxEnergy, 1, 10);
                            p.maxHp     = std::max(1, p.maxHp);
                            p.maxEnergy = std::max(1, p.maxEnergy);
                            ImGui::EndTabItem();
                        }

                        // ── Tab 3: 帧动画编辑器 ────────────────────────────────
                        {
                            const ImGuiTabItemFlags feFlags =
                                (m_requestedTab == 3) ? ImGuiTabItemFlags_SetSelected : 0;
                            if (m_requestedTab == 3) m_requestedTab = -1;
                            if (ImGui::BeginTabItem("帧动画编辑器", nullptr, feFlags))
                            {
                                m_activeTabIndex = 3;
                                bindEditorsFromBaseFields();
                                m_frameEditor.renderInline(_context.getResourceManager());
                                syncSavedPaths();
                                if (m_frameEditor.wantsSmEditor())
                                {
                                    m_frameEditor.clearSmEditorRequest();
                                    m_requestedTab = 4;
                                }
                                ImGui::EndTabItem();
                            }
                        }

                        // ── Tab 4: 状态机 ──────────────────────────────────────
                        {
                            const ImGuiTabItemFlags smFlags =
                                (m_requestedTab == 4) ? ImGuiTabItemFlags_SetSelected : 0;
                            if (m_requestedTab == 4) m_requestedTab = -1;
                            if (ImGui::BeginTabItem("状态机", nullptr, smFlags))
                            {
                                m_activeTabIndex = 4;
                                bindEditorsFromBaseFields();
                                m_smEditor.renderInline();
                                syncSavedPaths();
                                ImGui::EndTabItem();
                            }
                        }

                        ImGui::EndTabBar();
                    }
                }
                else
                {
                    ImGui::TextDisabled("请先在左侧选择角色，或点击上方\"新建角色\"按钮。");
                }

                ImGui::EndChild(); // ##ce_tab_area

                // ── 第三列：动画预览 ───────────────────────────────────────────
                ImGui::TableNextColumn();
                ImGui::BeginChild("##ce_preview_area", ImVec2(0.0f, 0.0f), false);
                if (m_selectedProfile >= 0 && m_selectedProfile < static_cast<int>(m_profiles.size()))
                    renderPreviewPanel(m_profiles[static_cast<size_t>(m_selectedProfile)]);
                ImGui::EndChild();

                ImGui::EndTable();
            }

            ImGui::End();
        }

    void CharacterEditorScene::renderSettings()
    {
        ImGui::SetNextWindowPos({12.0f, 138.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({430.0f, 150.0f}, ImGuiCond_Always);
        if (!ImGui::Begin("角色编辑器设置", &m_showSettings,
                          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove))
        {
            ImGui::End();
            return;
        }

        if (ImGui::Button("重扫角色文件", ImVec2(180, 34)))
            loadProfiles();
        ImGui::TextDisabled("角色文件目录: assets/characters/*.character.json");
        ImGui::TextDisabled("建议每个角色一个 JSON 文件，便于多人协作与后期维护。");

        ImGui::End();
    }

    void CharacterEditorScene::renderPerformanceOverlay() const
    {
        ImGui::SetNextWindowPos({10.0f, 10.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.45f);
        ImGui::Begin("##fps_character_editor", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::End();
    }

    void CharacterEditorScene::loadProfiles()
    {
        m_profiles.clear();
        m_selectedProfile = -1;
        m_hasUnsavedChanges = false;

        const std::filesystem::path dir{"assets/characters"};
        if (!std::filesystem::exists(dir))
            return;

        for (const auto& entry : std::filesystem::directory_iterator(dir))
        {
            if (!entry.is_regular_file())
                continue;
            if (entry.path().extension() != ".json")
                continue;
            if (entry.path().filename().string().find(".character.") == std::string::npos)
                continue;

            std::ifstream in(entry.path());
            if (!in.is_open())
                continue;

            nlohmann::json j;
            try { in >> j; }
            catch (...) { continue; }

            CharacterProfile p;
            p.id = j.value("id", entry.path().stem().string());
            p.displayName = j.value("display_name", p.id);
            p.frameJsonPath = j.value("frame_json", std::string{});
            p.stateMachineJsonPath = j.value("state_machine_json", std::string{});
            p.texturePath = j.value("texture", std::string{});
            p.mapRole = j.value("map_role", std::string{"npc"});
            if (j.contains("collision") && j["collision"].is_object())
            {
                const auto& c = j["collision"];
                p.collisionHalfW = c.value("half_w_px", p.collisionHalfW);
                p.collisionHalfD = c.value("half_d_px", p.collisionHalfD);
                p.mechHeightPx = c.value("mech_height_px", p.mechHeightPx);
            }
            if (j.contains("stats") && j["stats"].is_object())
            {
                const auto& s = j["stats"];
                p.moveSpeed = s.value("move_speed", p.moveSpeed);
                p.jumpVelocity = s.value("jump_velocity", p.jumpVelocity);
                p.maxHp = s.value("max_hp", p.maxHp);
                p.maxEnergy = s.value("max_energy", p.maxEnergy);
            }
            p.filePath = entry.path().generic_string();
            m_profiles.push_back(std::move(p));
        }

        std::sort(m_profiles.begin(), m_profiles.end(),
            [](const CharacterProfile& a, const CharacterProfile& b) { return a.id < b.id; });

        if (!m_profiles.empty())
        {
            m_selectedProfile = 0;
            syncEditBuffersFromSelection();
            loadPreviewFromProfile(m_profiles[0]);
        }
    }

    void CharacterEditorScene::syncEditBuffersFromSelection()
    {
        if (m_selectedProfile < 0 || m_selectedProfile >= static_cast<int>(m_profiles.size()))
            return;

        const auto& p = m_profiles[static_cast<size_t>(m_selectedProfile)];
        std::snprintf(m_editIdBuf.data(), m_editIdBuf.size(), "%s", p.id.c_str());
        std::snprintf(m_editNameBuf.data(), m_editNameBuf.size(), "%s", p.displayName.c_str());
        std::snprintf(m_editFrameJsonBuf.data(), m_editFrameJsonBuf.size(), "%s", p.frameJsonPath.c_str());
        std::snprintf(m_editSmJsonBuf.data(), m_editSmJsonBuf.size(), "%s", p.stateMachineJsonPath.c_str());
        std::snprintf(m_editTextureBuf.data(), m_editTextureBuf.size(), "%s", p.texturePath.c_str());
        std::snprintf(m_editMapRoleBuf.data(), m_editMapRoleBuf.size(), "%s", p.mapRole.c_str());
        m_editingProfile = m_selectedProfile;
        loadPreviewFromProfile(p);
    }

    void CharacterEditorScene::applyEditBuffersToSelection()
    {
        if (m_selectedProfile < 0 || m_selectedProfile >= static_cast<int>(m_profiles.size()))
            return;

        auto& p = m_profiles[static_cast<size_t>(m_selectedProfile)];
        p.id = m_editIdBuf.data();
        p.displayName = m_editNameBuf.data();
        p.frameJsonPath = m_editFrameJsonBuf.data();
        p.stateMachineJsonPath = m_editSmJsonBuf.data();
        p.texturePath = m_editTextureBuf.data();
        p.mapRole = m_editMapRoleBuf.data();
    }

    void CharacterEditorScene::loadPreviewFromProfile(const CharacterProfile& p)
    {
        m_previewActions.clear();
        m_previewActionIndex = 0;
        m_previewFrameIndex = 0;
        m_previewTimerSec = 0.0f;
        m_previewError.clear();
        m_previewLoadedFrameJson = p.frameJsonPath;
        m_previewTexturePath = p.texturePath;

        if (p.frameJsonPath.empty())
        {
            m_previewError = "未配置帧动画 JSON 路径";
            return;
        }

        std::ifstream in(p.frameJsonPath);
        if (!in.is_open())
        {
            m_previewError = "无法打开帧动画 JSON: " + p.frameJsonPath;
            return;
        }

        nlohmann::json j;
        try
        {
            in >> j;
        }
        catch (...)
        {
            m_previewError = "帧动画 JSON 解析失败";
            return;
        }

        if (j.contains("texture") && j["texture"].is_string())
            m_previewTexturePath = j["texture"].get<std::string>();

        if (!j.contains("actions") || !j["actions"].is_array())
        {
            m_previewError = "帧动画 JSON 缺少 actions";
            return;
        }

        for (const auto& a : j["actions"])
        {
            if (!a.is_object())
                continue;
            PreviewAction action;
            action.name = a.value("name", std::string{"Unnamed"});
            action.isLoop = a.value("is_loop", true);

            if (a.contains("frames") && a["frames"].is_array())
            {
                for (const auto& f : a["frames"])
                {
                    if (!f.is_object())
                        continue;
                    PreviewFrame frame;
                    frame.sx = f.value("sx", 0);
                    frame.sy = f.value("sy", 0);
                    frame.sw = std::max(1, f.value("sw", 64));
                    frame.sh = std::max(1, f.value("sh", 64));
                    frame.durationMs = std::max(16, f.value("duration_ms", 100));
                    frame.anchorX = f.value("anchor_x", frame.sw / 2);
                    frame.anchorY = f.value("anchor_y", frame.sh);
                    action.frames.push_back(frame);
                }
            }
            if (!action.frames.empty())
                m_previewActions.push_back(std::move(action));
        }

        if (m_previewActions.empty())
            m_previewError = "未找到可预览帧";
    }

    void CharacterEditorScene::scanTextureAssets()
    {
        m_textureAssets.clear();
        m_selectedTextureAsset = -1;

        const std::filesystem::path root{"assets/textures/Actors"};
        if (!std::filesystem::exists(root))
            return;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file())
                continue;
            if (!hasImageExt(entry.path()))
                continue;
            m_textureAssets.push_back(entry.path().generic_string());
        }

        std::sort(m_textureAssets.begin(), m_textureAssets.end());
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  右侧动画预览面板（上下文感知：帧编辑 tab 显示实时帧，碰撞 tab 高亮覆盖层）
    // ─────────────────────────────────────────────────────────────────────────
    void CharacterEditorScene::renderPreviewPanel(CharacterProfile& p)
    {
        ImGui::SeparatorText("动画演示");

        if (ImGui::Button("重载预览##pvr")) loadPreviewFromProfile(p);

        // 当预览源发生变化时自动重载
        if (m_previewLoadedFrameJson != p.frameJsonPath)
            loadPreviewFromProfile(p);

        // ── 确定当前帧来源 ─────────────────────────────────────────────────
        const bool useFrameEditorSrc = (m_activeTabIndex == 3);

        unsigned int glTex = 0;
        float texW = 0.0f, texH = 0.0f;
        int sx = 0, sy = 0, sw = 64, sh = 64;
        int anchorX = 32, anchorY = 56;
        std::string actionName;
        int frameIdx = 0, totalFrames = 1;
        bool hasSrc = false;

        if (useFrameEditorSrc)
        {
            const FrameEditor::FrameViewInfo fvi = m_frameEditor.peekCurrentFrame();
            if (fvi.valid)
            {
                glTex = fvi.glTex; texW = fvi.texW; texH = fvi.texH;
                sx = fvi.sx; sy = fvi.sy; sw = fvi.sw; sh = fvi.sh;
                anchorX = fvi.anchorX; anchorY = fvi.anchorY;
                actionName = "帧编辑器";
                hasSrc = true;
            }
        }

        if (!hasSrc)
        {
            // 从 m_previewActions 获取帧（带自动播放）
            if (!m_previewActions.empty())
            {
                if (m_previewActionIndex < 0 || m_previewActionIndex >= static_cast<int>(m_previewActions.size()))
                    m_previewActionIndex = 0;
                auto& action = m_previewActions[static_cast<size_t>(m_previewActionIndex)];
                if (m_previewFrameIndex < 0 || m_previewFrameIndex >= static_cast<int>(action.frames.size()))
                    m_previewFrameIndex = 0;

                // 推进动画
                if (!action.frames.empty())
                {
                    m_previewTimerSec += ImGui::GetIO().DeltaTime;
                    const float frameSec = static_cast<float>(
                        action.frames[static_cast<size_t>(m_previewFrameIndex)].durationMs) / 1000.0f;
                    if (m_previewTimerSec >= frameSec)
                    {
                        m_previewTimerSec = 0.0f;
                        ++m_previewFrameIndex;
                        if (m_previewFrameIndex >= static_cast<int>(action.frames.size()))
                            m_previewFrameIndex = action.isLoop ? 0 : static_cast<int>(action.frames.size()) - 1;
                    }
                    const auto& fr = action.frames[static_cast<size_t>(m_previewFrameIndex)];
                    sx = fr.sx; sy = fr.sy; sw = fr.sw; sh = fr.sh;
                    anchorX = fr.anchorX; anchorY = fr.anchorY;
                }

                actionName  = action.name;
                frameIdx    = m_previewFrameIndex;
                totalFrames = static_cast<int>(action.frames.size());

                if (!m_previewTexturePath.empty())
                {
                    glTex = _context.getResourceManager().getGLTexture(m_previewTexturePath);
                    const glm::vec2 ts = _context.getResourceManager().getTextureSize(m_previewTexturePath);
                    texW = ts.x; texH = ts.y;
                }
                hasSrc = true;
            }

            // 动作选择下拉（非帧编辑器 tab）
            if (m_previewActions.size() > 1)
            {
                const std::string curName = (m_previewActionIndex >= 0 &&
                    m_previewActionIndex < static_cast<int>(m_previewActions.size()))
                    ? m_previewActions[static_cast<size_t>(m_previewActionIndex)].name : "---";
                if (ImGui::BeginCombo("动作##pvr_act", curName.c_str(), ImGuiComboFlags_HeightSmall))
                {
                    for (int i = 0; i < static_cast<int>(m_previewActions.size()); ++i)
                    {
                        const bool sel = (i == m_previewActionIndex);
                        if (ImGui::Selectable(m_previewActions[static_cast<size_t>(i)].name.c_str(), sel))
                        {
                            m_previewActionIndex = i;
                            m_previewFrameIndex  = 0;
                            m_previewTimerSec    = 0.0f;
                        }
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            }
        }

        if (!m_previewError.empty() && !hasSrc)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", m_previewError.c_str());
            return;
        }

        if (glTex == 0 || texW <= 0.0f || texH <= 0.0f)
        {
            ImGui::TextDisabled("贴图不可用");
            return;
        }

        // ── 显示帧图像 ─────────────────────────────────────────────────────
        const float infoH    = ImGui::GetTextLineHeightWithSpacing() * 3.0f + 8.0f;
        const float displayH = std::max(64.0f, ImGui::GetContentRegionAvail().y - infoH);
        const float displayW = ImGui::GetContentRegionAvail().x;

        const float scaleX  = displayW / static_cast<float>(std::max(sw, 1));
        const float scaleY  = displayH / static_cast<float>(std::max(sh, 1));
        const float scale   = std::max(0.5f, std::min(scaleX, scaleY));
        const float dispW   = static_cast<float>(sw) * scale;
        const float dispH   = static_cast<float>(sh) * scale;
        const float xOff    = (displayW - dispW) * 0.5f;

        if (xOff > 0.0f)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + xOff);
        const ImVec2 imgPos = ImGui::GetCursorScreenPos();

        const float u0 = static_cast<float>(sx) / texW;
        const float v0 = static_cast<float>(sy) / texH;
        const float u1 = static_cast<float>(sx + sw) / texW;
        const float v1 = static_cast<float>(sy + sh) / texH;
        ImGui::Image((ImTextureID)(intptr_t)glTex, ImVec2(dispW, dispH), ImVec2(u0, v0), ImVec2(u1, v1));

        // ── DrawList 覆盖层 ────────────────────────────────────────────────
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // 锚点坐标（屏幕空间）
        const float asx = imgPos.x + static_cast<float>(anchorX) * scale;
        const float asy = imgPos.y + static_cast<float>(anchorY) * scale;

        // 脚下影子椭圆
        if (p.collisionHalfW > 0.0f && p.collisionHalfD > 0.0f)
        {
            dl->AddEllipseFilled(
                ImVec2(asx, asy),
                ImVec2(p.collisionHalfW * scale, p.collisionHalfD * scale),
                IM_COL32(0, 0, 0, 90));
        }

        // 碰撞盒
        if (p.mechHeightPx > 0.0f && p.collisionHalfW > 0.0f)
        {
            const bool hilit = (m_activeTabIndex == 1);  // 碰撞 tab
            const ImVec2 boxMin{asx - p.collisionHalfW * scale, asy - p.mechHeightPx * scale};
            const ImVec2 boxMax{asx + p.collisionHalfW * scale, asy};

            dl->AddRectFilled(boxMin, boxMax,
                hilit ? IM_COL32(0, 255, 80, 45) : IM_COL32(0, 200, 60, 20), 2.0f);
            dl->AddRect(boxMin, boxMax,
                hilit ? IM_COL32(0, 255, 80, 220) : IM_COL32(0, 200, 60, 100),
                2.0f, 0, hilit ? 2.0f : 1.0f);

            // 碰撞 tab：机甲高度顶线 + 标注
            if (hilit)
            {
                dl->AddLine(ImVec2(boxMin.x - 6.0f, boxMin.y),
                            ImVec2(boxMax.x + 6.0f, boxMin.y),
                            IM_COL32(255, 220, 0, 240), 2.0f);
                char htlbl[32];
                std::snprintf(htlbl, sizeof(htlbl), "%.1fpx", p.mechHeightPx);
                dl->AddText(ImVec2(boxMin.x, boxMin.y - 16.0f),
                            IM_COL32(255, 220, 0, 240), htlbl);
            }
        }

        // 锚点红点
        dl->AddCircleFilled(ImVec2(asx, asy), 3.0f, IM_COL32(255, 80, 80, 255));

        // ── 信息文字 ──────────────────────────────────────────────────────
        ImGui::TextDisabled("碰撞: %.0f×%.0fpx  深: %.1fpx  高: %.0fpx",
            p.collisionHalfW * 2.0f, p.mechHeightPx,
            p.collisionHalfD * 2.0f, p.mechHeightPx);
        if (!useFrameEditorSrc && !actionName.empty())
            ImGui::TextDisabled("%s  帧 %d/%d", actionName.c_str(), frameIdx + 1, totalFrames);
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  图片选择模态对话框（扫描 assets/textures/Actors/）
    // ─────────────────────────────────────────────────────────────────────────
    void CharacterEditorScene::renderActorImagePicker(CharacterProfile& p)
    {
        if (!ImGui::BeginPopupModal("选择角色贴图##apicker", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
            return;

        ImGui::TextUnformatted("从 assets/textures/Actors/ 选择贴图：");
        ImGui::TextDisabled("共 %d 个文件", static_cast<int>(m_textureAssets.size()));
        ImGui::SameLine();
        if (ImGui::Button("刷新##apicker")) scanTextureAssets();

        constexpr float kListH    = 280.0f;
        constexpr float kPreviewW = 200.0f;

        ImGui::BeginChild("##apicker_list", ImVec2(360.0f, kListH), ImGuiChildFlags_Borders);
        for (int i = 0; i < static_cast<int>(m_textureAssets.size()); ++i)
        {
            const bool sel = (i == m_selectedTextureAsset);
            if (ImGui::Selectable(m_textureAssets[static_cast<size_t>(i)].c_str(), sel))
                m_selectedTextureAsset = i;
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##apicker_preview", ImVec2(kPreviewW, kListH), ImGuiChildFlags_Borders);
        if (m_selectedTextureAsset >= 0 &&
            m_selectedTextureAsset < static_cast<int>(m_textureAssets.size()))
        {
            const std::string& picked = m_textureAssets[static_cast<size_t>(m_selectedTextureAsset)];
            const unsigned int gt = _context.getResourceManager().getGLTexture(picked);
            const glm::vec2 ts    = _context.getResourceManager().getTextureSize(picked);
            if (gt != 0 && ts.x > 0.0f)
            {
                const float pw = kPreviewW - 8.0f;
                const float ph = pw * ts.y / std::max(1.0f, ts.x);
                ImGui::Image((ImTextureID)(intptr_t)gt, ImVec2(pw, ph));
            }
            ImGui::TextWrapped("%s", picked.c_str());
            const glm::vec2 ts2 = _context.getResourceManager().getTextureSize(picked);
            ImGui::TextDisabled("%.0f × %.0f px", ts2.x, ts2.y);
        }
        else
        {
            ImGui::TextDisabled("未选择");
        }
        ImGui::EndChild();

        ImGui::Spacing();
        const bool hasSelection = (m_selectedTextureAsset >= 0 &&
            m_selectedTextureAsset < static_cast<int>(m_textureAssets.size()));
        if (!hasSelection) ImGui::BeginDisabled();
        if (ImGui::Button("选择此图片##apicker", ImVec2(160.0f, 0.0f)))
        {
            const std::string& picked = m_textureAssets[static_cast<size_t>(m_selectedTextureAsset)];
            std::snprintf(m_editTextureBuf.data(), m_editTextureBuf.size(), "%s", picked.c_str());
            applyEditBuffersToSelection();
            m_hasUnsavedChanges = true;
            loadPreviewFromProfile(p);
            ImGui::CloseCurrentPopup();
        }
        if (!hasSelection) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("取消##apicker", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    void CharacterEditorScene::createNewCharacterFile()
    {
        std::string id = m_newCharacterIdBuf.data();
        id.erase(std::remove_if(id.begin(), id.end(), [](unsigned char ch) {
            return std::isspace(ch) != 0;
        }), id.end());
        if (id.empty())
            id = "new_character";

        std::string displayName = m_newCharacterNameBuf.data();
        if (displayName.empty())
            displayName = id;

        const std::filesystem::path filePath = std::filesystem::path("assets/characters") / (id + ".character.json");
        if (std::filesystem::exists(filePath))
        {
            spdlog::warn("角色文件已存在: {}", filePath.string());
            return;
        }

        CharacterProfile p;
        p.id = id;
        p.displayName = displayName;
        p.frameJsonPath = "assets/textures/Characters/gundom.frame.json";
        p.stateMachineJsonPath = "assets/textures/Characters/gundom.sm.json";
        p.texturePath = "assets/textures/Characters/gundom.png";
        p.mapRole = "npc";
        p.filePath = filePath.generic_string();

        std::filesystem::create_directories(filePath.parent_path());
        nlohmann::json j;
        j["id"] = p.id;
        j["display_name"] = p.displayName;
        j["frame_json"] = p.frameJsonPath;
        j["state_machine_json"] = p.stateMachineJsonPath;
        j["texture"] = p.texturePath;
        j["map_role"] = p.mapRole;
        j["collision"] = {
            {"half_w_px", p.collisionHalfW},
            {"half_d_px", p.collisionHalfD},
            {"mech_height_px", p.mechHeightPx}
        };
        j["stats"] = {
            {"move_speed", p.moveSpeed},
            {"jump_velocity", p.jumpVelocity},
            {"max_hp", p.maxHp},
            {"max_energy", p.maxEnergy}
        };

        std::ofstream out(filePath);
        if (!out.is_open())
            return;
        out << j.dump(4);

        loadProfiles();
        for (int i = 0; i < static_cast<int>(m_profiles.size()); ++i)
        {
            if (m_profiles[static_cast<size_t>(i)].id == id)
            {
                m_selectedProfile = i;
                syncEditBuffersFromSelection();
                break;
            }
        }
        spdlog::info("已新建角色文件: {}", filePath.string());
    }

    void CharacterEditorScene::saveCurrentProfile()
    {
        if (m_selectedProfile < 0 || m_selectedProfile >= static_cast<int>(m_profiles.size()))
            return;

        // 先把文本缓冲提交，避免“改了文本但未点应用”导致保存遗漏。
        applyEditBuffersToSelection();

        auto& p = m_profiles[static_cast<size_t>(m_selectedProfile)];
        nlohmann::json j;
        j["id"] = p.id;
        j["display_name"] = p.displayName;
        j["frame_json"] = p.frameJsonPath;
        j["state_machine_json"] = p.stateMachineJsonPath;
        j["texture"] = p.texturePath;
        j["map_role"] = p.mapRole;
        j["collision"] = {
            {"half_w_px", p.collisionHalfW},
            {"half_d_px", p.collisionHalfD},
            {"mech_height_px", p.mechHeightPx}
        };
        j["stats"] = {
            {"move_speed", p.moveSpeed},
            {"jump_velocity", p.jumpVelocity},
            {"max_hp", p.maxHp},
            {"max_energy", p.maxEnergy}
        };

        std::filesystem::create_directories(std::filesystem::path(p.filePath).parent_path());
        std::ofstream out(p.filePath);
        if (!out.is_open())
            return;
        out << j.dump(4);

        // 保存后自动同步到 gameplay 当前角色配置（选中角色即当前运行角色）。
        {
            nlohmann::json cfg = nlohmann::json::object();
            {
                std::ifstream inCfg("assets/config.json");
                if (inCfg.is_open())
                {
                    try { inCfg >> cfg; }
                    catch (...) { cfg = nlohmann::json::object(); }
                }
            }
            if (!cfg.contains("gameplay") || !cfg["gameplay"].is_object())
                cfg["gameplay"] = nlohmann::json::object();

            cfg["gameplay"]["selected_character_profile_path"] = p.filePath;
            cfg["gameplay"]["player_frame_json_path"] = p.frameJsonPath;
            cfg["gameplay"]["player_sm_path"] = p.stateMachineJsonPath;
            cfg["gameplay"]["player_collision_half_w_px"] = p.collisionHalfW;
            cfg["gameplay"]["player_collision_half_d_px"] = p.collisionHalfD;
            cfg["gameplay"]["player_mech_height_px"] = p.mechHeightPx;
            cfg["gameplay"]["player_base_move_speed"] = std::max(1.0f, p.moveSpeed / 18.0f);
            cfg["gameplay"]["player_base_jump_speed"] = std::clamp(std::abs(p.jumpVelocity) / 52.5f, 2.0f, 24.0f);
            cfg["gameplay"]["player_base_max_hp"] = std::max(1, p.maxHp);
            cfg["gameplay"]["player_base_max_energy"] = std::max(1, p.maxEnergy);

            std::ofstream outCfg("assets/config.json");
            if (outCfg.is_open())
                outCfg << cfg.dump(4);
        }

        m_hasUnsavedChanges = false;
        spdlog::info("角色配置已保存: {}", p.filePath);
    }
} // namespace game::scene
