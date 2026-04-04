#pragma once
#include "frame_editor.h"
#include "state_machine_editor.h"
#include "universe_editor.h"
#include "../../engine/statemachine/state_controller.h"
#include "../../engine/scene/scene.h"
#include "../../engine/world/chunk_manager.h"
#include "../../engine/world/world_config.h"
#include "../../engine/physics/physics_manager.h"
#include "../../engine/actor/actor_manager.h"
#include "../../engine/render/text_renderer.h"
#include "../../engine/ecs/registry.h"
#include "../inventory/inventory.h"
#include "../weapon/weapon.h"
#include "../monster/monster_manager.h"
#include "../world/tree_manager.h"
#include "../world/time_of_day_system.h"
#include "../weather/weather_system.h"
#include "../mission/planet_mission_ui.h"
#include "../route/route_data.h"
#include "../skill/star_skill.h"
#include "../world/ground_tile_catalog.h"
#include "../component/attribute_component.h"
#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace engine::object
{
    class GameObject;
}

struct MIX_Track;

namespace game::scene
{
    struct PerfMetric
    {
        float lastMs = 0.0f;
        float avgMs = 0.0f;
        float peakMs = 0.0f;
    };

    struct FrameProfiler
    {
        PerfMetric updateTotal;
        PerfMetric coreLogic;
        PerfMetric monsterUpdate;
        PerfMetric cameraUpdate;
        PerfMetric physicsUpdate;
        PerfMetric actorUpdate;
        PerfMetric stateMachineUpdate;
        PerfMetric dropUpdate;
        PerfMetric weatherUpdate;
        PerfMetric missionUpdate;
        PerfMetric chunkStreamUpdate;
        PerfMetric renderTotal;
        PerfMetric backgroundRender;
        PerfMetric sceneRender;
        PerfMetric skyBackgroundRender;
        PerfMetric chunkRender;
        PerfMetric parallaxRender;
        PerfMetric spriteRender;
        PerfMetric tileRender;
        PerfMetric shadowRender;
        PerfMetric actorRender;
        PerfMetric lightingRender;
        PerfMetric imguiRender;
        float frameDeltaMs = 0.0f;
        size_t loadedChunks = 0;
        size_t pendingChunkLoads = 0;
    };

    class GameScene : public engine::scene::Scene
    {
    public:
        GameScene(const std::string &name,
                  engine::core::Context &context,
                  engine::scene::SceneManager &sceneManager,
                  game::route::RouteData routeData = {},
                  bool startInMapEditor = false);

        void init() override;
        void update(float delta_time) override;
        void render() override;
        void handleInput() override;
        void clean() override;

    private:
        std::unique_ptr<engine::world::ChunkManager> chunk_manager;
        std::unique_ptr<engine::physics::PhysicsManager> physics_manager;
        std::unique_ptr<engine::actor::ActorManager> actor_manager;
        engine::render::TextRenderer* text_renderer = nullptr;
        engine::ecs::Registry ecs_registry;

        engine::object::GameObject* m_player = nullptr;

        SDL_GPUTexture* m_textTexture = nullptr;
        SDL_GLContext m_glContext = nullptr;

        // 缩放滑块
        float m_zoomSliderValue = 9.0f;   // DNF 默认缩放

        // ── 设置界面：内存历史折线图 ──────────────────────────────────────────
        static constexpr int kMemHistoryLen = 128;   // 环形缓冲长度（帧/采样点）
        std::array<float, kMemHistoryLen> m_rssHistory{};   // RSS（MB）历史
        int   m_rssHistoryIdx   = 0;         // 下一写入位置
        float m_rssHistoryTimer = 0.0f;      // 距上次采样经过秒数
        float m_rssPeakMB       = 0.0f;      // 历史峰值

