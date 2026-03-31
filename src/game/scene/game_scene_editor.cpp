#include "game_scene.h"

#include "../../engine/component/animation_component.h"
#include "../../engine/component/controller_component.h"
#include "../../engine/component/parallax_component.h"
#include "../../engine/component/physics_component.h"
#include "../../engine/component/sprite_component.h"
#include "../../engine/component/transform_component.h"
#include "../../engine/core/context.h"
#include "../../engine/object/game_object.h"
#include "../../engine/resource/resource_manager.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace game::scene
{
namespace
{
    constexpr int kDevThemeVarCount = 6;
    constexpr int kDevThemeColorCount = 10;

    void saveBoolSetting(const char* key, bool enabled)
    {
        nlohmann::json json = nlohmann::json::object();

        std::ifstream input("assets/settings.json");
        if (input.is_open())
        {
            try
            {
                input >> json;
            }
            catch (const std::exception&)
            {
                json = nlohmann::json::object();
            }
        }

        json[key] = enabled;

        std::ofstream output("assets/settings.json");
        if (!output.is_open())
            return;
        output << json.dump(4);
    }

    void pushDevEditorTheme()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 6.0f);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.09f, 0.12f, 0.96f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.12f, 0.16f, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.12f, 0.16f, 0.24f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.16f, 0.24f, 0.36f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.13f, 0.16f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.18f, 0.22f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.30f, 0.44f, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.40f, 0.58f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.27f, 0.38f, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.24f, 0.37f, 0.52f, 1.0f));
    }

    void popDevEditorTheme()
    {
        ImGui::PopStyleColor(kDevThemeColorCount);
        ImGui::PopStyleVar(kDevThemeVarCount);
    }

    void drawEditorSectionTitle(const char* title)
    {
        ImGui::SeparatorText(title);
    }

    void drawEditorKeyValue(const char* label, const char* value)
    {
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine();
        ImGui::TextUnformatted(value);
    }

    void drawEditorStatusChip(const char* text, const ImVec4& color)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
        ImGui::Button(text, ImVec2(88.0f, 0.0f));
        ImGui::PopStyleColor(3);
    }
}

void GameScene::setGameplayRunning(bool running)
{
    if (m_gameplayRunning == running)
        return;

    if (running && m_enablePlayRollback)
        capturePlaySnapshot();

    m_gameplayRunning = running;
    m_gameplayPaused = false;
    m_stepOneFrame = false;
    if (!m_gameplayRunning)
    {
        const bool restored = m_enablePlayRollback && restorePlaySnapshot();

        auto stopActorMotion = [](engine::object::GameObject* actor) {
            if (!actor)
                return;
            if (auto* physics = actor->getComponent<engine::component::PhysicsComponent>())
                physics->setVelocity({0.0f, 0.0f});
            if (auto* ctrl = actor->getComponent<engine::component::ControllerComponent>())
                ctrl->setRunMode(false);
        };

        if (!restored)
        {
            stopActorMotion(m_player);
            stopActorMotion(m_mech);
            stopActorMotion(m_possessedMonster);
        }

        m_isDashing = false;
        m_dashTimer = 0.0f;
        m_dashCooldown = 0.0f;
        shutdownFlightAmbientSound();
    }

    spdlog::info("编辑器播放状态切换: {}", m_gameplayRunning ? "运行" : "编辑");
}