        // ── 设置界面：FPS 历史折线图 ──────────────────────────────────────────
        std::array<float, kMemHistoryLen> m_fpsHistory{};  // 实时 FPS 历史
        int   m_fpsHistoryIdx   = 0;         // 下一写入位置
        float m_fpsHistoryTimer = 0.0f;      // 距上次采样经过秒数（每 0.25s 一次）
        float m_fpsPeak         = 0.0f;      // 历史峰值
        int   m_maxFpsSlider    = 60;        // 目标帧率设置（0 = 不限）
        FrameProfiler m_frameProfiler;

        // ── 设置界面：粒子效果档位 ────────────────────────────────────────────
        enum class UiParticleLevel { None = 0, Low, Medium, High };
        UiParticleLevel m_uiParticleLevel = UiParticleLevel::Low;

        // 设置界面背景漂浮尘埃粒子
        struct UiDust {
            float x, y;           // 屏幕归一化坐标 [0,1]
            float vy;             // 上升速度（归一化/秒）
            float alpha;          // 当前透明度
            float size;           // 像素大小
            uint8_t r, g, b;      // 颜色
        };
        std::vector<UiDust> m_uiDusts;
        glm::vec2 m_lastChunkUpdatePos = {-99999.0f, -99999.0f};
        glm::vec2 m_lastMouseLogicalPos = {0.0f, 0.0f};
        glm::vec2 m_lastMouseWorldPos = {0.0f, 0.0f};
        glm::vec2 m_lastHoveredTileCenter = {0.0f, 0.0f};
        glm::vec2 m_lastAttackSkillTarget = {0.0f, 0.0f};
        glm::ivec2 m_hoveredTile = {0, 0};
        bool m_hasHoveredTile = false;
        bool m_hasLastAttackSkillTarget = false;
        bool m_showActiveChunkHighlights = false;
        bool m_showSkillDebugOverlay = false;
        bool m_showPhysicsDebug = false;
        bool m_showFpsOverlay = true;   // 由 config.json performance.show_fps 控制
        bool m_showMapEditor = false;
        bool m_startInMapEditor = false;
        game::scene::UniverseEditor m_universeEditor;
        bool m_invertPlayerFacing = false;
        bool m_screenRainOverlay = false;
        float m_screenRainOverlayStrength = 1.0f;
        float m_screenRainMotionStrength = 1.0f;
        float m_flightAmbientGain = 0.0f;
        MIX_Track* m_flightAmbientTrack = nullptr;
        bool m_flightAmbientReady = false;
        bool m_flightAmbientWasFlyMode = false;
        bool m_vsyncEnabled = true;
        glm::vec2 m_cameraFollowDeadzonePx = {140.0f, 56.0f};
        bool m_showSettings = false;
        bool m_showEscMenu  = false;   // ESC 暂停/退出菜单
        // ── 按键绑定重映射状态 ────────────────────────────────────────────
        std::string m_keyListeningAction;      // 正在等待绑定的动作名（空=未在监听）
        int         m_keyListeningSlot   = 0;  // 绑定槽位：0=主键, 1=副键
        int         m_keyListeningFrames = 0;  // 已等待帧数（避免弹出瞬间误触发）
        bool m_cleanStartupUi = true;
        bool m_showPlayerConfigPanel = true;
        std::string m_editorLayoutPreset = "default";
        bool m_showEditorToolbar = true;
        bool m_showMainToolbar = true;
        unsigned int m_editorDockspaceId = 0;
        bool m_showResourceExplorerPanel = true;
        bool m_showSceneViewportPanel = true;
        bool m_showConsolePanel = true;
        bool m_showAnimationEditorPanel = false;
        bool m_showShaderEditorPanel = false;
        bool m_showProfilerPanel = false;
        bool m_showHierarchyPanel = true;
        bool m_showInspectorPanel = true;
        bool m_gameplayRunning = false;
        bool m_gameplayPaused = false;
        bool m_stepOneFrame = false;
        bool m_toolbarShowPlayControls = true;
        bool m_toolbarShowWindowControls = true;
        bool m_toolbarShowDebugControls = false;
        bool m_devOverlayShowEditorTools = true;
        bool m_sceneViewportShowGrid = true;
        bool m_sceneViewportShowAxes = true;
        bool m_sceneViewportShowCameraInfo = true;
        bool m_sceneViewportShowLighting = true;
        bool m_sceneViewportShowGizmo = true;
        int m_selectedActorIndex = -1;
        bool m_hierarchyGroupByTag = false;
        bool m_hierarchyFavoritesOnly = false;
        bool m_enablePlayRollback = true;
        std::string m_resourceExplorerViewMode = "tree";
        std::string m_selectedResourcePath;
        std::array<char, 128> m_resourceExplorerFilterBuffer{};
        std::array<char, 96> m_hierarchyFilterBuffer{};
        std::array<char, 64> m_inspectorRenameBuffer{};
        std::array<char, 64> m_inspectorTagBuffer{};
        std::array<char, 64> m_groundMakerNameBuffer{"ground_platform"};
        std::array<char, 96>  m_playerConfigNameBuffer{};
        std::array<char, 96>  m_mechConfigNameBuffer{};
        std::array<char, 256> m_playerConfigSmPathBuffer{};
        std::array<char, 256> m_playerConfigFrameJsonBuffer{};
        int m_inspectorRenameBufferActorIndex = -1;
        // ── 玩家碰撞 & 机甲高度 ──────────────────────────────────────────────
        float m_playerMechHeightPx   = 0.0f;   // 机甲视觉高度(px)，碰撞矩形 Y 偏移量
        float m_playerCollisionHalfW = 12.0f;  // 碰撞矩形半宽 (px)
        float m_playerCollisionHalfD = 3.5f;   // 碰撞矩形半深 (px)
        glm::vec2 m_groundMakerSpawnPos = {0.0f, 96.0f};
        glm::vec2 m_groundMakerScale = {1.0f, 1.0f};
        glm::vec2 m_groundMakerBodyHalfPx = {48.0f, 10.0f};
        int m_backgroundGridRows = 14;
        float m_backgroundGridStart = 0.0f;
        float m_backgroundGridEnd   = 0.42f;
        int m_groundGridRows = 10;
        float m_groundGridStart = 0.55f;
        float m_groundGridEnd   = 1.0f;
        float m_groundGridAspect = 2.5f;
        int m_groundMakerLengthCells = 6;
        int m_groundMakerTextureIndex = 0;
        float m_groundMakerRotation = 0.0f;
        bool m_groundMakerUsePhysics = true;
        bool m_groundCollisionLowerHalfOnly = true;
        bool m_groundMakerUseGridSnap = true;
        bool m_groundMakerSnapX = true;
        bool m_groundMakerSnapY = true;
        bool m_groundMakerShowGrid = true;
        bool m_groundMakerPlaceMode = false;
        bool m_groundMakerDragPlacing = false;
        bool m_groundMakerRightMouseWasDown = false;
        glm::vec2 m_groundMakerDragStartWorld = {0.0f, 0.0f};
        bool m_groundBoxSelecting = false;
        bool m_groundConfigDirty = false;
        glm::vec2 m_groundBoxSelectStartWorld = {0.0f, 0.0f};
        glm::vec2 m_groundBoxSelectEndWorld = {0.0f, 0.0f};
        bool m_showFootCollisionDebug = true;
        bool m_showEditorColliderBoxes = false;
        glm::vec4 m_debugFootRect = {0.0f, 0.0f, 0.0f, 0.0f};
        std::vector<glm::ivec2> m_debugFootTiles;
        bool m_debugFootOverlapped = false;
        float m_debugFootTileHeightPx = 0.0f;
        float m_debugFootPlayerHeightPx = 0.0f;
        std::array<float, static_cast<size_t>(engine::world::TileType::WallDecor) + 1> m_tileTypeHeightPx{};
        game::world::GroundTileCatalog m_groundTileCatalog;
        std::unordered_map<const engine::object::GameObject*, glm::vec2> m_groundColliderHalfByActor;
        std::unordered_set<const engine::object::GameObject*> m_groundSelection;
        std::unordered_set<const engine::object::GameObject*> m_hierarchyFavorites;
        bool m_devMode = false;         // 开发模式：显示地形/物理调试覆盖层
        engine::world::TileType m_mapEditorPaintTile = engine::world::TileType::Stone;
        std::string m_mapEditorPaintTileKey;
        int m_mapEditorBrushRadius = 0;

        enum class EditorConsoleLevel
        {
            Log,
            Warning,
            Error,
        };

        struct EditorConsoleEntry
        {
            EditorConsoleLevel level = EditorConsoleLevel::Log;
            std::string source;
            std::string message;
            double timeSeconds = 0.0;
        };

        std::vector<EditorConsoleEntry> m_consoleEntries;
        std::array<char, 128> m_consoleSearchBuffer{};
        bool m_consoleAutoScroll = true;
        bool m_consoleFilterLog = true;
        bool m_consoleFilterWarning = true;
        bool m_consoleFilterError = true;
        bool m_consoleScrollToBottom = false;
        bool m_editorLayoutLoadedFromConfig = false;
        float m_editorLayoutSaveAccumulator = 0.0f;

        struct ActorRuntimeSnapshot
        {
            std::string name;
            std::string tag;
            bool needRemove = false;
            bool hasTransform = false;
            glm::vec2 position = {0.0f, 0.0f};
            glm::vec2 scale = {1.0f, 1.0f};
            float rotation = 0.0f;
            bool hasController = false;
            float controllerSpeed = 0.0f;
            bool controllerEnabled = true;
            bool controllerRunMode = false;
            bool hasPhysics = false;
            glm::vec2 physicsVelocity = {0.0f, 0.0f};
            glm::vec2 physicsPosition = {0.0f, 0.0f};
            bool hasSprite = false;
            bool spriteHidden = false;
            bool spriteFlipped = false;
            bool hasParallax = false;
            glm::vec2 parallaxFactor = {1.0f, 1.0f};
            glm::bvec2 parallaxRepeat = {false, false};
            bool parallaxHidden = false;
        };