void GameScene::capturePlaySnapshot()
{
    m_hasPlaySnapshot = false;
    m_playActorSnapshots.clear();
    m_playTileSnapshots.clear();

    if (!actor_manager)
        return;

    const auto& actors = actor_manager->getActors();
    m_playActorSnapshots.reserve(actors.size());
    for (size_t index = 0; index < actors.size(); ++index)
    {
        const auto* actor = actors[index].get();
        if (!actor)
            continue;

        ActorRuntimeSnapshot snapshot;
        snapshot.name = actor->getName();
        snapshot.tag = actor->getTag();
        snapshot.needRemove = actor->isNeedRemove();

        if (auto* transform = actor->getComponent<engine::component::TransformComponent>())
        {
            snapshot.hasTransform = true;
            snapshot.position = transform->getPosition();
            snapshot.scale = transform->getScale();
            snapshot.rotation = transform->getRotation();
        }
        if (auto* controller = actor->getComponent<engine::component::ControllerComponent>())
        {
            snapshot.hasController = true;
            snapshot.controllerSpeed = controller->getSpeed();
            snapshot.controllerEnabled = controller->isEnabled();
            snapshot.controllerRunMode = controller->isRunMode();
        }
        if (auto* physics = actor->getComponent<engine::component::PhysicsComponent>())
        {
            snapshot.hasPhysics = true;
            snapshot.physicsVelocity = physics->getVelocity();
            snapshot.physicsPosition = physics->getPosition();
        }
        if (auto* sprite = actor->getComponent<engine::component::SpriteComponent>())
        {
            snapshot.hasSprite = true;
            snapshot.spriteHidden = sprite->isHidden();
            snapshot.spriteFlipped = sprite->isFlipped();
        }
        if (auto* parallax = actor->getComponent<engine::component::ParallaxComponent>())
        {
            snapshot.hasParallax = true;
            snapshot.parallaxFactor = parallax->getScrollFactor();
            snapshot.parallaxRepeat = parallax->getRepeat();
            snapshot.parallaxHidden = parallax->isHidden();
        }

        m_playActorSnapshots.push_back(std::move(snapshot));
    }

    if (chunk_manager)
    {
        const auto tileSize = chunk_manager->getTileSize();
        const auto loadedBounds = chunk_manager->getLoadedChunkBounds();
        for (const auto& [worldPos, worldSize] : loadedBounds)
        {
            const int startX = static_cast<int>(std::floor(worldPos.x / std::max(1, tileSize.x)));
            const int startY = static_cast<int>(std::floor(worldPos.y / std::max(1, tileSize.y)));
            const int widthTiles = std::max(1, static_cast<int>(std::round(worldSize.x / std::max(1, tileSize.x))));
            const int heightTiles = std::max(1, static_cast<int>(std::round(worldSize.y / std::max(1, tileSize.y))));
            for (int y = 0; y < heightTiles; ++y)
            {
                for (int x = 0; x < widthTiles; ++x)
                {
                    TileRuntimeSnapshot tile;
                    tile.x = startX + x;
                    tile.y = startY + y;
                    tile.tile = chunk_manager->tileAt(tile.x, tile.y);
                    m_playTileSnapshots.push_back(tile);
                }
            }
        }
    }

    auto findActorIndex = [&](engine::object::GameObject* target) {
        if (!target || !actor_manager)
            return -1;
        const auto& refs = actor_manager->getActors();
        for (int i = 0; i < static_cast<int>(refs.size()); ++i)
        {
            if (refs[static_cast<size_t>(i)].get() == target)
                return i;
        }
        return -1;
    };

    m_snapshotPlayerIndex = findActorIndex(m_player);
    m_snapshotMechIndex = findActorIndex(m_mech);
    m_snapshotPossessedIndex = findActorIndex(m_possessedMonster);
    m_snapshotIsPlayerInMech = m_isPlayerInMech;
    m_snapshotCurrentZone = m_currentZone;

    m_playUiSnapshot.showInventory = m_showInventory;
    m_playUiSnapshot.showSettings = m_showSettings;
    m_playUiSnapshot.showMapEditor = m_showMapEditor;
    m_playUiSnapshot.showCommandInput = m_showCommandInput;
    m_playUiSnapshot.missionWindow = m_missionUI.showWindow;
    m_playUiSnapshot.showSettlement = m_showSettlement;
    m_playUiSnapshot.showHierarchyPanel = m_showHierarchyPanel;
    m_playUiSnapshot.showInspectorPanel = m_showInspectorPanel;
    m_playUiSnapshot.showFpsOverlay = m_showFpsOverlay;
    m_playUiSnapshot.devMode = m_devMode;
    m_playUiSnapshot.showSkillDebug = m_showSkillDebugOverlay;
    m_playUiSnapshot.showChunkHighlight = m_showActiveChunkHighlights;
    m_playUiSnapshot.selectedActorIndex = m_selectedActorIndex;
    m_playUiSnapshot.weaponActiveIndex = m_weaponBar.getActiveIndex();
    m_playUiSnapshot.inventory = m_inventory;
    m_playUiSnapshot.mechInventory = m_mechInventory;
    m_playUiSnapshot.equipmentLoadout = m_equipmentLoadout;
    m_playUiSnapshot.starSockets = m_starSockets;
    m_playUiSnapshot.skillCooldowns = m_skillCooldowns;
    m_playUiSnapshot.weaponBar = m_weaponBar;
    m_playTimeSnapshot = m_timeOfDaySystem.captureRuntimeState();
    m_playWeatherSnapshot = m_weatherSystem.captureRuntimeState();
    m_hasPlaySnapshot = true;
}

bool GameScene::restorePlaySnapshot()
{
    if (!m_hasPlaySnapshot || !actor_manager)
        return false;

    auto& actors = actor_manager->getActors();
    const int restoreCount = std::min(static_cast<int>(actors.size()), static_cast<int>(m_playActorSnapshots.size()));
    for (int i = 0; i < restoreCount; ++i)
    {
        auto* actor = actors[static_cast<size_t>(i)].get();
        if (!actor)
            continue;

        const auto& snapshot = m_playActorSnapshots[static_cast<size_t>(i)];
        actor->setName(snapshot.name);
        actor->setTag(snapshot.tag);
        actor->setNeedRemove(snapshot.needRemove);

        if (snapshot.hasTransform)
        {
            if (auto* transform = actor->getComponent<engine::component::TransformComponent>())
            {
                transform->setPosition(snapshot.position);
                transform->setScale(snapshot.scale);
                transform->setRotation(snapshot.rotation);
            }
        }
        if (snapshot.hasController)
        {
            if (auto* controller = actor->getComponent<engine::component::ControllerComponent>())
            {
                controller->setSpeed(snapshot.controllerSpeed);
                controller->setEnabled(snapshot.controllerEnabled);
                controller->setRunMode(snapshot.controllerRunMode);
            }
        }
        if (snapshot.hasPhysics)
        {
            if (auto* physics = actor->getComponent<engine::component::PhysicsComponent>())
            {
                physics->setWorldPosition(snapshot.physicsPosition);
                physics->setVelocity(snapshot.physicsVelocity);
            }
        }
        if (snapshot.hasSprite)
        {
            if (auto* sprite = actor->getComponent<engine::component::SpriteComponent>())
            {
                sprite->setHidden(snapshot.spriteHidden);
                sprite->setFlipped(snapshot.spriteFlipped);
            }
        }
        if (snapshot.hasParallax)
        {
            if (auto* parallax = actor->getComponent<engine::component::ParallaxComponent>())
            {
                parallax->setScrollFactor(snapshot.parallaxFactor);
                parallax->setRepeat(snapshot.parallaxRepeat);
                parallax->setHidden(snapshot.parallaxHidden);
            }
        }
    }

    for (int i = restoreCount; i < static_cast<int>(actors.size()); ++i)
    {
        if (auto* actor = actors[static_cast<size_t>(i)].get())
            actor->setNeedRemove(true);
    }

    if (chunk_manager)
    {
        for (const auto& tile : m_playTileSnapshots)
            chunk_manager->setTileSilent(tile.x, tile.y, tile.tile);
        chunk_manager->rebuildDirtyChunks();
    }

    auto actorByIndex = [&](int index) -> engine::object::GameObject* {
        if (index < 0 || index >= static_cast<int>(actors.size()))
            return nullptr;
        return actors[static_cast<size_t>(index)].get();
    };

    m_player = actorByIndex(m_snapshotPlayerIndex);
    m_mech = actorByIndex(m_snapshotMechIndex);
    m_possessedMonster = actorByIndex(m_snapshotPossessedIndex);
    m_isPlayerInMech = m_snapshotIsPlayerInMech;
    m_currentZone = m_snapshotCurrentZone;

    m_showInventory = m_playUiSnapshot.showInventory;
    m_showSettings = m_playUiSnapshot.showSettings;
    m_showMapEditor = m_playUiSnapshot.showMapEditor;
    m_showCommandInput = m_playUiSnapshot.showCommandInput;
    m_missionUI.showWindow = m_playUiSnapshot.missionWindow;
    m_showSettlement = m_playUiSnapshot.showSettlement;
    m_showHierarchyPanel = m_playUiSnapshot.showHierarchyPanel;
    m_showInspectorPanel = m_playUiSnapshot.showInspectorPanel;
    m_showFpsOverlay = m_playUiSnapshot.showFpsOverlay;
    m_devMode = m_playUiSnapshot.devMode;
    m_showSkillDebugOverlay = m_playUiSnapshot.showSkillDebug;
    m_showActiveChunkHighlights = m_playUiSnapshot.showChunkHighlight;
    m_selectedActorIndex = m_playUiSnapshot.selectedActorIndex;
    m_inventory = m_playUiSnapshot.inventory;
    m_mechInventory = m_playUiSnapshot.mechInventory;
    m_equipmentLoadout = m_playUiSnapshot.equipmentLoadout;
    m_starSockets = m_playUiSnapshot.starSockets;
    m_skillCooldowns = m_playUiSnapshot.skillCooldowns;
    m_weaponBar = m_playUiSnapshot.weaponBar;
    m_weaponBar.setActiveIndex(m_playUiSnapshot.weaponActiveIndex);
    m_timeOfDaySystem.restoreRuntimeState(m_playTimeSnapshot);
    m_weatherSystem.restoreRuntimeState(m_playWeatherSnapshot);
    m_hasPlaySnapshot = false;

    spdlog::info("编辑器回滚完成: actors={}, tiles={}", m_playActorSnapshots.size(), m_playTileSnapshots.size());
    return true;
}