        struct TileRuntimeSnapshot
        {
            int x = 0;
            int y = 0;
            engine::world::TileData tile{};
        };

        struct UiRuntimeSnapshot
        {
            bool showInventory = false;
            bool showSettings = false;
            bool showMapEditor = false;
            bool missionWindow = false;
            bool showSettlement = false;
            bool showHierarchyPanel = true;
            bool showInspectorPanel = true;
            bool showFpsOverlay = true;
            bool devMode = false;
            bool showSkillDebug = false;
            bool showChunkHighlight = false;
            int selectedActorIndex = -1;
            int weaponActiveIndex = 0;
            game::inventory::Inventory inventory;
            game::inventory::Inventory mechInventory;
            game::inventory::EquipmentLoadout equipmentLoadout;
            std::array<game::inventory::InventorySlot, 6> starSockets{};
            std::array<float, 6> skillCooldowns{};
            game::weapon::WeaponBar weaponBar;
        };

        bool m_hasPlaySnapshot = false;
        std::vector<ActorRuntimeSnapshot> m_playActorSnapshots;
        std::vector<TileRuntimeSnapshot> m_playTileSnapshots;
        UiRuntimeSnapshot m_playUiSnapshot;
        game::world::TimeOfDaySystem::RuntimeState m_playTimeSnapshot;
        game::weather::WeatherSystem::RuntimeState m_playWeatherSnapshot;
        int m_snapshotPlayerIndex = -1;
        int m_snapshotMechIndex = -1;
        int m_snapshotPossessedIndex = -1;
        bool m_snapshotIsPlayerInMech = false;
        int m_snapshotCurrentZone = 0;

        // 背包系统
        game::inventory::Inventory m_inventory;      // 人物背包
        game::inventory::Inventory m_mechInventory;  // 机甲背包
        game::inventory::EquipmentLoadout m_equipmentLoadout;
        bool m_showInventory = false;

        // 星技槽：6 个圆形槽，只接受 StarSkill 类型物品
        std::array<game::inventory::InventorySlot, 6> m_starSockets;

        // 星技技能状态
        std::array<float, 6> m_skillCooldowns{};   // 各槽冷却计时（秒）
        bool m_windStarEquipped = false;            // 上帧疾风星技装备状态（防止每帧重设）
        bool m_prevFlyModeActive = false;             // 上帧飞行模式状态（用于检测切换）
        std::string m_modeSwitchHintText;             // 模式切换提示文字
        float m_modeSwitchHintTimer = 0.0f;           // 提示显示倒计时（秒）

        // 技能特效粒子列表
        struct SkillVFX
        {
            game::skill::SkillEffect type;
            glm::vec2 worldPos;
            float age    = 0.0f;   // 当前已存在时间（秒）
            float maxAge = 0.6f;   // 总持续时间
            float param  = 0.0f;   // 附加参数（如冲刺方向）
            bool active = false;
        };
        std::vector<SkillVFX> m_skillVfxList;

        struct SkillProjectile
        {
            game::skill::SkillEffect type;
            glm::vec2 originPos;
            glm::vec2 worldPos;
            glm::vec2 lastWorldPos;
            glm::vec2 targetPos;
            glm::vec2 velocity;
            float age = 0.0f;
            float maxAge = 0.0f;
            float radius = 0.0f;
            bool active = false;
        };
        std::vector<SkillProjectile> m_skillProjectiles;

        struct SlashVFX
        {
            glm::vec2 worldPos;
            float facing = 1.0f;
            float age = 0.0f;
            float maxAge = 0.18f;
            float radius = 0.0f;
            bool active = false;
        };
        std::vector<SlashVFX> m_slashVfxList;

        struct CombatFragment
        {
            glm::vec2 worldPos;
            glm::vec2 velocity;
            float age = 0.0f;
            float maxAge = 0.0f;
            float size = 0.0f;
            bool active = false;
        };
        std::vector<CombatFragment> m_combatFragments;

        // 武器栏
        game::weapon::WeaponBar m_weaponBar;
        float m_weaponAttackCooldown = 0.0f;

        // 怪物系统
        std::unique_ptr<game::monster::MonsterManager> m_monsterManager;

        // 树木系统
        game::world::TreeManager m_treeManager;

        // 天气系统
        game::weather::WeatherSystem m_weatherSystem;

        // 昼夜与天空系统
        game::world::TimeOfDaySystem m_timeOfDaySystem;

        // 星球任务规划 UI
        game::mission::PlanetMissionUI m_missionUI;

        // 路线数据
        game::route::RouteData m_routeData;
        int                    m_currentZone = 0;

        // 星球（世界）参数
        engine::world::WorldConfig m_worldConfig;