void GameScene::renderEditorToolbar()
{
    if (!m_showEditorToolbar)
        return;

    pushDevEditorTheme();

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, 8.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    if (!ImGui::Begin("编辑器工具条", nullptr,
                      ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_AlwaysAutoResize |
                      ImGuiWindowFlags_NoMove))
    {
        ImGui::End();
        popDevEditorTheme();
        return;
    }

    bool persistUiState = false;
    const bool running = m_gameplayRunning;
    const bool paused = running && m_gameplayPaused;

    if (!running)
        drawEditorStatusChip("编辑模式", ImVec4(0.26f, 0.36f, 0.54f, 1.0f));
    else if (paused)
        drawEditorStatusChip("已暂停", ImVec4(0.64f, 0.48f, 0.16f, 1.0f));
    else
        drawEditorStatusChip("运行中", ImVec4(0.20f, 0.56f, 0.30f, 1.0f));

    ImGui::SameLine();
    ImGui::TextDisabled("F5 启动/停止  F6 暂停/继续  F10 单帧");

    if (m_toolbarShowPlayControls)
    {
        drawEditorSectionTitle("运行控制");
        ImVec4 playCol = running ? ImVec4(0.16f, 0.60f, 0.30f, 1.0f) : ImVec4(0.16f, 0.40f, 0.68f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, playCol);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(playCol.x + 0.08f, playCol.y + 0.08f, playCol.z + 0.08f, 1.0f));
        if (ImGui::Button(running ? "停止运行 [F5]" : "启动游戏 [F5]", ImVec2(132.0f, 0.0f)))
            setGameplayRunning(!running);
        ImGui::PopStyleColor(2);

        ImGui::SameLine();
        ImGui::BeginDisabled(!running);
        if (ImGui::Button(m_gameplayPaused ? "继续 [F6]" : "暂停 [F6]", ImVec2(96.0f, 0.0f)))
            m_gameplayPaused = !m_gameplayPaused;
        ImGui::SameLine();
        if (ImGui::Button("单步 [F10]", ImVec2(88.0f, 0.0f)) && m_gameplayPaused)
            m_stepOneFrame = true;
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("强制停止", ImVec2(88.0f, 0.0f)))
            setGameplayRunning(false);
    }

    if (m_toolbarShowWindowControls)
    {
        drawEditorSectionTitle("窗口开关");
        persistUiState |= ImGui::Checkbox("层级面板", &m_showHierarchyPanel);
        ImGui::SameLine();
        persistUiState |= ImGui::Checkbox("检视器", &m_showInspectorPanel);
        ImGui::SameLine();
        persistUiState |= ImGui::Checkbox("地图编辑", &m_showMapEditor);
        ImGui::SameLine();
        persistUiState |= ImGui::Checkbox("设置", &m_showSettings);
        persistUiState |= ImGui::Checkbox("开发覆盖", &m_devMode);
        ImGui::SameLine();
        persistUiState |= ImGui::Checkbox("性能浮层", &m_showFpsOverlay);
    }

    if (m_toolbarShowDebugControls)
    {
        drawEditorSectionTitle("调试摘要");
        if (ImGui::BeginTable("##editor_toolbar_debug", 3, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            ImGui::Text("选中对象");
            ImGui::TextDisabled("%d", m_selectedActorIndex);
            ImGui::TableNextColumn();
            ImGui::Text("对象总数");
            ImGui::TextDisabled("%zu", actor_manager ? actor_manager->actorCount() : 0);
            ImGui::TableNextColumn();
            ImGui::Text("回滚模式");
            ImGui::TextDisabled("%s", m_enablePlayRollback ? "退出运行时恢复" : "关闭");
            ImGui::EndTable();
        }
    }

    drawEditorSectionTitle("工具条模块");
    persistUiState |= ImGui::Checkbox("显示运行控制", &m_toolbarShowPlayControls);
    ImGui::SameLine();
    persistUiState |= ImGui::Checkbox("显示窗口开关", &m_toolbarShowWindowControls);
    ImGui::SameLine();
    persistUiState |= ImGui::Checkbox("显示调试摘要", &m_toolbarShowDebugControls);
    ImGui::SameLine();
    persistUiState |= ImGui::Checkbox("退出运行时回滚", &m_enablePlayRollback);

    if (persistUiState)
    {
        saveBoolSetting("show_hierarchy_panel", m_showHierarchyPanel);
        saveBoolSetting("show_inspector_panel", m_showInspectorPanel);
        saveBoolSetting("show_editor_toolbar", m_showEditorToolbar);
        saveBoolSetting("toolbar_show_play_controls", m_toolbarShowPlayControls);
        saveBoolSetting("toolbar_show_window_controls", m_toolbarShowWindowControls);
        saveBoolSetting("toolbar_show_debug_controls", m_toolbarShowDebugControls);
        saveBoolSetting("enable_play_rollback", m_enablePlayRollback);
    }

    ImGui::End();
    popDevEditorTheme();
}