        void createTestObject();
        void testCamera();
        void createPlayer();
        void setupGroundTileScene(const engine::world::WorldConfig& config);
        void setupSkyBackgroundScene();
        void setupGroundBuildingBackgroundScene();
        void warmupSceneTextures();
        void initializeGroundChunksAndTrees();
        void preallocateRuntimeBuffers();
        void updateFlightAmbientSound(float dt);
        void shutdownFlightAmbientSound();
        void emitSkillVFX(game::skill::SkillEffect type, glm::vec2 worldPos, float maxAge, float param);
        void emitSkillProjectile(game::skill::SkillEffect type,
                     glm::vec2 originPos,
                     glm::vec2 worldPos,
                     glm::vec2 lastWorldPos,
                     glm::vec2 targetPos,
                     glm::vec2 velocity,
                     float maxAge,
                     float radius);
        void emitSlashVFX(glm::vec2 worldPos, float facing, float maxAge, float radius);
        void emitCombatFragment(glm::vec2 worldPos, glm::vec2 velocity, float maxAge, float size);
        void renderUI();
        void updateTextTexture();
        void renderInventoryUI();
        void renderEquipmentPage();
        void renderStarSocketPage();
        void renderSkillHUD();           // 屏幕底部技能冷却 HUD
        void renderPlayerStatusHUD();    // 左上角 HP / 星能 / 属性面板
        void tickSkillVFX(float dt);     // 推进技能特效寿命
        void tickSkillProjectiles(float dt);
        void tickCombatEffects(float dt);
        void renderSkillVFX();           // 渲染技能特效（前景层）
        void renderSkillProjectiles();
        void renderCombatEffects();
        void renderSkillDebugOverlay();  // 临时调试：技能目标与特效对齐
        void renderWeaponBar();
        void renderDropItems();
        void renderPlayerStateTag();
        void renderFlightThrusterFX();
        void renderModeSwitchHint();          // 飞行/陆地模式切换屏幕提示
        void renderMonsterIFFMarkers();
        void renderActorGroundShadows();
        float tileHeightForType(engine::world::TileType type) const;
        void updateActorFootTileContact(engine::object::GameObject* actor);
        void syncPlayerPresentation();
        void renderPerformanceOverlay();
        void renderEditorWorkbenchShell();
        void renderEditorMainMenuBar();
        void renderEditorMainToolbar();
        void renderEditorStatusBar();
        void renderEditorToolbar();
        void renderHierarchyPanel();
        void renderInspectorPanel();
        void renderResourceExplorerPanel();
        void renderSceneViewportPanel();
        void renderConsolePanel();
        void renderAnimationEditorPanel();
        void renderShaderEditorPanel();
        void renderProfilerPanel();
        void appendEditorConsole(EditorConsoleLevel level, const std::string& source, const std::string& message);
        void ensureEditorConsoleSeeded();
        void persistEditorUiSettings() const;
        void applyEditorLayoutPreset(const std::string& presetKey, bool loadImGuiLayout);
        std::string editorLayoutIniPath(const std::string& presetKey) const;
        void renderSettingsPage();
        void renderPlayerConfigPanel();
        void renderMapEditor();
        void renderPlayerAltitudeMeter();
        void createGroundActor();
        float backgroundZoneTopWorldY() const;
        float backgroundZoneBottomWorldY() const;
        float groundZoneTopWorldY() const;
        float groundZoneBottomWorldY() const;
        glm::vec2 backgroundGridCellSizeWorld() const;
        glm::vec2 groundGridCellSizeWorld() const;
        bool isGroundZoneAt(glm::vec2 worldPos) const;
        glm::vec2 groundMakerGridSizeFor(glm::vec2 worldPos) const;
        glm::vec2 snapGroundMakerPosition(glm::vec2 worldPos) const;
        float snapGroundMakerWidth(float widthPx) const;
        float groundMakerWidthFromCells() const;
        void loadGroundActorsFromConfig(bool clearExisting);
        void saveGroundActorsToConfig();
        void clearPersistedGroundActors();
        void snapSelectedGroundActorsToGrid();
        void pruneGroundSelection();
        bool isGroundActor(const engine::object::GameObject* actor) const;
        void applyRuntimeGraphicsSettings();
        void setGameplayRunning(bool running);
        void capturePlaySnapshot();
        bool restorePlaySnapshot();
        void updateSettingsParticles(float dt);  // 设置界面粒子更新（每帧调用）
        void renderMechPrompt();
        void renderRouteHUD();    // 左下角路线 HUD
        void renderSettlementUI(); // 撤离结算界面
        void injectOreVeins();    // 在目标格区域注入矿脉
        void generateRockObstacles();
        void initTestItems();
        void loadActorRoleConfig();
        void loadMechProfileConfig();
        void loadSelectedCharacterProfileConfig();
        void saveMechProfileConfig() const;
        void updateMechFlightCapability();
        void updateEquipmentAttributeBonuses();
        void tryEnterMech();
        void exitMech();
        void tryPossessNearestMonster();
        void releasePossessedMonster(bool forced = false);
        void updatePossession(float dt);
        void performPossessedMonsterAttack();
        void performPossessedMonsterSkill();
        void performMeleeAttack(glm::vec2 targetPos);
        void performMechAttack();
        void tickStarSkillPassives(float dt);          // 被动星技每帧更新
        void triggerAttackStarSkills(glm::vec2 pos);  // 攻击触发型星技
        void triggerActiveStarSkills();               // 主动技能（Q键）
        void explodeFireBlast(glm::vec2 pos, float radius);
        bool canAccessMechInventory() const;
        engine::object::GameObject* getControlledActor() const;
        glm::vec2 getActorWorldPosition(const engine::object::GameObject* actor) const;
        glm::vec2 getPlayerCastOrigin(glm::vec2 targetPos) const;
        glm::vec2 findSafeDisembarkPosition() const;

        // 撤离结算状态
        bool m_showSettlement = false;
        bool m_isPlayerInMech = false;
        std::string m_mechProfilePath = "assets/mechs/default_mech.json";
        std::string m_selectedCharacterProfilePath = "assets/characters/gundom.character.json";
        std::string m_playerFrameJsonPath = "assets/textures/Characters/gundom.frame.json";
        std::string m_playerActorKey = "player";
        std::string m_mechActorKey = "mech_drop";
        std::string m_controlLabelPlayer = "人物";
        std::string m_controlLabelMech = "机甲";
        std::string m_controlLabelPossessed = "接管体";
        std::string m_statePrefixMech = "机甲·";
        std::string m_statePrefixPossessed = "接管·";
        std::string m_hudPlayerText = "player";
        std::string m_hudMechPrefix = "机甲：";
        std::string m_hudMechName = "gundom";
        std::array<char, 16> m_commandBuffer{};  // 保留，避免 config 序列化引用
        engine::object::GameObject* m_mech = nullptr;
        engine::object::GameObject* m_possessedMonster = nullptr;
        float m_possessionEnergy = 0.0f;
        float m_playerBaseMoveSpeed = 12.0f;
        float m_playerBaseJumpSpeed = 8.0f;
        float m_playerBaseMaxHp = 100.0f;
        float m_playerBaseMaxEnergy = 100.0f;
        float m_possessionFxTimer = 0.0f;
        float m_possessedAttackCooldown = 0.0f;
        float m_possessedSkillCooldown = 0.0f;
        int m_possessedLastAttackHits = 0;
        float m_mechAttackCooldown = 0.0f;
        float m_mechAttackFlashTimer = 0.0f;
        int m_mechLastAttackHits = 0;
        bool m_mechFlightEngineInstalled = false;
        // 玩家帧动画由 AnimationComponent 管理，此处不再保留手动计时变量

        // 机甲精灵表动画
        int   m_mechAnimFrame  = 0;
        float m_mechAnimTimer  = 0.0f;
        int   m_mechAnimRow    = 1;   // 0=步行 1=待机/瞄准 2=射击
        float m_mechShootTimer = 0.0f;

        // DNF 卷轴战斗：冲刺系统
        bool  m_isDashing       = false;
        float m_dashTimer       = 0.0f;   // 当前冲刺剩余时长
        float m_dashCooldown    = 0.0f;   // 冲刺冷却剩余时长
        float m_dashFacing      = 1.0f;   // 冲刺方向 (+1=右 -1=左)
        bool  m_dashKeyWas      = false;  // 上帧 Shift 状态（边沿检测）

        // DNF 卷轴战斗：连击系统
        int   m_comboCount      = 0;      // 当前连击数 (0‑2)
        float m_comboResetTimer = 0.0f;   // 连击窗口倒计时

        // DNF 双击跑步检测
        float m_doubleTapTimer   = 0.0f;  // 200ms 窗口倒计时
        int   m_doubleTapLastDir = 0;     // 上次单击方向 (-1/0/1)

        // ── Gundam 攻击动画状态机 ──────────────────────────────
        // 连招动画队列：Z键/左键鼠标 触发，每轮 attack_a → attack_b → attack_c 循环
        int   m_attackComboStep   = 0;    // 0/1/2 对应 a/b/c 连招
        float m_attackAnimTimer   = 0.0f; // 当前攻击动画剩余时间（>0 表示正在播放攻击动画）
        int   m_attackQueuedCount = 0;    // 攻击动画中缓存的连招输入段数（0-2）