void GameScene::renderHierarchyPanel()
{
    if (!m_showHierarchyPanel || !actor_manager)
        return;

    pushDevEditorTheme();
    pruneGroundSelection();

    const auto& actors = actor_manager->getActors();
    if (m_selectedActorIndex >= static_cast<int>(actors.size()))
        m_selectedActorIndex = actors.empty() ? -1 : 0;

    ImGui::SetNextWindowPos({16.0f, 36.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({332.0f, 520.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("对象层级", &m_showHierarchyPanel, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        popDevEditorTheme();
        return;
    }

    drawEditorSectionTitle("筛选");
    ImGui::Text("对象总数: %zu", actors.size());
    ImGui::SameLine();
    ImGui::TextDisabled("当前选中: %d", m_selectedActorIndex);

    ImGui::SetNextItemWidth(-112.0f);
    ImGui::InputTextWithHint("##hier_filter", "搜索名称/标签", m_hierarchyFilterBuffer.data(), m_hierarchyFilterBuffer.size());
    ImGui::SameLine();
    if (ImGui::Button("清空"))
        m_hierarchyFilterBuffer[0] = '\0';

    bool persistUiState = false;
    persistUiState |= ImGui::Checkbox("按标签分组", &m_hierarchyGroupByTag);
    ImGui::SameLine();
    persistUiState |= ImGui::Checkbox("仅看收藏", &m_hierarchyFavoritesOnly);
    if (persistUiState)
    {
        saveBoolSetting("hierarchy_group_by_tag", m_hierarchyGroupByTag);
        saveBoolSetting("hierarchy_favorites_only", m_hierarchyFavoritesOnly);
    }

    const std::string filterText(m_hierarchyFilterBuffer.data());
    auto containsInsensitive = [](const std::string& text, const std::string& key) {
        if (key.empty())
            return true;
        auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
        const auto it = std::search(text.begin(), text.end(), key.begin(), key.end(),
                                    [&](char a, char b) { return lower(static_cast<unsigned char>(a)) == lower(static_cast<unsigned char>(b)); });
        return it != text.end();
    };

    drawEditorSectionTitle("地面制作");
    static const char* kGroundTextureOptions[] = {
        "assets/textures/Props/platform-long.png",
        "assets/textures/Props/small-platform.png",
        "assets/textures/Props/block-big.png",
        "assets/textures/Props/block.png"
    };
    ImGui::InputText("名称##ground_name", m_groundMakerNameBuffer.data(), m_groundMakerNameBuffer.size());
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("地面素材", &m_groundMakerTextureIndex, kGroundTextureOptions, IM_ARRAYSIZE(kGroundTextureOptions));

    const char* groundTexturePath = kGroundTextureOptions[std::clamp(m_groundMakerTextureIndex, 0, static_cast<int>(IM_ARRAYSIZE(kGroundTextureOptions)) - 1)];
    const unsigned int groundTextureId = _context.getResourceManager().getGLTexture(groundTexturePath);
    const glm::vec2 groundTextureSize = _context.getResourceManager().getTextureSize(groundTexturePath);
    if (groundTextureId != 0 && groundTextureSize.x > 0.0f && groundTextureSize.y > 0.0f)
    {
        drawEditorSectionTitle("材质预览");
        const float previewMaxW = 240.0f;
        const float previewMaxH = 88.0f;
        const float ratio = std::min(previewMaxW / groundTextureSize.x, previewMaxH / groundTextureSize.y);
        const ImVec2 previewSize(groundTextureSize.x * ratio, groundTextureSize.y * ratio);
        ImGui::Image((ImTextureID)(intptr_t)groundTextureId, previewSize);
        ImGui::TextDisabled("%.0f x %.0f px", groundTextureSize.x, groundTextureSize.y);
    }
    else
    {
        ImGui::TextDisabled("材质预览不可用: %s", groundTexturePath);
    }

    float groundSpawnPos[2] = {m_groundMakerSpawnPos.x, m_groundMakerSpawnPos.y};
    if (ImGui::DragFloat2("生成位置", groundSpawnPos, 1.0f))
        m_groundMakerSpawnPos = snapGroundMakerPosition({groundSpawnPos[0], groundSpawnPos[1]});

    ImGui::Checkbox("网格吸附", &m_groundMakerUseGridSnap);
    ImGui::SameLine();
    ImGui::Checkbox("显示场景网格", &m_groundMakerShowGrid);
    if (m_groundMakerUseGridSnap)
    {
        ImGui::Checkbox("吸附 X", &m_groundMakerSnapX);
        ImGui::SameLine();
        ImGui::Checkbox("吸附 Y", &m_groundMakerSnapY);

        int gridSize[2] = {m_groundMakerGridSize.x, m_groundMakerGridSize.y};
        if (ImGui::InputInt2("网格尺寸", gridSize))
        {
            m_groundMakerGridSize.x = std::max(1, gridSize[0]);
            m_groundMakerGridSize.y = std::max(1, gridSize[1]);
            m_groundMakerSpawnPos = snapGroundMakerPosition(m_groundMakerSpawnPos);
        }
        if (chunk_manager)
        {
            ImGui::SameLine();
            if (ImGui::Button("使用瓦片网格"))
            {
                const auto tileSize = chunk_manager->getTileSize();
                m_groundMakerGridSize = {std::max(1, tileSize.x), std::max(1, tileSize.y)};
                m_groundMakerSpawnPos = snapGroundMakerPosition(m_groundMakerSpawnPos);
            }
        }
    }

    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("长度格数", &m_groundMakerLengthCells))
        m_groundMakerLengthCells = std::max(1, m_groundMakerLengthCells);
    ImGui::SameLine();
    ImGui::TextDisabled("宽度 %.0f px", groundMakerWidthFromCells());

    float scaleY = m_groundMakerScale.y;
    if (ImGui::DragFloat("垂直缩放", &scaleY, 0.05f, 0.1f, 20.0f))
        m_groundMakerScale.y = std::max(0.1f, scaleY);
    ImGui::DragFloat("旋转角度", &m_groundMakerRotation, 1.0f, -180.0f, 180.0f, "%.0f deg");
    ImGui::TextDisabled("旋转作用于精灵预览与对象变换，碰撞仍保持轴对齐");

    ImGui::Checkbox("创建默认碰撞体", &m_groundMakerUsePhysics);
    if (m_groundMakerUsePhysics)
    {
        float bodyHalfPx[2] = {m_groundMakerBodyHalfPx.x, m_groundMakerBodyHalfPx.y};
        if (ImGui::DragFloat2("碰撞半尺寸", bodyHalfPx, 1.0f, 1.0f, 512.0f))
        {
            m_groundMakerBodyHalfPx.x = std::max(1.0f, bodyHalfPx[0]);
            m_groundMakerBodyHalfPx.y = std::max(1.0f, bodyHalfPx[1]);
        }
    }

    ImGui::Checkbox("右键放置模式", &m_groundMakerPlaceMode);
    if (m_groundMakerPlaceMode)
        ImGui::TextDisabled("在场景里按住右键拖拽，可直接落点并按拖拽长度扩展宽度");

    if (m_hasHoveredTile)
    {
        ImGui::SameLine();
        if (ImGui::Button("使用悬停瓦片"))
            m_groundMakerSpawnPos = snapGroundMakerPosition(m_lastHoveredTileCenter);
    }

    if (ImGui::Button("创建地面对象", ImVec2(-1.0f, 0.0f)))
        createGroundActor();

    drawEditorSectionTitle("地面批量操作");
    ImGui::TextDisabled("已框选地面: %zu  |  Shift + 左键框选", m_groundSelection.size());
    if (m_groundConfigDirty)
        drawEditorStatusChip("待保存", ImVec4(0.68f, 0.48f, 0.16f, 1.0f));
    else
        drawEditorStatusChip("已同步", ImVec4(0.20f, 0.52f, 0.28f, 1.0f));

    if (ImGui::Button("对齐框选到网格", ImVec2(-1.0f, 0.0f)))
        snapSelectedGroundActorsToGrid();
    if (ImGui::Button("保存地面到关卡", ImVec2(-1.0f, 0.0f)))
        saveGroundActorsToConfig();
    if (ImGui::Button("重载已保存地面", ImVec2(-1.0f, 0.0f)))
        loadGroundActorsFromConfig(true);
    if (ImGui::Button("清空框选", ImVec2(-1.0f, 0.0f)))
    {
        m_groundSelection.clear();
        m_selectedActorIndex = -1;
    }

    drawEditorSectionTitle("对象列表");
    ImGui::BeginChild("##hierarchy_actor_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    auto drawActorItem = [&](int index) {
        const auto* actor = actors[static_cast<size_t>(index)].get();
        if (!actor)
            return;
        if (m_hierarchyFavoritesOnly && !m_hierarchyFavorites.contains(actor))
            return;

        const std::string name = actor->getName().empty() ? "<unnamed>" : actor->getName();
        const std::string tag = actor->getTag();
        if (!containsInsensitive(name, filterText) && !containsInsensitive(tag, filterText))
            return;

        const bool selected = (m_selectedActorIndex == index) || m_groundSelection.contains(actor);
        std::string label = name;
        if (!tag.empty() && tag != "未定义的标签")
            label += " [" + tag + "]";
        if (actor->isNeedRemove())
            label += " [PendingDelete]";

        const bool favorite = m_hierarchyFavorites.contains(actor);
        const std::string rowId = "##actor_row_" + std::to_string(index);
        if (!ImGui::BeginTable(rowId.c_str(), 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
            return;

        ImGui::TableSetupColumn("fav", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        const std::string buttonLabel = std::string(favorite ? "★" : "☆") + "##fav_" + std::to_string(index);
        if (ImGui::SmallButton(buttonLabel.c_str()))
        {
            if (favorite)
                m_hierarchyFavorites.erase(actor);
            else
                m_hierarchyFavorites.insert(actor);
        }

        ImGui::TableSetColumnIndex(1);
        if (ImGui::Selectable((label + "##actor_" + std::to_string(index)).c_str(), selected))
        {
            m_selectedActorIndex = index;
            m_groundSelection.clear();
            if (isGroundActor(actor))
                m_groundSelection.insert(actor);
        }

        ImGui::EndTable();
    };

    if (!m_hierarchyGroupByTag)
    {
        for (int index = 0; index < static_cast<int>(actors.size()); ++index)
            drawActorItem(index);
    }
    else
    {
        std::vector<std::string> tagOrder;
        tagOrder.reserve(actors.size());
        for (const auto& holder : actors)
        {
            if (!holder)
                continue;
            std::string tag = holder->getTag();
            if (tag.empty() || tag == "未定义的标签")
                tag = "(untagged)";
            if (std::find(tagOrder.begin(), tagOrder.end(), tag) == tagOrder.end())
                tagOrder.push_back(tag);
        }

        for (const auto& tag : tagOrder)
        {
            if (!ImGui::CollapsingHeader(tag.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                continue;
            for (int index = 0; index < static_cast<int>(actors.size()); ++index)
            {
                const auto* actor = actors[static_cast<size_t>(index)].get();
                if (!actor)
                    continue;
                std::string actorTag = actor->getTag();
                if (actorTag.empty() || actorTag == "未定义的标签")
                    actorTag = "(untagged)";
                if (actorTag != tag)
                    continue;
                drawActorItem(index);
            }
        }
    }

    ImGui::EndChild();

    ImGui::End();
    popDevEditorTheme();
}

void GameScene::createGroundActor()
{
    if (!actor_manager)
        return;

    static const char* kGroundTextureOptions[] = {
        "assets/textures/Props/platform-long.png",
        "assets/textures/Props/small-platform.png",
        "assets/textures/Props/block-big.png",
        "assets/textures/Props/block.png"
    };

    m_groundMakerSpawnPos = snapGroundMakerPosition(m_groundMakerSpawnPos);
    const int textureIndex = std::clamp(m_groundMakerTextureIndex, 0, static_cast<int>(IM_ARRAYSIZE(kGroundTextureOptions)) - 1);
    const std::string actorName = m_groundMakerNameBuffer[0] ? m_groundMakerNameBuffer.data() : "ground_platform";
    const float targetWidthPx = groundMakerWidthFromCells();
    m_groundMakerScale.x = std::max(0.1f, targetWidthPx / 96.0f);

    auto* ground = actor_manager->createActor(actorName);
    if (!ground)
        return;

    ground->setTag("Ground");
    auto* transform = ground->addComponent<engine::component::TransformComponent>(m_groundMakerSpawnPos, m_groundMakerScale, m_groundMakerRotation);
    transform->setRotation(m_groundMakerRotation);
    ground->addComponent<engine::component::SpriteComponent>(kGroundTextureOptions[textureIndex], engine::utils::Alignment::CENTER);

    if (m_groundMakerUsePhysics && physics_manager)
    {
        constexpr float kPixelsPerMeter = 32.0f;
        const glm::vec2 bodyHalfPx = {
            std::max(1.0f, m_groundMakerBodyHalfPx.x * m_groundMakerScale.x),
            std::max(1.0f, m_groundMakerBodyHalfPx.y * m_groundMakerScale.y)
        };
        const glm::vec2 bodyHalfMeters = bodyHalfPx / kPixelsPerMeter;
        b2BodyId bodyId = physics_manager->createStaticBody(
            {m_groundMakerSpawnPos.x / kPixelsPerMeter, m_groundMakerSpawnPos.y / kPixelsPerMeter},
            {bodyHalfMeters.x, bodyHalfMeters.y},
            ground);
        ground->addComponent<engine::component::PhysicsComponent>(bodyId, physics_manager.get());
        m_groundColliderHalfByActor[ground] = bodyHalfPx;
    }
    else
    {
        m_groundColliderHalfByActor[ground] = {
            std::max(8.0f, m_groundMakerBodyHalfPx.x * m_groundMakerScale.x),
            std::max(8.0f, m_groundMakerBodyHalfPx.y * m_groundMakerScale.y)
        };
    }

    m_selectedActorIndex = static_cast<int>(actor_manager->actorCount()) - 1;
    m_inspectorRenameBufferActorIndex = -1;
    m_groundSelection.clear();
    m_groundSelection.insert(ground);
    m_groundConfigDirty = true;
    spdlog::info("编辑器创建地面对象: name='{}' texture='{}' pos=({}, {}) rot={}",
                 actorName,
                 kGroundTextureOptions[textureIndex],
                 m_groundMakerSpawnPos.x,
                 m_groundMakerSpawnPos.y,
                 m_groundMakerRotation);
}

glm::vec2 GameScene::snapGroundMakerPosition(glm::vec2 worldPos) const
{
    if (!m_groundMakerUseGridSnap)
        return worldPos;

    const float gridX = static_cast<float>(std::max(1, m_groundMakerGridSize.x));
    const float gridY = static_cast<float>(std::max(1, m_groundMakerGridSize.y));
    return {
        m_groundMakerSnapX ? std::round(worldPos.x / gridX) * gridX : worldPos.x,
        m_groundMakerSnapY ? std::round(worldPos.y / gridY) * gridY : worldPos.y
    };
}

float GameScene::snapGroundMakerWidth(float widthPx) const
{
    if (!m_groundMakerUseGridSnap)
        return widthPx;

    const float gridX = static_cast<float>(std::max(1, m_groundMakerGridSize.x));
    return std::max(gridX, std::round(widthPx / gridX) * gridX);
}

float GameScene::groundMakerWidthFromCells() const
{
    const float gridX = static_cast<float>(std::max(1, m_groundMakerGridSize.x));
    return std::max(gridX, static_cast<float>(std::max(1, m_groundMakerLengthCells)) * gridX);
}

void GameScene::renderInspectorPanel()
{
    if (!m_showInspectorPanel || !actor_manager)
        return;

    const auto& actors = actor_manager->getActors();
    if (m_selectedActorIndex < 0 || m_selectedActorIndex >= static_cast<int>(actors.size()))
        return;

    auto* actor = actors[static_cast<size_t>(m_selectedActorIndex)].get();
    if (!actor)
        return;

    pushDevEditorTheme();

    ImGui::SetNextWindowPos({360.0f, 36.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({360.0f, 520.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("对象检视器", &m_showInspectorPanel, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        popDevEditorTheme();
        return;
    }

    std::string actorName = actor->getName();
    if (actorName.empty())
        actorName = "<unnamed>";
    const bool isGround = isGroundActor(actor);
    if (m_inspectorRenameBufferActorIndex != m_selectedActorIndex)
    {
        snprintf(m_inspectorRenameBuffer.data(), m_inspectorRenameBuffer.size(), "%s", actorName.c_str());
        snprintf(m_inspectorTagBuffer.data(), m_inspectorTagBuffer.size(), "%s", actor->getTag().c_str());
        m_inspectorRenameBufferActorIndex = m_selectedActorIndex;
    }

    drawEditorSectionTitle("基本信息");
    drawEditorKeyValue("对象名称:", actorName.c_str());
    ImGui::TextDisabled("对象索引: %d", m_selectedActorIndex);
    ImGui::InputText("名称", m_inspectorRenameBuffer.data(), m_inspectorRenameBuffer.size());
    ImGui::InputText("标签", m_inspectorTagBuffer.data(), m_inspectorTagBuffer.size());
    if (ImGui::Button("应用名称与标签", ImVec2(-120.0f, 0.0f)))
    {
        actor->setName(std::string(m_inspectorRenameBuffer.data()));
        actor->setTag(std::string(m_inspectorTagBuffer.data()));
        if (isGround)
            m_groundConfigDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("标记删除", ImVec2(-1.0f, 0.0f)))
    {
        actor->setNeedRemove(true);
        m_groundSelection.erase(actor);
        if (isGround)
            m_groundConfigDirty = true;
    }

    if (isGround)
    {
        ImGui::TextDisabled("Ground 已接入关卡保存，可在层级面板执行保存/重载。");
    }

    drawEditorSectionTitle("组件管理");
    const bool isCoreControlled = (actor == m_player || actor == m_mech || actor == m_possessedMonster);
    if (ImGui::BeginTable("##component_controls", 2, ImGuiTableFlags_SizingStretchSame))
    {
        auto drawComponentToggle = [&](const char* addLabel,
                                       const char* removeLabel,
                                       bool hasComponent,
                                       auto addFn,
                                       auto removeFn) {
            ImGui::TableNextColumn();
            if (!hasComponent)
            {
                if (ImGui::Button(addLabel, ImVec2(-1.0f, 0.0f)))
                    addFn();
            }
            else
            {
                ImGui::BeginDisabled(isCoreControlled);
                if (ImGui::Button(removeLabel, ImVec2(-1.0f, 0.0f)))
                    removeFn();
                ImGui::EndDisabled();
            }
        };

        drawComponentToggle("添加 Transform", "移除 Transform",
                            actor->hasComponent<engine::component::TransformComponent>(),
                            [&] { actor->addComponent<engine::component::TransformComponent>(glm::vec2{0.0f, 56.0f}); },
                            [&] { actor->removeComponent<engine::component::TransformComponent>(); });
        drawComponentToggle("添加 Controller", "移除 Controller",
                            actor->hasComponent<engine::component::ControllerComponent>(),
                            [&] { actor->addComponent<engine::component::ControllerComponent>(15.0f, 20.0f); },
                            [&] { actor->removeComponent<engine::component::ControllerComponent>(); });
        drawComponentToggle("添加 Sprite", "移除 Sprite",
                            actor->hasComponent<engine::component::SpriteComponent>(),
                            [&] { actor->addComponent<engine::component::SpriteComponent>("assets/textures/Props/bubble1.svg", engine::utils::Alignment::CENTER); },
                            [&] { actor->removeComponent<engine::component::SpriteComponent>(); });
        drawComponentToggle("添加 Animation", "移除 Animation",
                            actor->hasComponent<engine::component::AnimationComponent>(),
                            [&] { actor->addComponent<engine::component::AnimationComponent>(32.0f, 32.0f); },
                            [&] { actor->removeComponent<engine::component::AnimationComponent>(); });
        drawComponentToggle("添加 Parallax", "移除 Parallax",
                            actor->hasComponent<engine::component::ParallaxComponent>(),
                            [&] { actor->addComponent<engine::component::ParallaxComponent>("assets/textures/Layers/back.png", glm::vec2{0.35f, 0.25f}); },
                            [&] { actor->removeComponent<engine::component::ParallaxComponent>(); });
        ImGui::EndTable();
    }
    ImGui::TextDisabled(isCoreControlled ? "核心角色已保护关键组件" : "可增删基础组件");

    if (auto* transform = actor->getComponent<engine::component::TransformComponent>())
    {
        drawEditorSectionTitle("Transform");
        glm::vec2 position = transform->getPosition();
        glm::vec2 scale = transform->getScale();
        float rotation = transform->getRotation();
        float positionArray[2] = {position.x, position.y};
        float scaleArray[2] = {scale.x, scale.y};
        auto* physics = actor->getComponent<engine::component::PhysicsComponent>();
        if (ImGui::DragFloat2("Position", positionArray, 0.5f))
        {
            transform->setPosition({positionArray[0], positionArray[1]});
            if (physics)
                physics->setWorldPosition({positionArray[0], positionArray[1]});
            if (isGround)
                m_groundConfigDirty = true;
        }
        if (ImGui::DragFloat2("Scale", scaleArray, 0.01f, 0.01f, 20.0f))
        {
            transform->setScale({scaleArray[0], scaleArray[1]});
            if (isGround)
                m_groundConfigDirty = true;
        }
        if (ImGui::DragFloat("Rotation", &rotation, 0.5f, -360.0f, 360.0f))
        {
            transform->setRotation(rotation);
            if (isGround)
                m_groundConfigDirty = true;
        }
    }

    if (auto* controller = actor->getComponent<engine::component::ControllerComponent>())
    {
        drawEditorSectionTitle("Controller");
        float speed = controller->getSpeed();
        if (ImGui::SliderFloat("MoveSpeed", &speed, 1.0f, 80.0f))
            controller->setSpeed(speed);
        bool enabled = controller->isEnabled();
        if (ImGui::Checkbox("Enabled", &enabled))
            controller->setEnabled(enabled);
        ImGui::TextDisabled("State: %s", controller->getMovementStateName());
        ImGui::TextDisabled("FlyMode: %s", controller->isFlyModeActive() ? "true" : "false");
    }

    if (auto* physics = actor->getComponent<engine::component::PhysicsComponent>())
    {
        drawEditorSectionTitle("Physics");
        const glm::vec2 velocity = physics->getVelocity();
        ImGui::TextDisabled("Velocity: (%.2f, %.2f)", velocity.x, velocity.y);
        if (ImGui::Button("Velocity=0"))
            physics->setVelocity({0.0f, 0.0f});
    }

    if (auto* sprite = actor->getComponent<engine::component::SpriteComponent>())
    {
        drawEditorSectionTitle("Sprite");
        bool hidden = sprite->isHidden();
        bool flipped = sprite->isFlipped();
        if (ImGui::Checkbox("Hidden", &hidden))
            sprite->setHidden(hidden);
        ImGui::SameLine();
        if (ImGui::Checkbox("Flipped", &flipped))
            sprite->setFlipped(flipped);
        ImGui::TextDisabled("Texture: %s", sprite->getTextureId().c_str());
    }

    if (auto* animation = actor->getComponent<engine::component::AnimationComponent>())
    {
        drawEditorSectionTitle("Animation");
        ImGui::TextDisabled("Clip: %s", animation->currentClip().empty() ? "<none>" : animation->currentClip().c_str());
        ImGui::TextDisabled("Frame: %d  Timer: %.3f", animation->currentFrame(), animation->currentTimer());
    }

    if (auto* parallax = actor->getComponent<engine::component::ParallaxComponent>())
    {
        drawEditorSectionTitle("Parallax");
        glm::vec2 factor = parallax->getScrollFactor();
        float factorArray[2] = {factor.x, factor.y};
        if (ImGui::DragFloat2("Factor", factorArray, 0.01f, -4.0f, 4.0f))
            parallax->setScrollFactor({factorArray[0], factorArray[1]});

        glm::bvec2 repeat = parallax->getRepeat();
        bool repeatX = repeat.x;
        bool repeatY = repeat.y;
        const bool changedX = ImGui::Checkbox("Repeat X", &repeatX);
        const bool changedY = ImGui::Checkbox("Repeat Y", &repeatY);
        if (changedX || changedY)
            parallax->setRepeat({repeatX, repeatY});

        bool hidden = parallax->isHidden();
        if (ImGui::Checkbox("Parallax Hidden", &hidden))
            parallax->setHidden(hidden);
    }

    drawEditorSectionTitle("对象身份");
    if (actor == m_player)
        ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.55f, 1.0f), "[Player]");
    else if (actor == m_mech)
        ImGui::TextColored(ImVec4(0.50f, 0.80f, 1.0f, 1.0f), "[Mech]");
    else if (actor == m_possessedMonster)
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1.0f), "[PossessedMonster]");
    else
        ImGui::TextDisabled("普通对象");

    ImGui::End();
    popDevEditorTheme();
}

void GameScene::renderMapEditor()
{
    if (!m_showMapEditor || !chunk_manager)
        return;

    pushDevEditorTheme();

    const char* tileNames[] = {
        "Air", "Stone", "Dirt", "Grass", "Wood",
        "Leaves", "Ore", "Gravel", "GroundDecor", "WallDecor"
    };

    int tileIndex = static_cast<int>(m_mapEditorPaintTile);

    ImGui::SetNextWindowPos({16.0f, 140.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({308.0f, 300.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("地图编辑器 [F8]", &m_showMapEditor,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        popDevEditorTheme();
        return;
    }

    drawEditorSectionTitle("笔刷");
    ImGui::TextUnformatted("左键绘制，右键擦除，笔刷范围为圆形。");

    if (ImGui::Combo("瓦片类型", &tileIndex, tileNames, IM_ARRAYSIZE(tileNames)))
        m_mapEditorPaintTile = static_cast<engine::world::TileType>(tileIndex);

    ImGui::SliderInt("笔刷半径", &m_mapEditorBrushRadius, 0, 4, "%d");

    if (m_hasHoveredTile)
    {
        const auto currentType = chunk_manager->tileAt(m_hoveredTile.x, m_hoveredTile.y).type;
        drawEditorSectionTitle("悬停信息");
        ImGui::Text("悬停格: (%d, %d)", m_hoveredTile.x, m_hoveredTile.y);
        ImGui::Text("当前瓦片: %s", tileNames[static_cast<int>(currentType)]);
    }

    drawEditorSectionTitle("操作");
    if (ImGui::Button("重建全部脏区块"))
        chunk_manager->rebuildDirtyChunks();

    ImGui::End();
    popDevEditorTheme();
}
} // namespace game::scene