        // 重击/大招变量
        float m_heavyAttackTimer  = 0.0f; // >0 = 重击动画播放中
        float m_ultimateTimer     = 0.0f; // >0 = 大招动画播放中
        float m_cannonTimer       = 0.0f; // >0 = 炮击动画播放中

        // 攻击动画持续时间常量（秒）
        // attack_a/b/c: 4帧，时序 60+80+80+100 ms = 320 ms
        static constexpr float kAttackADur  = 0.32f;
        static constexpr float kAttackBDur  = 0.32f;
        static constexpr float kAttackCDur  = 0.32f;
        static constexpr float kAttackDDur  = 8 * 0.06f;
        static constexpr float kCannonDur   = 8 * 0.08f;
        static constexpr float kUltimateDur = 8 * 0.10f;

        // ── 帧编辑器 ──────────────────────────────────────────────────
        FrameEditor m_frameEditor;

        // ── 状态机编辑器 ──────────────────────────────────────────────
        StateMachineEditor m_smEditor;

        // ── 开发模式：角色选择器 ─────────────────────────────────────────
        struct CharacterEntry {
            std::string id;           // e.g. "gundom"
            std::string displayName;  // e.g. "冈达姆"
            std::string frameJsonPath; // e.g. "assets/textures/Characters/gundom.frame.json"
            std::string texturePath;  // e.g. "assets/textures/Characters/gundom.png"
            std::string smPath;       // e.g. "assets/textures/Characters/gundom.sm.json"
            // 碰撞参数
            float collisionHalfW = 12.0f;
            float collisionHalfD = 3.5f;
            float mechHeightPx   = 0.0f;
            float moveSpeed      = 220.0f;
            float jumpVelocity   = -420.0f;
            std::string profilePath; // assets/characters/xxx.character.json
        };
        std::vector<CharacterEntry> m_characters;
        int m_selectedCharacter = 0;

        // ── 开发模式：实体管理面板 ────────────────────────────────────────
        int  m_selectedWorldEntity   = -1;      // 实体列表中当前高亮行
        int  m_selectedSpawnChar     = 0;       // 生成列表中的选中项（基于过滤后索引）
        bool m_showEntityManagerPanel = true;   // 实体管理面板可见性
        engine::object::GameObject* m_devControlledActor = nullptr; // dev 模式下指定控制的实体（null = 玩家）

        // ── 玩家状态机控制器 ─────────────────────────────────────────────
        // 用法：
        //  1. 在 init() 或切换角色时调用 loadPlayerSM("xxx.sm.json")
        //  2. 在 update() 中调用 tickPlayerSM(dt)：
        //     - 根据按键/物理状态构建 activeInputs
        //     - 调用 m_playerSM.update() 并应用根位移/帧事件
        engine::statemachine::StateController m_playerSM;
        engine::statemachine::StateMachineData m_playerSMData;
        std::string m_playerSMPath;
        bool   m_playerSMLoaded  = false;
        bool   m_prevGrounded    = true;   // 上帧落地状态（用于检测 LAND 事件）

        void loadPlayerSM(const std::string& smJsonPath);  // 加载并 init
        void tickPlayerSM(float dt);                        // 每帧驱动状态机

        void scanCharacters();        // 扫描 character profile 文件填充 m_characters
        void reloadPlayerAnimation(const std::string& frameJsonPath); // 切换角色动画（不重建物理体）
        void spawnCharacterFromProfile(const CharacterEntry& ce); // 在玩家附近生成非玩家角色实体
        void setDevControlledActor(engine::object::GameObject* actor); // 切换 dev 控制对象，自动禁用其他实体的输入
        void renderDevModeOverlay();      // 右上角调试覆盖层（仅 dev 模式）
        void renderEntityManagerPanel();  // 实体管理面板（编辑器工作台内，dev 模式均可显示）
    }; // class GameScene
} // namespace game::scene