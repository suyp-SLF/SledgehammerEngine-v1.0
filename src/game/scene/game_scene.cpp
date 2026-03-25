#include "game_scene.h"
#include "menu_scene.h"
#include "ship_scene.h"
#include "../component/attribute_component.h"
#include "../../engine/scene/scene_manager.h"
#include "../../engine/object/game_object.h"
#include "../../engine/component/transform_component.h"
#include "../../engine/component/sprite_component.h"
#include "../../engine/component/controller_component.h"
#include "../../engine/component/physics_component.h"
#include "../../engine/component/animation_component.h"
#include "../../engine/core/context.h"
#include "../../engine/render/sprite_render_system.h"
#include "../../engine/render/parallax_render_system.h"
#include "../../engine/render/tilelayer_render_system.h"
#include "../../engine/resource/resource_manager.h"
#include "../../engine/resource/font_manager.h"
#include "../../engine/input/input_manager.h"
#include "../../engine/render/camera.h"
#include "../../engine/world/world_config.h"
#include "../../engine/core/config.h"
#include "../../engine/world/perlin_noise_generator.h"
#include "../../engine/world/chunk_manager.h"
#include "../../engine/render/renderer.h"
#include "../../engine/ecs/components.h"
#include "../locale/locale_manager.h"
#include "../../engine/utils/math.h"
#include <spdlog/spdlog.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <mach/mach.h>

namespace game::scene
{

static void drawWorldShadow(engine::core::Context &context,
                            const glm::vec2 &center,
                            const glm::vec2 &size,
                            float alpha)
{
    auto &renderer = context.getRenderer();
    const auto &camera = context.getCamera();

    renderer.drawRect(camera,
                      center.x - size.x * 0.5f,
                      center.y - size.y * 0.5f,
                      size.x,
                      size.y,
                      glm::vec4(0.0f, 0.0f, 0.0f, alpha));
    renderer.drawRect(camera,
                      center.x - size.x * 0.35f,
                      center.y - size.y * 0.38f,
                      size.x * 0.70f,
                      size.y * 0.76f,
                      glm::vec4(0.0f, 0.0f, 0.0f, alpha * 0.55f));
}

// ────────────────────────────────────────────────────────────────────────────
//  绘制星技球形图标 (file-scope helper)
//  dl     : ImDrawList*
//  center : 图标中心屏幕坐标
//  r      : 半径（像素）
//  id     : 星技 item id，如 "star_fire"
//  alpha  : 整体不透明度 0-1
// ────────────────────────────────────────────────────────────────────────────
static void drawStarGemSphereIcon(ImDrawList* dl, ImVec2 center, float r,
                                  const std::string& id, float alpha = 1.0f)
{
    int a = static_cast<int>(alpha * 255.0f);
    if (a <= 0) return;

    ImU32 baseCol, midCol, rimCol, glowCol;
    const char* symbol = "\xe2\x98\x86"; // ☆ fallback
    if (id == "star_fire")
    {
        baseCol = IM_COL32(140, 40,  5,  a);
        midCol  = IM_COL32(230,110,  0,  a);
        rimCol  = IM_COL32(255, 80,  0,  a);
        glowCol = IM_COL32(255,140,  0, static_cast<int>(60*alpha));
        symbol  = "\xe7\x81\xab"; // 火
    }
    else if (id == "star_ice")
    {
        baseCol = IM_COL32( 10, 60,140,  a);
        midCol  = IM_COL32( 50,130,200,  a);
        rimCol  = IM_COL32( 80,200,255,  a);
        glowCol = IM_COL32( 80,200,255, static_cast<int>(60*alpha));
        symbol  = "\xe5\x86\xb0"; // 冰
    }
    else if (id == "star_wind")
    {
        baseCol = IM_COL32( 20,100, 50,  a);
        midCol  = IM_COL32( 50,180, 80,  a);
        rimCol  = IM_COL32( 80,230,100,  a);
        glowCol = IM_COL32( 80,230,100, static_cast<int>(60*alpha));
        symbol  = "\xe9\xa3\x8e"; // 风
    }
    else if (id == "star_light")
    {
        baseCol = IM_COL32(120,100,  5,  a);
        midCol  = IM_COL32(210,180, 20,  a);
        rimCol  = IM_COL32(255,240, 60,  a);
        glowCol = IM_COL32(255,240, 60, static_cast<int>(60*alpha));
        symbol  = "\xe5\x85\x89"; // 光
    }
    else
    {
        baseCol = IM_COL32( 60, 60, 80,  a);
        midCol  = IM_COL32( 90, 90,120,  a);
        rimCol  = IM_COL32(160,160,200,  a);
        glowCol = IM_COL32(160,160,200, static_cast<int>(40*alpha));
    }

    // 外发光环
    dl->AddCircle(center, r * 1.3f, glowCol, 32, 2.0f);
    // 底色实心圆
    dl->AddCircleFilled(center, r, baseCol);
    // 中间色（偏上左，3D 感）
    dl->AddCircleFilled({center.x - r*0.10f, center.y - r*0.14f}, r*0.70f, midCol);
    // 高光（白色，左上角小圆）
    dl->AddCircleFilled({center.x - r*0.26f, center.y - r*0.30f}, r*0.34f,
                        IM_COL32(255,255,255, static_cast<int>(80*alpha)));
    // 轮廓
    dl->AddCircle(center, r, rimCol, 32, 1.5f);
    // 中央符号文字
    ImVec2 ts = ImGui::CalcTextSize(symbol);
    dl->AddText({center.x - ts.x*0.5f, center.y - ts.y*0.5f},
                IM_COL32(255,255,255, a), symbol);
}

// ────────────────────────────────────────────────────────────────────────────
//  绘制普通物品图标（带颜色标识的小方块 + 类别符号）
// ────────────────────────────────────────────────────────────────────────────
static void drawItemIcon(ImDrawList* dl, ImVec2 bmin, ImVec2 bmax,
                         game::inventory::ItemCategory cat, int count)
{
    float cx = (bmin.x + bmax.x) * 0.5f;
    float cy = (bmin.y + bmax.y) * 0.5f;
    float r  = std::min(bmax.x - bmin.x, bmax.y - bmin.y) * 0.32f;

    const char* sym = nullptr;
    ImU32 col = IM_COL32(160,160,160,200);
    switch (cat)
    {
    case game::inventory::ItemCategory::Weapon:
        col = IM_COL32(200,120, 60,220);
        sym = "\xe5\x88\x80"; // 刀
        break;
    case game::inventory::ItemCategory::Consumable:
        col = IM_COL32( 80,200,100,220);
        sym = "\xe2\x9c\xbf"; // ✿ (fallback ascii '+')
        break;
    case game::inventory::ItemCategory::Material:
        col = IM_COL32(100,180,240,220);
        sym = "\xe2\x97\x86"; // ◆
        break;
    default:
        col = IM_COL32(160,160,180,200);
        sym = nullptr;
        break;
    }

    // 圆角矩形背景
    dl->AddRectFilled(bmin, bmax, IM_COL32(20,20,35,180), 4.0f);

    // 类别色圆形图标
    dl->AddCircleFilled({cx, cy - r*0.3f}, r, col);
    dl->AddCircle({cx, cy - r*0.3f}, r, IM_COL32(255,255,255,60), 20, 1.0f);

    if (sym)
    {
        ImVec2 ts = ImGui::CalcTextSize(sym);
        dl->AddText({cx - ts.x*0.5f, cy - r*0.3f - ts.y*0.5f},
                    IM_COL32(255,255,255,200), sym);
    }

    // 数量（右下角）
    if (count > 1)
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", count);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        dl->AddText({bmax.x - ts.x - 2.0f, bmax.y - ts.y - 1.0f},
                    IM_COL32(220,220,220,230), buf);
    }
}

static ImVec2 logicalToImGuiScreen(const engine::core::Context& context, const glm::vec2& logicalPos)
{
    glm::vec2 logicalSize = context.getRenderer().getLogicalSize();
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    if (logicalSize.x <= 0.0f || logicalSize.y <= 0.0f)
        return {logicalPos.x, logicalPos.y};

    return {
        logicalPos.x * (displaySize.x / logicalSize.x),
        logicalPos.y * (displaySize.y / logicalSize.y)
    };
}

static void drawDebugCross(ImDrawList* dl, ImVec2 center, ImU32 color, float size = 8.0f)
{
    dl->AddLine({center.x - size, center.y}, {center.x + size, center.y}, color, 1.5f);
    dl->AddLine({center.x, center.y - size}, {center.x, center.y + size}, color, 1.5f);
    dl->AddCircle(center, size * 0.7f, color, 16, 1.0f);
}

static bool isProjectileBlockingTile(engine::world::TileType type)
{
    return type != engine::world::TileType::Air;
}

static bool loadBoolSetting(const char* key, bool defaultValue = false)
{
    std::ifstream file("assets/settings.json");
    if (!file.is_open())
        return defaultValue;

    try
    {
        nlohmann::json j;
        file >> j;
        return j.value(key, defaultValue);
    }
    catch (const std::exception&)
    {
        return defaultValue;
    }
}

static void saveBoolSetting(const char* key, bool enabled)
{
    nlohmann::json j = nlohmann::json::object();

    {
        std::ifstream file("assets/settings.json");
        if (file.is_open())
        {
            try
            {
                file >> j;
            }
            catch (const std::exception&)
            {
                j = nlohmann::json::object();
            }
        }
    }

    j[key] = enabled;

    std::ofstream file("assets/settings.json");
    if (!file.is_open())
        return;
    file << j.dump(4);
}


    GameScene::GameScene(const std::string &name,
                         engine::core::Context &context,
                         engine::scene::SceneManager &sceneManager,
                         game::route::RouteData routeData)
        : Scene(name, context, sceneManager)
        , m_routeData(std::move(routeData))
    {
        spdlog::debug("GameScene '{}' 构造完成（路线 {} 步）",
                      name, m_routeData.path.size());
    }

    void GameScene::init()
    {
        Scene::init(); // 设置 _is_initialized = true

        physics_manager = std::make_unique<engine::physics::PhysicsManager>();
        physics_manager->init({0.0f, 10.0f});

        engine::world::WorldConfig config;
        config.loadFromFile("assets/world_config.json");
        config.seed = m_routeData.planetSeed;
        config.seaLevel += m_routeData.planetSeaLevelOffset;
        config.amplitude *= m_routeData.planetAmplitudeScale;
        config.treeMinTrunkHeight = m_routeData.planetTreeMin;
        config.treeMaxTrunkHeight = m_routeData.planetTreeMax;
        config.treeSpacing = m_routeData.planetTreeSpacing;
        m_worldConfig = config;  // 保存星球参数
        m_timeOfDaySystem.dayLengthSeconds = m_routeData.dayLengthSeconds;

        // 读取游戏配置（FPS 覆盖层等）
        {
            engine::core::Config gameConfig("assets/config.json");
            m_showFpsOverlay = gameConfig._show_fps_overlay;
        }
        m_showSkillDebugOverlay = loadBoolSetting("show_skill_debug_overlay");
        m_showActiveChunkHighlights = loadBoolSetting("show_active_chunk_highlights");

        chunk_manager = std::make_unique<engine::world::ChunkManager>(
            "assets/textures/Tiles/tileset.svg",
            config.TILE_SIZE,
            &_context.getResourceManager(),
            physics_manager.get());

        auto generator = std::make_unique<engine::world::PerlinNoiseGenerator>(config);

        // 绑定生物群系查询：根据世界 X 坐标查询当前路线格子的地形类型
        if (m_routeData.isValid())
        {
            auto rd = m_routeData;  // 捕获副本，生成器生命周期内始终有效
            generator->setBiomeLookup([rd](int tileX) -> int {
                int zone = tileX / game::route::RouteData::TILES_PER_CELL;
                if (zone < 0 || zone >= static_cast<int>(rd.path.size()))
                    return 0;  // 路线外：草原
                auto cell = rd.path[zone];
                return static_cast<int>(rd.terrain[cell.y][cell.x]);
            });
        }

        chunk_manager->setTerrainGenerator(std::move(generator));

        actor_manager = std::make_unique<engine::actor::ActorManager>(_context);
        createPlayer();
        m_monsterManager = std::make_unique<game::monster::MonsterManager>(
            _context,
            *actor_manager,
            *physics_manager,
            *chunk_manager,
            m_player);

        // OpenGL模式下暂时不加载字体（TextRenderer需要SDL_GPUDevice）
        // auto& fontMgr = _context.getResourceManager().getFontManager();
        // fontMgr.setDevice(_context.getRenderer().getDevice());
        // text_renderer = fontMgr.loadFont("assets/fonts/VonwaonBitmap-16px.ttf", 16);

        // 初始化ImGui
        SDL_Window* window = _context.getRenderer().getWindow();
        spdlog::debug("GameScene::init() - window ptr: {}", (void*)window);
        if (window)
        {
            m_glContext = SDL_GL_GetCurrentContext();
            spdlog::debug("GameScene::init() - GL context ptr: {}", (void*)m_glContext);
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
                spdlog::info("ImGui initialized with OpenGL3 + 中文字体");
            }
            else
            {
                spdlog::error("Failed to get OpenGL context for ImGui");
            }
        }
        else
        {
            spdlog::error("Failed to get window for ImGui");
        }

        // ECS测试
        for (int i = 0; i < 5; i++)
        {
            auto entity = ecs_registry.create();
            ecs_registry.add<engine::ecs::Position>(entity, glm::vec2(i * 50.0f, 100.0f));
            ecs_registry.add<engine::ecs::Velocity>(entity, glm::vec2(10.0f + i * 5.0f, 0.0f));
        }
        spdlog::info("ECS: 创建了5个测试实体");

        initTestItems();

        // 预热 SVG 纹理缓存：IMG_Load 会光栅化 SVG，首次调用耗时 ~100-300ms
        // 在 updateVisibleChunks 触发区块网格构建前预先完成，避免首帧卡顿
        {
            auto& resMgr = _context.getResourceManager();
            resMgr.getGLTexture("assets/textures/Tiles/tileset.svg");
            resMgr.getGLTexture("assets/textures/Characters/player_sheet.svg");
            resMgr.getGLTexture("assets/textures/Characters/wolf.svg");
            resMgr.getGLTexture("assets/textures/Characters/white_ape.svg");
            resMgr.getGLTexture("assets/textures/Characters/slime.svg");
            resMgr.getGLTexture("assets/textures/Props/tileset_atlas.svg");
            spdlog::info("纹理预热完毕（6 个 SVG）");
        }

        // 在初始可见区域生成树木（先 updateVisibleChunks 确保区块已加载）
        chunk_manager->updateVisibleChunks({0.0f, 0.0f}, 3);
        for (int cx = -3; cx <= 3; ++cx)
            for (int cy = -3; cy <= 3; ++cy)
                m_treeManager.generateTreesForChunk(cx, cy, *chunk_manager, m_worldConfig);
        spdlog::info("TreeManager: 初始区域树木生成完毕（min height={}, max height={}, spacing={})",
            m_worldConfig.treeMinTrunkHeight, m_worldConfig.treeMaxTrunkHeight, m_worldConfig.treeSpacing);

        // 在目标区域注入矿脉（需在区块加载后）
        injectOreVeins();
    }
    void GameScene::update(float delta_time)
    {
        Scene::update(delta_time);

        m_mechAttackCooldown = std::max(0.0f, m_mechAttackCooldown - delta_time);
        m_mechAttackFlashTimer = std::max(0.0f, m_mechAttackFlashTimer - delta_time);
        m_weaponAttackCooldown = std::max(0.0f, m_weaponAttackCooldown - delta_time);
        m_timeOfDaySystem.update(delta_time);

        tickStarSkillPassives(delta_time);
        tickSkillVFX(delta_time);
        tickSkillProjectiles(delta_time);
        tickCombatEffects(delta_time);

        if (m_monsterManager)
            m_monsterManager->update(delta_time);

        // 更新相机
        _context.getCamera().update(delta_time);

        if (physics_manager)
        {
            // 限幅 delta 防止帧率低时物理负荷越大越慢的死循环；子步 2 = 拟真精度对折中
            const float physDt = std::min(delta_time, 1.0f / 30.0f);
            physics_manager->update(physDt, 2);
        }

        if (actor_manager)
        {
            actor_manager->update(delta_time);
        }

        syncPlayerPresentation();

        // 每秒输出一次玩家信息
        static float timer = 0.0f;
        timer += delta_time;
        if (timer >= 1.0f && m_player)
        {
            auto* transform = m_player->getComponent<engine::component::TransformComponent>();
            if (transform)
            {
                float height = transform->getPosition().y;
                spdlog::info("Player: {} | Height: {:.1f}", m_player->getName(), height);
            }
            timer = 0.0f;
        }

        // 更新掉落物（重力、拾取）
        {
            glm::vec2 ppos = getActorWorldPosition(getControlledActor());
            m_treeManager.updateDrops(delta_time, ppos, m_inventory, *chunk_manager);
        }

        // 更新天气
        {
            const auto &io = ImGui::GetIO();
            m_weatherSystem.update(delta_time, io.DisplaySize.x, io.DisplaySize.y);
        }

        // 更新星球任务规划 UI
        {
            glm::vec2 ppos = getActorWorldPosition(getControlledActor());
            m_missionUI.update(delta_time, ppos, *chunk_manager);
        }

        // 更新路线区域进度
        if (!m_routeData.path.empty())
        {
            // tile X = worldX / 16 ; zone = tileX / TILES_PER_CELL
            glm::vec2 ppos = getActorWorldPosition(getControlledActor());
            int tileX = static_cast<int>(ppos.x) / 16;
            int zone  = tileX / game::route::RouteData::TILES_PER_CELL;
            zone = std::max(0, std::min(zone, static_cast<int>(m_routeData.path.size()) - 1));
            if (zone != m_currentZone)
            {
                m_currentZone = zone;
                spdlog::info("路线进度: 第 {}/{} 步  ({})",
                    m_currentZone + 1,
                    static_cast<int>(m_routeData.path.size()),
                    game::route::RouteData::cellLabel(m_routeData.path[m_currentZone]));
            }
        }

        glm::vec2 playerPos = getActorWorldPosition(getControlledActor());

        // 只在玩家移动超过半个 chunk 时才更新可见区块
        constexpr float CHUNK_UPDATE_THRESHOLD = engine::world::Chunk::SIZE * 16.0f * 0.5f;
        if (glm::distance(playerPos, m_lastChunkUpdatePos) > CHUNK_UPDATE_THRESHOLD)
        {
            chunk_manager->updateVisibleChunks(playerPos, 3);
            m_lastChunkUpdatePos = playerPos;
        }
    }

    void GameScene::render()
    {
        Scene::render();
        m_timeOfDaySystem.renderBackground(_context, m_weatherSystem.getSkyVisibility());
        chunk_manager->renderAll(_context);
        _context.getParallaxRenderSystem().renderAll(_context);
        _context.getSpriteRenderSystem().renderAll(_context);
        _context.getTilelayerRenderSystem().renderAll(_context);
        renderActorGroundShadows();

        if (actor_manager)
        {
            actor_manager->render();
        }

        m_timeOfDaySystem.renderLighting(_context);

        if (chunk_manager && m_showActiveChunkHighlights)
        {
            chunk_manager->renderActiveChunkHighlights(_context);
        }

        if (physics_manager && m_showPhysicsDebug)
        {
            physics_manager->debugDraw(_context.getRenderer(), _context.getCamera());
        }

        if (m_hasHoveredTile)
        {
            glm::vec2 tileWorldPos = chunk_manager->tileToWorld(m_hoveredTile);
            const auto &tileSize = chunk_manager->getTileSize();
            _context.getRenderer().drawRect(_context.getCamera(), tileWorldPos.x, tileWorldPos.y, tileSize.x, tileSize.y,
                                            glm::vec4(1.0f, 0.9f, 0.2f, 0.35f));
        }

        // 渲染UI文字
        // 渲染UI文字
        if (text_renderer && m_player)
        {
            auto* transform = m_player->getComponent<engine::component::TransformComponent>();
            if (transform)
            {
                glm::vec2 pos = transform->getPosition();
                std::string text = "X: " + std::to_string(static_cast<int>(pos.x)) +
                                   " Y: " + std::to_string(static_cast<int>(pos.y));

                auto glyphs = text_renderer->prepareText(text, 10.0f, 10.0f);
                auto& renderer = _context.getRenderer();

                for (const auto& glyph : glyphs)
                {
                    renderer.drawTexture(glyph.texture, glyph.x, glyph.y, glyph.w, glyph.h);
                }
            }
        }

        // ImGui滑块 + 武器显示
        if (m_glContext)
        {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            if (m_showFpsOverlay)
                renderPerformanceOverlay();

            renderSettingsPage();

            float displayW = ImGui::GetIO().DisplaySize.x;
            float displayH = ImGui::GetIO().DisplaySize.y;

            // ── 天气效果（背景层，位于所有ImGui窗口之下）──
            m_weatherSystem.render(displayW, displayH);

            // 相机缩放 + 天气控制
            ImGui::SetNextWindowPos(ImVec2(displayW - 260, 20), ImGuiCond_Always);
            ImGui::Begin("设置", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
            if (ImGui::SliderFloat(locale::T("game.zoom").c_str(), &m_zoomSliderValue, 0.5f, 3.0f))
                _context.getCamera().setZoom(m_zoomSliderValue);

            ImGui::Separator();
            ImGui::TextUnformatted("天气");
            ImGui::Text("星球: %s", game::route::RouteData::planetName(m_routeData.selectedPlanet));
            ImGui::Text("时间: %02d:%02d  %s",
                        m_timeOfDaySystem.getHour24(),
                        m_timeOfDaySystem.getMinute(),
                        m_timeOfDaySystem.getPhaseName());
            ImGui::Text("日照: %.0f%%", m_timeOfDaySystem.getDaylightFactor() * 100.0f);
            ImGui::Text("天体可见度: %.0f%%", m_weatherSystem.getSkyVisibility() * 100.0f);
            // 当前天气显示
            ImGui::Text("当前: %s", m_weatherSystem.getCurrentWeatherName());

            // 天气切换按钮
            using game::weather::WeatherType;
            const struct { WeatherType type; const char* label; } kWeathers[] = {
                { WeatherType::Clear,        "晴天" },
                { WeatherType::LightRain,    "小雨" },
                { WeatherType::MediumRain,   "中雨" },
                { WeatherType::HeavyRain,    "大雨" },
                { WeatherType::Thunderstorm, "雷雨" },
            };
            for (auto &w : kWeathers)
            {
                bool selected = (m_weatherSystem.getCurrentWeather() == w.type);
                if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
                if (ImGui::Button(w.label))
                    m_weatherSystem.setWeather(w.type, 3.0f);
                if (selected) ImGui::PopStyleColor();
                ImGui::SameLine();
            }
            ImGui::NewLine();
            ImGui::End();

            // 左侧武器栏（常显示）
            renderWeaponBar();

            // 左上角属性面板（血量 / 星能 / 属性）
            renderPlayerStatusHUD();

            // 底部居中星技 HUD
            renderSkillHUD();

            // 玩家头顶状态名
            renderPlayerStateTag();

            // 背包界面（E 键开关）
            renderInventoryUI();

            // 掉落物标注（悬浮文字）
            renderDropItems();

            // 左下角路线 HUD
            renderRouteHUD();

            // 撤离结算界面
            renderSettlementUI();
            renderCommandTerminal();
            renderMechPrompt();

            // 星球任务规划 UI
            {
                glm::vec2 ppos = getActorWorldPosition(getControlledActor());
                m_missionUI.render(*chunk_manager, ppos);
            }

            // 技能特效（前景层，覆盖在所有 ImGui 窗口之上）
            renderCombatEffects();
            renderSkillProjectiles();
            renderSkillVFX();
            if (m_showSkillDebugOverlay)
                renderSkillDebugOverlay();

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
    }

    void GameScene::renderActorGroundShadows()
    {
        engine::object::GameObject *shadowActor = getControlledActor();
        if (shadowActor)
        {
            auto *transform = shadowActor->getComponent<engine::component::TransformComponent>();
            auto *physics = shadowActor->getComponent<engine::component::PhysicsComponent>();
            if (transform)
            {
                glm::vec2 size = m_isPlayerInMech ? glm::vec2(38.0f, 11.0f) : glm::vec2(24.0f, 7.0f);
                float alpha = m_isPlayerInMech ? 0.20f : 0.17f;
                if (physics)
                {
                    float airFactor = std::min(std::abs(physics->getVelocity().y) / 8.0f, 1.0f);
                    alpha *= 1.0f - airFactor * 0.35f;
                    size *= 1.0f - airFactor * 0.20f;
                }

                glm::vec2 shadowCenter = transform->getPosition()
                    + glm::vec2(0.0f, m_isPlayerInMech ? 20.0f : 14.0f);
                drawWorldShadow(_context, shadowCenter, size, alpha);
            }
        }

        if (m_monsterManager)
            m_monsterManager->renderGroundShadows(_context);
    }

    void GameScene::handleInput()
    {
        Scene::handleInput();

        auto &input = _context.getInputManager();

        if (input.isActionPressed("command_mode"))
        {
            m_showCommandInput = !m_showCommandInput;
            m_focusCommandInput = m_showCommandInput;
            if (!m_showCommandInput)
                m_commandBuffer[0] = '\0';
        }

        if (m_showCommandInput)
            return;

        if (actor_manager)
            actor_manager->handleInput();

        glm::vec2 mousePos = input.getLogicalMousePosition();
        glm::vec2 worldPos = _context.getCamera().screenToWorld(mousePos);
        m_lastMouseLogicalPos = mousePos;
        m_lastMouseWorldPos = worldPos;
        m_hoveredTile = chunk_manager->worldToTile(worldPos);
        m_hasHoveredTile = true;
        glm::vec2 hoveredTileCenter = chunk_manager->tileToWorld(m_hoveredTile)
            + glm::vec2(chunk_manager->getTileSize()) * 0.5f;
        m_lastHoveredTileCenter = hoveredTileCenter;

        // 鼠标左键点击摧毁瓦片（树木会触发倒树逻辑）
        if (input.isActionPressed("attack"))
        {
            if (m_isPlayerInMech)
                performMechAttack();
            else
            {
                const auto &activeSlot = m_weaponBar.getActiveSlot();
                const auto *weaponDef = (!activeSlot.isEmpty() && activeSlot.item)
                    ? game::weapon::getWeaponDef(activeSlot.item->id)
                    : nullptr;

                if (weaponDef && weaponDef->attack_type == game::weapon::AttackType::Melee)
                {
                    performMeleeAttack(hoveredTileCenter);
                }
                else
                {
                    using engine::world::TileType;
                    TileType t = chunk_manager->tileAt(m_hoveredTile.x, m_hoveredTile.y).type;
                    if (t == TileType::Wood || t == TileType::Leaves)
                    {
                        m_treeManager.digTile(m_hoveredTile.x, m_hoveredTile.y,
                                              *chunk_manager, m_treeManager.getDrops());
                    }
                    else if (t == TileType::Ore)
                    {
                        chunk_manager->setTileSilent(m_hoveredTile.x, m_hoveredTile.y,
                                                     engine::world::TileData(TileType::Air));
                        chunk_manager->rebuildDirtyChunks();
                        using Cat = game::inventory::ItemCategory;
                        m_inventory.addItem({"ore", "矿石", 64, Cat::Material}, 1);
                    }
                    else
                    {
                        chunk_manager->setTileSilent(m_hoveredTile.x, m_hoveredTile.y,
                                                     engine::world::TileData(TileType::Air));
                        chunk_manager->rebuildDirtyChunks();
                    }

                    triggerAttackStarSkills(hoveredTileCenter);
                }
            }
        }

        // E键切换背包
        if (input.isActionPressed("open_inventory"))
            m_showInventory = !m_showInventory;

        // ~键切换设置页面
        if (input.isActionPressed("open_settings"))
            m_showSettings = !m_showSettings;

        // M键切换星球任务规划
        if (input.isActionPressed("open_map"))
            m_missionUI.showWindow = !m_missionUI.showWindow;

        // B键：在撤离区域触发结算
        if (input.isActionPressed("evacuate"))
        {
            if (m_routeData.isValid())
            {
                int lastZone = static_cast<int>(m_routeData.path.size()) - 1;
                if (m_currentZone >= lastZone)
                    m_showSettlement = true;
                else
                    spdlog::info("B键：还未到达撤离区 (当前区域 {}/{})", m_currentZone + 1, lastZone + 1);
            }
        }
        if (input.isActionPressed("interact"))
        {
            if (m_isPlayerInMech)
                exitMech();
            else
                tryEnterMech();
        }

        // Q键：主动展光冲刺星技
        if (input.isActionPressed("skill_use"))
            triggerActiveStarSkills();

        // 滚轮切换武器栏
        float wheel = input.getMouseWheelDelta();
        if (wheel > 0.5f)
            m_weaponBar.scroll(-1);   // 向上滚 → 上一个
        else if (wheel < -0.5f)
            m_weaponBar.scroll(1);    // 向下滚 → 下一个
    }

    void GameScene::clean()
    {
        Scene::clean();

        if (m_glContext)
        {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            m_glContext = nullptr;
        }
    }
    void GameScene::createTestObject()
    {
        for (int i = 0; i < 50; i += 32)
        {
            for (int j = 0; j < 50; j += 32)
            {
                auto test_object = std::make_unique<engine::object::GameObject>(_context, "test_object");
                test_object->addComponent<engine::component::TransformComponent>(glm::vec2(i, j));
                test_object->addComponent<engine::component::SpriteComponent>("assets/textures/Props/bubble1.svg", engine::utils::Alignment::CENTER);
                addGameObject(std::move(test_object));
            }
        }
    }

    void GameScene::testCamera()
    {
        auto &camera = engine::core::Context::Current->getCamera();
        auto &input_manager = engine::core::Context::Current->getInputManager();
        if (input_manager.isActionDown("move_up"))
            camera.move(glm::vec2(0, -1));
        if (input_manager.isActionDown("move_down"))
            camera.move(glm::vec2(0, 1));
        if (input_manager.isActionDown("move_left"))
            camera.move(glm::vec2(-1, 0));
        if (input_manager.isActionDown("move_right"))
            camera.move(glm::vec2(1, 0));
    }

    // ------------------------------------------------------------------
    //  矿脉注入（目标区域在路线时，在对应世界列注入 Ore 瓦片）
    // ------------------------------------------------------------------
    void GameScene::injectOreVeins()
    {
        if (!m_routeData.isValid() || m_routeData.objectiveZone < 0) return;

        const int oz = m_routeData.objectiveZone;
        int startTileX = oz * game::route::RouteData::TILES_PER_CELL;
        int endTileX   = startTileX + game::route::RouteData::TILES_PER_CELL;
        int centerX    = (startTileX + endTileX) / 2;

        // 用世界种子生成矿脉位置
        uint64_t rngState = static_cast<uint64_t>(m_worldConfig.seed) ^ 0xBEEF1234CAFE5678ULL;
        auto rng = [&](int lo, int hi) -> int {
            rngState ^= rngState << 13;
            rngState ^= rngState >> 7;
            rngState ^= rngState << 17;
            return lo + static_cast<int>(rngState % static_cast<uint64_t>(hi - lo + 1));
        };

        using engine::world::TileType;
        using engine::world::TileData;

        int seaLevel = m_worldConfig.seaLevel;
        int clusters = rng(4, 7);
        bool hasOreChanges = false;
        for (int c = 0; c < clusters; ++c)
        {
            int cx = centerX + rng(-30, 30);
            int cy = seaLevel + rng(6, 18);  // 地表以下
            int clusterSize = rng(5, 10);
            for (int i = 0; i < clusterSize; ++i)
            {
                int tx = cx + rng(-3, 3);
                int ty = cy + rng(-2, 2);
                if (chunk_manager->tileAt(tx, ty).type == TileType::Stone)
                {
                    chunk_manager->setTileSilent(tx, ty, TileData(TileType::Ore));
                    hasOreChanges = true;
                }
            }
        }
        if (hasOreChanges)
            chunk_manager->rebuildDirtyChunks();
        spdlog::info("矿脉注入完毕：目标区域 zone={}, X=[{},{}), {} 个矿团",
            oz, startTileX, endTileX, clusters);
    }

    // ------------------------------------------------------------------
    //  撤离结算界面
    // ------------------------------------------------------------------
    void GameScene::renderSettlementUI()
    {
        if (!m_showSettlement) return;

        ImGuiIO &io = ImGui::GetIO();
        float dw = io.DisplaySize.x;
        float dh = io.DisplaySize.y;

        // 半透明全屏遮罩
        ImDrawList *bg = ImGui::GetBackgroundDrawList();
        bg->AddRectFilled({0, 0}, {dw, dh}, IM_COL32(0, 0, 0, 160));

        float winW = 400.0f, winH = 360.0f;
        ImGui::SetNextWindowPos({(dw - winW) * 0.5f, (dh - winH) * 0.5f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({winW, winH}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.95f);
        ImGui::Begin("##settlement", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav);

        // 标题
        ImGui::SetCursorPosX((winW - ImGui::CalcTextSize("任务结算").x) * 0.5f);
        ImGui::TextColored({0.4f, 1.0f, 0.5f, 1.0f}, "任务结算");
        ImGui::Separator();
        ImGui::Spacing();

        // 路线信息
        if (m_routeData.isValid())
        {
            ImGui::TextColored({0.6f, 0.8f, 1.0f, 1.0f}, "路线概况");
            ImGui::Text("  出发点: %s", game::route::RouteData::cellLabel(m_routeData.startCell()).c_str());
            ImGui::Text("  撤离点: %s", game::route::RouteData::cellLabel(m_routeData.evacCell()).c_str());
            ImGui::Text("  路线步数: %d 格", static_cast<int>(m_routeData.path.size()));
            bool visitedObjective = (m_routeData.objectiveZone >= 0);
            ImGui::Text("  目标矿区: %s",
                visitedObjective ? "已经过 [矿山]" : "未经过（路线绕开了目标格）");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored({0.6f, 0.8f, 1.0f, 1.0f}, "收集物品");
        ImGui::Spacing();

        // 显示背包中各种材料数量
        const struct { const char* id; const char* name; } kItems[] = {
            {"ore",    "矿石"},
            {"wood",   "木材"},
            {"stone",  "石块"},
            {"leaves", "树叶"},
        };
        bool anyItem = false;
        for (auto &it : kItems)
        {
            int cnt = m_inventory.countItem(it.id);
            if (cnt > 0)
            {
                ImGui::Text("  %s  x%d", it.name, cnt);
                anyItem = true;
            }
        }
        if (!anyItem)
            ImGui::TextDisabled("  （背包为空）");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // 返回主菜单按钮
        float btnW = winW - 32.0f;
        ImGui::SetCursorPosX(16.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,        {0.12f, 0.50f, 0.15f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.20f, 0.70f, 0.22f, 1.0f});
        if (ImGui::Button("返回飞船", {btnW, 40.0f}))
        {
            auto ss = std::make_unique<game::scene::ShipScene>("ShipScene", _context, _scene_manager);
            _scene_manager.requestReplaceScene(std::move(ss));
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing();
        ImGui::SetCursorPosX(16.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, {0.25f, 0.25f, 0.35f, 1.0f});
        if (ImGui::Button("继续游戏", {btnW, 30.0f}))
            m_showSettlement = false;
        ImGui::PopStyleColor();

        ImGui::End();
    }

    // ------------------------------------------------------------------
    //  玩家朝向 / 动画资源同步
    // ------------------------------------------------------------------
    void GameScene::syncPlayerPresentation()
    {
        if (!m_player) return;

        auto* controller = m_player->getComponent<engine::component::ControllerComponent>();
        auto* sprite = m_player->getComponent<engine::component::SpriteComponent>();
        if (!controller || !sprite) return;

        bool shouldFlip = controller->getFacingDirection() == engine::component::ControllerComponent::FacingDirection::Left;
        if (sprite->isFlipped() != shouldFlip)
            sprite->setFlipped(shouldFlip);

        // 将动画状态委托给 AnimationComponent（组件自身 update 驱动帧推进）
        const std::string animKey = controller->getAnimationStateKey();
        if (auto* anim = m_player->getComponent<engine::component::AnimationComponent>())
            anim->play(animKey);

        if (m_mech)
        {
            auto* mechController = m_mech->getComponent<engine::component::ControllerComponent>();
            auto* mechSprite = m_mech->getComponent<engine::component::SpriteComponent>();
            if (mechController && mechSprite)
            {
                // 朝向翻转
                bool mechFlip = mechController->getFacingDirection() == engine::component::ControllerComponent::FacingDirection::Left;
                if (mechSprite->isFlipped() != mechFlip)
                    mechSprite->setFlipped(mechFlip);

                // 动画帧驱动（精灵表 172×256，3行）
                constexpr float MECH_W = 172.0f;
                constexpr float MECH_H = 256.0f;
                const float dt = ImGui::GetIO().DeltaTime;

                if (m_mechShootTimer > 0.0f)
                    m_mechShootTimer = std::max(0.0f, m_mechShootTimer - dt);

                using State = engine::component::ControllerComponent::MovementState;
                const auto mechState = mechController->getMovementState();

                int targetRow; int maxFrames; float frameDuration;
                if (m_mechShootTimer > 0.0f)
                {
                    targetRow = 2; maxFrames = 7; frameDuration = 0.07f;
                }
                else if (mechState == State::Run || mechState == State::Jump || mechState == State::Fall)
                {
                    targetRow = 0; maxFrames = 8; frameDuration = 0.10f;
                }
                else
                {
                    targetRow = 1; maxFrames = 6; frameDuration = 0.15f;
                }

                if (targetRow != m_mechAnimRow)
                {
                    m_mechAnimRow   = targetRow;
                    m_mechAnimFrame = 0;
                    m_mechAnimTimer = 0.0f;
                }
                m_mechAnimTimer += dt;
                if (m_mechAnimTimer >= frameDuration)
                {
                    m_mechAnimTimer -= frameDuration;
                    ++m_mechAnimFrame;
                    if (m_mechAnimFrame >= maxFrames)
                        m_mechAnimFrame = (targetRow == 2) ? maxFrames - 1 : 0;
                }

                const float srcX = static_cast<float>(m_mechAnimFrame) * MECH_W;
                const float srcY = static_cast<float>(m_mechAnimRow)   * MECH_H;
                mechSprite->setSourceRect(engine::utils::FRect{{srcX, srcY}, {MECH_W, MECH_H}});
            }
        }

        // 属性系统 → 控制器参数同步（每帧，保证淨畫 buff 立即生效）
        if (auto* attr = m_player->getComponent<game::component::AttributeComponent>())
        {
            if (auto* ctrl = m_player->getComponent<engine::component::ControllerComponent>())
            {
                constexpr float BASE_SPEED = 25.0f;
                constexpr float BASE_JUMP  = 8.0f;
                ctrl->setSpeed(BASE_SPEED * attr->get(game::component::StatType::Speed));
                ctrl->setJumpSpeed(BASE_JUMP * attr->get(game::component::StatType::JumpPower));
            }
        }
    }

    void GameScene::renderCommandTerminal()
    {
        if (!m_showCommandInput)
            return;

        ImGuiIO &io = ImGui::GetIO();
        const float width = 420.0f;
        ImGui::SetNextWindowPos({(io.DisplaySize.x - width) * 0.5f, 40.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({width, 118.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.94f);
        ImGui::Begin("指令终端", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted("输入 001 呼叫空投机甲。再次按 R 可退出指令模式。") ;
        ImGui::Spacing();
        if (m_focusCommandInput)
        {
            ImGui::SetKeyboardFocusHere();
            m_focusCommandInput = false;
        }

        bool submitted = ImGui::InputText("##command", m_commandBuffer.data(), m_commandBuffer.size(),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("执行") || submitted)
            executeCommand();

        ImGui::TextDisabled("当前可用指令: 001") ;
        ImGui::End();
    }

    void GameScene::renderSettingsPage()
    {
        if (!m_showSettings) return;

        // 查询进程总 RSS（macOS mach API）
        struct mach_task_basic_info taskInfo;
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        size_t rss = 0;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                      reinterpret_cast<task_info_t>(&taskInfo), &count) == KERN_SUCCESS)
            rss = taskInfo.resident_size;

        // 各模块估算
        size_t chunkCount  = chunk_manager ? chunk_manager->loadedChunkCount() : 0;
        // 每个 chunk：16×16 tiles + GL VBO（约 16×16×6顶点×4浮点×4字节）
        size_t chunkMemEst = chunkCount * (sizeof(engine::world::Chunk) + 16 * 16 * 6 * 4 * sizeof(float));

        size_t actorCount  = actor_manager ? actor_manager->actorCount() : 0;
        size_t actorMemEst = actorCount * 2048; // 粗估每个 Actor ~2 KB（组件+对象）

        size_t monsterCount  = m_monsterManager ? m_monsterManager->monsterCount() : 0;
        size_t monsterMemEst = monsterCount * 2048;

        size_t dropCount  = m_treeManager.getDrops().size();
        size_t dropMemEst = dropCount * 256;

        ImGui::SetNextWindowSize({560.0f, 420.0f}, ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(
            {ImGui::GetIO().DisplaySize.x * 0.5f - 280.0f,
             ImGui::GetIO().DisplaySize.y * 0.5f - 210.0f},
            ImGuiCond_Appearing);
        if (!ImGui::Begin("设置  [按 ` 关闭]", &m_showSettings))
        {
            ImGui::End();
            return;
        }

        ImGui::SeparatorText("内存使用");

        // 总进程 RSS
        auto fmtMB = [](size_t bytes) -> float { return static_cast<float>(bytes) / (1024.0f * 1024.0f); };

        ImGui::Text("进程总 RSS: %.2f MB", fmtMB(rss));
        ImGui::Spacing();

        if (ImGui::BeginTable("##mem_table", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("模块",   ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("数量",   ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("估算内存", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableHeadersRow();

            auto row = [&](const char *name, size_t cnt, size_t mem) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", cnt);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f MB", fmtMB(mem));
            };

            row("区块 (Chunk)",       chunkCount,   chunkMemEst);
            row("Actor",              actorCount,   actorMemEst);
            row("怪物 (Monster)",     monsterCount, monsterMemEst);
            row("掉落物 (Drop Item)", dropCount,    dropMemEst);

            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("* 模块内存为估算值，总 RSS 为实际物理内存占用。");

        ImGui::SeparatorText("图形设置");
        if (ImGui::Checkbox("显示帧率", &m_showFpsOverlay))
        {
            // 实时生效，无需额外操作
        }
        if (ImGui::Checkbox("显示技能调试", &m_showSkillDebugOverlay))
        {
            saveBoolSetting("show_skill_debug_overlay", m_showSkillDebugOverlay);
        }
        if (ImGui::Checkbox("高亮活跃地形块", &m_showActiveChunkHighlights))
        {
            saveBoolSetting("show_active_chunk_highlights", m_showActiveChunkHighlights);
        }

        ImGui::End();
    }

    void GameScene::renderPerformanceOverlay() const    {
        ImGui::SetNextWindowPos({10.0f, 10.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.45f);
        ImGui::Begin("##fps_game", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::End();
    }

    void GameScene::renderMechPrompt()
    {
        if (!m_mech)
            return;

        if (m_isPlayerInMech)
        {
            ImGuiIO &io = ImGui::GetIO();
            const float width = 280.0f;
            ImGui::SetNextWindowPos({(io.DisplaySize.x - width) * 0.5f, io.DisplaySize.y - 110.0f}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({width, 78.0f}, ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.84f);
            ImGui::Begin("##mechhud", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                ImGuiWindowFlags_NoSavedSettings);
            ImGui::TextUnformatted("F 下机   鼠标左键 震荡重击");
            if (m_mechAttackCooldown > 0.0f)
                ImGui::Text("重击冷却: %.2fs", m_mechAttackCooldown);
            else
                ImGui::TextColored({0.55f, 1.0f, 0.7f, 1.0f}, "重击就绪");
            if (m_mechLastAttackHits > 0)
                ImGui::Text("上次重击命中: %d", m_mechLastAttackHits);
            else
                ImGui::TextDisabled("上次重击未命中目标");
            ImGui::End();
            return;
        }

        if (!m_player)
            return;

        glm::vec2 playerPos = getActorWorldPosition(m_player);
        glm::vec2 mechPos = getActorWorldPosition(m_mech);
        if (glm::distance(playerPos, mechPos) > 220.0f)
            return;

        glm::vec2 screenLogical = _context.getCamera().worldToScreen({mechPos.x, mechPos.y - 220.0f});
        ImVec2 screen = logicalToImGuiScreen(_context, screenLogical);
        const char* text = "按 F 进入机甲";
        ImDrawList *dl = ImGui::GetForegroundDrawList();
        ImVec2 textSize = ImGui::CalcTextSize(text);
        ImVec2 min{screen.x - textSize.x * 0.5f - 10.0f, screen.y - 6.0f};
        ImVec2 max{screen.x + textSize.x * 0.5f + 10.0f, screen.y + textSize.y + 6.0f};
        dl->AddRectFilled(min, max, IM_COL32(9, 17, 26, 220), 8.0f);
        dl->AddRect(min, max, IM_COL32(120, 220, 255, 220), 8.0f, 0, 1.2f);
        dl->AddText({screen.x - textSize.x * 0.5f, screen.y}, IM_COL32(240, 248, 255, 255), text);
    }

    // ------------------------------------------------------------------
    //  玩家状态标签（显示在角色头顶）
    // ------------------------------------------------------------------
    void GameScene::renderPlayerStateTag()
    {
        auto* actor = getControlledActor();
        if (!actor) return;

        auto* transform = actor->getComponent<engine::component::TransformComponent>();
        auto* controller = actor->getComponent<engine::component::ControllerComponent>();
        if (!transform || !controller) return;

        glm::vec2 screenLogical = _context.getCamera().worldToScreen(transform->getPosition());
        ImVec2 screen = logicalToImGuiScreen(_context, screenLogical);
        std::string stateLabel = controller->getMovementStateName();
        if (m_isPlayerInMech)
            stateLabel = "机甲·" + stateLabel;
        float fuelRatio = controller->getJetpackFuelRatio();

        ImDrawList *dl = ImGui::GetForegroundDrawList();
        ImVec2 textSize = ImGui::CalcTextSize(stateLabel.c_str());
        ImVec2 textPos{screen.x - textSize.x * 0.5f, screen.y - 58.0f};
        ImVec2 boxMin{textPos.x - 8.0f, textPos.y - 4.0f};
        ImVec2 boxMax{textPos.x + textSize.x + 8.0f, textPos.y + textSize.y + 14.0f};

        dl->AddRectFilled(boxMin, boxMax, IM_COL32(12, 18, 30, 210), 6.0f);
        dl->AddRect(boxMin, boxMax, IM_COL32(100, 180, 255, 220), 6.0f, 0, 1.2f);
        dl->AddText(textPos, IM_COL32(240, 245, 255, 255), stateLabel.c_str());

        ImVec2 fuelBarMin{boxMin.x + 6.0f, boxMax.y - 8.0f};
        ImVec2 fuelBarMax{boxMax.x - 6.0f, boxMax.y - 3.0f};
        float fuelFillX = fuelBarMin.x + (fuelBarMax.x - fuelBarMin.x) * fuelRatio;
        dl->AddRectFilled(fuelBarMin, fuelBarMax, IM_COL32(30, 36, 46, 230), 2.0f);
        dl->AddRectFilled(fuelBarMin, {fuelFillX, fuelBarMax.y}, IM_COL32(255, 190, 60, 230), 2.0f);
    }

    // ------------------------------------------------------------------
    //  路线 HUD（左下角，显示 20×20 路线进度）
    // ------------------------------------------------------------------
    void GameScene::renderRouteHUD()
    {
        if (!m_routeData.isValid()) return;

        using namespace game::route;
        using R = RouteData;

        const float CELL_PX  = 6.0f;
        const float CELL_TOT = CELL_PX + 1.0f;      // 7px/格
        const float MAP_W    = R::MAP_SIZE * CELL_TOT;
        const float MAP_H    = R::MAP_SIZE * CELL_TOT;
        const float WIN_W    = MAP_W + 14.0f;
        const float WIN_H    = MAP_H + 46.0f;

        float dh = ImGui::GetIO().DisplaySize.y;
        ImGui::SetNextWindowPos({10.0f, dh - WIN_H - 10.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({WIN_W, WIN_H}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.82f);
        ImGui::Begin("##routehud", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoNav         | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        // 标题
        ImGui::TextColored({0.5f, 0.9f, 1.0f, 1.0f}, "路线进度");
        ImGui::SameLine(WIN_W - 40.0f);
        ImGui::TextDisabled("%d步", static_cast<int>(m_routeData.path.size()));

        ImDrawList *dl    = ImGui::GetWindowDrawList();
        ImVec2      origin = ImGui::GetCursorScreenPos();
        origin.x += 4.0f;

        int psize = static_cast<int>(m_routeData.path.size());

        // 绘制微型 20×20 地图
        for (int ry = 0; ry < R::MAP_SIZE; ++ry)
        {
            for (int cx = 0; cx < R::MAP_SIZE; ++cx)
            {
                float px = origin.x + cx * CELL_TOT;
                float py = origin.y + ry * CELL_TOT;
                ImVec2 p0{px, py}, p1{px + CELL_PX, py + CELL_PX};

                glm::ivec2 cell{cx, ry};
                int pidx = -1;
                for (int i = 0; i < psize; ++i)
                    if (m_routeData.path[i] == cell) { pidx = i; break; }

                bool isObjective = (cell == m_routeData.objectiveCell);

                ImU32 fill;
                if (pidx == 0)
                    fill = IM_COL32(40, 200, 80, 255);   // 出发
                else if (pidx == psize - 1)
                    fill = IM_COL32(220, 70, 70, 255);   // 撤离
                else if (pidx == m_currentZone)
                    fill = IM_COL32(255, 220, 50, 255);  // 当前区域
                else if (pidx >= 0)
                {
                    auto tc = game::route::RouteData::terrainColor(m_routeData.terrain[ry][cx]);
                    fill = IM_COL32(tc.r/3*2, tc.g/3*2+30, tc.b/3*2+60, 210);  // 路线（地形色调）
                }
                else
                {
                    auto tc = game::route::RouteData::terrainColor(m_routeData.terrain[ry][cx]);
                    fill = IM_COL32(tc.r/3, tc.g/3, tc.b/3, 160);  // 空格（暗淡地形）
                }

                dl->AddRectFilled(p0, p1, fill, 1.0f);

                // 目标格金色边框
                if (isObjective)
                    dl->AddRect(p0, p1, IM_COL32(255, 210, 50, 220), 1.0f, 0, 1.5f);

                // 当前区域加白色边框
                else if (pidx == m_currentZone)
                    dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 220), 1.0f, 0, 1.5f);
            }
        }

        // 路线连线
        for (int i = 0; i + 1 < psize; ++i)
        {
            const auto &a = m_routeData.path[i];
            const auto &b = m_routeData.path[i + 1];
            ImVec2 pa{origin.x + a.x * CELL_TOT + CELL_PX * 0.5f,
                      origin.y + a.y * CELL_TOT + CELL_PX * 0.5f};
            ImVec2 pb{origin.x + b.x * CELL_TOT + CELL_PX * 0.5f,
                      origin.y + b.y * CELL_TOT + CELL_PX * 0.5f};
            ImU32 lcol = (i < m_currentZone)
                ? IM_COL32(100, 180, 120, 120)  // 已过
                : IM_COL32(255, 230, 80, 160);  // 未过
            dl->AddLine(pa, pb, lcol, 1.5f);
        }

        // 占位，让光标移到文字行
        ImGui::Dummy({MAP_W, MAP_H});

        // 当前位置文字
        if (m_currentZone < psize)
        {
            const auto &curCell = m_routeData.path[m_currentZone];
            bool isLast = (m_currentZone == psize - 1);
            ImGui::TextColored(
                isLast ? ImVec4{0.3f, 1.0f, 0.4f, 1.0f} : ImVec4{1.0f, 0.9f, 0.4f, 1.0f},
                "%s第%d/%d步 %s%s",
                isLast ? "[" : "",
                m_currentZone + 1,
                psize,
                RouteData::cellLabel(curCell).c_str(),
                isLast ? "] 撤离点" : "");
        }

        ImGui::End();
    }

    // ------------------------------------------------------------------
    //  掉落物渲染（屏幕空间悬浮标签）
    // ------------------------------------------------------------------
    void GameScene::renderDropItems()
    {
        const auto &camera = _context.getCamera();
        const auto &drops  = m_treeManager.getDrops();
        if (drops.empty()) return;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {2.0f, 2.0f});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.0f, 0.0f, 0.0f, 0.55f});

        for (size_t i = 0; i < drops.size(); ++i)
        {
            const auto &drop = drops[i];
            // 世界坐标 → 屏幕坐标
            glm::vec2 screenLogical = camera.worldToScreen(drop.worldPos);
            ImVec2 screen = logicalToImGuiScreen(_context, screenLogical);

            // 上下轻微飘动
            float bx = screen.x - 16.0f;
            float by = screen.y - 20.0f + std::sin(drop.bobTimer) * 3.0f;

            char wid[32];
            snprintf(wid, sizeof(wid), "##drop%zu", i);
            ImGui::SetNextWindowPos({bx, by}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({40.0f, 20.0f}, ImGuiCond_Always);
            ImGui::Begin(wid, nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

            // 根据物品类型选颜色
            ImVec4 col = (drop.item.id == "wood")
                ? ImVec4(0.85f, 0.65f, 0.30f, 1.0f)
                : ImVec4(0.40f, 0.90f, 0.40f, 1.0f);
            ImGui::TextColored(col, "%s x%d", drop.item.name.c_str(), drop.count);
            ImGui::End();
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // ------------------------------------------------------------------
    //  初始化测试物品
    // ------------------------------------------------------------------
    void GameScene::initTestItems()
    {
        using namespace game::inventory;
        using Cat = ItemCategory;

        m_inventory.addItem({"alloy_greatsword",  "合金巨剑",   1, Cat::Weapon}, 1);
        auto &slot0 = m_weaponBar.getSlot(0);
        slot0.item = Item{"alloy_greatsword", "合金巨剑", 1, Cat::Weapon};
        slot0.count = 1;
        m_weaponBar.setActiveIndex(0);

        // 普通物品
        m_inventory.addItem({"gold_coin", "金币",   99, Cat::Misc},     42);
        m_inventory.addItem({"apple",     "苹果",   20, Cat::Consumable}, 5);
        m_inventory.addItem({"wood",      "木材",   64, Cat::Material},  32);
        m_inventory.addItem({"stone",     "石头",   64, Cat::Material},  24);

        // 星技珠子（StarSkill 类型，可放入星技圆形槽）
        m_inventory.addItem({"star_fire",  "炎焰星技", 1, Cat::StarSkill}, 2);
        m_inventory.addItem({"star_ice",   "寒冰星技", 1, Cat::StarSkill}, 1);
        m_inventory.addItem({"star_wind",  "疾风星技", 1, Cat::StarSkill}, 1);
        m_inventory.addItem({"star_light", "闪光星技", 1, Cat::StarSkill}, 1);
    }

    // ------------------------------------------------------------------
    //  左侧武器栏（常驻显示）
    // ------------------------------------------------------------------
    void GameScene::renderWeaponBar()
    {
        constexpr float SLOT_H  = 58.0f;
        constexpr float SLOT_W  = 70.0f;
        constexpr float GAP     = 3.0f;
        constexpr int   SLOTS   = game::weapon::WeaponBar::SLOTS;

        const float WIN_H = SLOTS * SLOT_H + (SLOTS - 1) * GAP + 46.0f;
        ImGui::SetNextWindowPos({10.0f, 180.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({SLOT_W + 16.0f, WIN_H}, ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {GAP, GAP});
        ImGui::Begin(locale::T("weapon_bar.title").c_str(), nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

        for (int i = 0; i < SLOTS; ++i)
        {
            const auto &slot = m_weaponBar.getSlot(i);
            bool is_active   = (i == m_weaponBar.getActiveIndex());

            // 激活槽用黄色高亮边框
            if (is_active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button,        {0.55f, 0.45f, 0.05f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.70f, 0.58f, 0.10f, 1.0f});
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button,        {0.18f, 0.18f, 0.28f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.30f, 0.30f, 0.50f, 1.0f});
            }

            char label[64];
            if (slot.isEmpty())
                snprintf(label, sizeof(label), "%d##wb%d", i + 1, i);
            else
            {
                const auto *def = game::weapon::getWeaponDef(slot.item->id);
                snprintf(label, sizeof(label), "%s\n%s##wb%d",
                    def ? def->icon_label.c_str() : "?",
                    slot.item->name.c_str(), i);
            }

            ImGui::Button(label, {SLOT_W, SLOT_H});

            // 武器栏格子作为拖放目标（接受背包物品）
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("INV_SLOT"))
                {
                    int src_idx = *static_cast<const int *>(payload->Data);
                    m_weaponBar.equipFromInventory(i, src_idx, m_inventory);
                }
                ImGui::EndDragDropTarget();
            }

            // 双击卸装回背包
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                m_weaponBar.unequipToInventory(i, m_inventory);

            // Tooltip
            if (!slot.isEmpty() && ImGui::IsItemHovered())
            {
                const auto *def = game::weapon::getWeaponDef(slot.item->id);
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(slot.item->name.c_str());
                if (def)
                {
                    ImGui::Separator();
                    ImGui::TextDisabled("%s: %d", locale::T("weapon.damage").c_str(), def->damage);
                    ImGui::TextDisabled("%s: %.1f/s", locale::T("weapon.speed").c_str(), def->attack_speed);
                    if (def->ammo_capacity > 0)
                        ImGui::TextDisabled("%s: %d", locale::T("weapon.ammo").c_str(), def->ammo_capacity);
                }
                ImGui::TextDisabled("%s", locale::T("weapon_bar.dblclick_unequip").c_str());
                ImGui::EndTooltip();
            }

            ImGui::PopStyleColor(2);
        }

        // 底部提示
        ImGui::Separator();
        ImGui::TextDisabled("%s", locale::T("weapon_bar.scroll_hint").c_str());

        ImGui::End();
        ImGui::PopStyleVar();
    }

    // ------------------------------------------------------------------
    //  星技圆形槽页面（在背包窗口 "星技" 标签页内渲染）
    // ------------------------------------------------------------------
    void GameScene::renderStarSocketPage()
    {
        constexpr int   COUNT  = 6;
        constexpr float RING_R = 130.0f;   // 槽围绕中心的半径
        constexpr float SLOT_R = 28.0f;    // 每个圆形槽的半径
        constexpr float CHAR_R = 48.0f;    // 中央"角色"圆的半径
        constexpr float PI     = 3.14159265f;

        auto* drawList = ImGui::GetWindowDrawList();
        ImVec2 avail   = ImGui::GetContentRegionAvail();
        ImVec2 origin  = ImGui::GetCursorScreenPos();

        // 中心点：可用区域中央
        float cx = origin.x + avail.x * 0.5f;
        float cy = origin.y + avail.y * 0.5f;

        // 预先计算各槽坐标
        float sx[COUNT], sy[COUNT];
        for (int i = 0; i < COUNT; ++i)
        {
            float angle = i * (2.0f * PI / COUNT) - PI * 0.5f; // 从正上方开始
            sx[i] = cx + RING_R * cosf(angle);
            sy[i] = cy + RING_R * sinf(angle);
        }

        // ── 绘制连接线（在按钮之前画，层次靠后）──────────────────────
        for (int i = 0; i < COUNT; ++i)
            drawList->AddLine({cx, cy}, {sx[i], sy[i]}, IM_COL32(60, 100, 160, 100), 1.5f);

        // ── 绘制中央角色圆 ────────────────────────────────────────────
        drawList->AddCircleFilled({cx, cy}, CHAR_R, IM_COL32(30, 50, 80, 230));
        drawList->AddCircle({cx, cy}, CHAR_R, IM_COL32(100, 180, 255, 200), 48, 2.5f);
        {
            const char* lbl = "角色";
            ImVec2 ls = ImGui::CalcTextSize(lbl);
            drawList->AddText({cx - ls.x * 0.5f, cy - ls.y * 0.5f},
                              IM_COL32(180, 220, 255, 255), lbl);
        }

        // ── 处理每个星技槽 ────────────────────────────────────────────
        for (int i = 0; i < COUNT; ++i)
        {
            auto& slot    = m_starSockets[i];
            bool occupied = !slot.isEmpty();

            // 先画槽背景圆（层次在 InvisibleButton 之前）
            ImU32 bgCol   = occupied ? IM_COL32(40, 80, 140, 230) : IM_COL32(20, 30, 55, 200);
            ImU32 ringCol = occupied ? IM_COL32(100, 200, 255, 255) : IM_COL32(80, 120, 200, 150);
            drawList->AddCircleFilled({sx[i], sy[i]}, SLOT_R, bgCol);
            drawList->AddCircle({sx[i], sy[i]}, SLOT_R, ringCol, 32, 2.0f);

            // 槽编号（右上角小字）
            char numBuf[4];
            snprintf(numBuf, sizeof(numBuf), "%d", i + 1);
            ImVec2 numSz = ImGui::CalcTextSize(numBuf);
            drawList->AddText({sx[i] - numSz.x * 0.5f, sy[i] - SLOT_R + 4.0f},
                              IM_COL32(120, 160, 200, 140), numBuf);

            // InvisibleButton 用于接收鼠标事件
            char btnId[16];
            snprintf(btnId, sizeof(btnId), "##ss%d", i);
            ImGui::SetCursorScreenPos({sx[i] - SLOT_R, sy[i] - SLOT_R});
            ImGui::InvisibleButton(btnId, {SLOT_R * 2.0f, SLOT_R * 2.0f});

            // 拖放源：已装备的星技珠可拖出
            if (occupied && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                ImGui::SetDragDropPayload("STAR_SLOT", &i, sizeof(int));
                ImGui::TextUnformatted(slot.item->name.c_str());
                ImGui::TextDisabled("拖至背包取下");
                ImGui::EndDragDropSource();
            }

            // 拖放目标：接受来自背包的 INV_SLOT（仅 StarSkill 类型）
            //           或来自其他星技槽的 STAR_SLOT（调换位置）
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("INV_SLOT"))
                {
                    int srcIdx = *static_cast<const int*>(p->Data);
                    auto& src  = m_inventory.getSlot(srcIdx);
                    if (!src.isEmpty() &&
                        src.item->category == game::inventory::ItemCategory::StarSkill)
                    {
                        if (occupied)                         // 槽已有珠子：退还到背包
                            m_inventory.addItem(*slot.item, slot.count);
                        slot.item  = src.item;
                        slot.count = 1;
                        src.item.reset();
                        src.count = 0;
                    }
                }
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("STAR_SLOT"))
                {
                    int srcSocket = *static_cast<const int*>(p->Data);
                    if (srcSocket != i)
                        std::swap(m_starSockets[srcSocket], m_starSockets[i]);
                }
                ImGui::EndDragDropTarget();
            }

            // Tooltip
            if (occupied && ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(slot.item->name.c_str());
                ImGui::TextDisabled("星技珠子");
                ImGui::TextDisabled("拖拽至背包可取下");
                ImGui::EndTooltip();
            }

            // 物品名称文字（画在 InvisibleButton 之后，层次在最上面）
            if (occupied)
            {
                const char* name = slot.item->name.c_str();
                ImVec2 ns = ImGui::CalcTextSize(name);
                if (ns.x <= SLOT_R * 2.0f - 4.0f)
                    drawList->AddText({sx[i] - ns.x * 0.5f, sy[i] - ns.y * 0.5f},
                                      IM_COL32(200, 240, 255, 255), name);
            }
            else
            {
                const char* plus = "+";
                ImVec2 ps = ImGui::CalcTextSize(plus);
                drawList->AddText({sx[i] - ps.x * 0.5f, sy[i] - ps.y * 0.5f},
                                  IM_COL32(80, 120, 200, 160), plus);
            }
        }

        // 占位 Dummy，避免布局高度为 0
        ImGui::SetCursorScreenPos({origin.x, origin.y + avail.y - 2.0f});
        ImGui::Dummy({avail.x, 1.0f});
    }

    void GameScene::renderInventoryUI()
    {
        if (!m_showInventory) return;

        constexpr int   COLS    = game::inventory::Inventory::COLS;
        constexpr int   ROWS    = game::inventory::Inventory::ROWS;
        constexpr float SLOT    = 54.0f;
        constexpr float GAP     = 4.0f;

        // 技能格子：2列×3行 = 6格，位于背包右侧
        constexpr int   SK_COLS     = 2;
        constexpr int   SK_ROWS     = 3;
        constexpr int   SK_COUNT    = SK_COLS * SK_ROWS;
        constexpr float PANEL_GAP   = 12.0f;  // 背包网格与技能面板之间的间距

        const float INV_W   = COLS * SLOT + (COLS - 1) * GAP;
        const float SKILL_W = SK_COLS * SLOT + (SK_COLS - 1) * GAP;
        const float WIN_W   = INV_W + PANEL_GAP + SKILL_W + 16.0f;  // +16 为窗口内边距
        const float WIN_H   = ROWS * SLOT + (ROWS - 1) * GAP + 76.0f;

        ImVec2 disp = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos({(disp.x - WIN_W) * 0.5f, (disp.y - WIN_H) * 0.5f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({WIN_W, WIN_H}, ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,  {GAP, GAP});
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {2.0f, 2.0f});

        ImGui::Begin(locale::T("inventory.title").c_str(), &m_showInventory,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse);

        if (ImGui::BeginTabBar("##inv_tabs"))
        {
            // ── 背包标签 ──────────────────────────────────────────────
            if (ImGui::BeginTabItem("背包"))
            {
                // ── 左侧：背包网格 ────────────────────────────────────
                ImGui::BeginChild("##inv_grid", {INV_W, 0.0f}, false,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                for (int row = 0; row < ROWS; ++row)
                {
                    for (int col = 0; col < COLS; ++col)
                    {
                        if (col > 0) ImGui::SameLine();

                        int idx = row * COLS + col;
                        auto &slot = m_inventory.getSlot(idx);

                        bool is_weapon = !slot.isEmpty() &&
                            slot.item->category == game::inventory::ItemCategory::Weapon;
                        bool is_star = !slot.isEmpty() &&
                            slot.item->category == game::inventory::ItemCategory::StarSkill;

                        if (is_weapon)
                            ImGui::PushStyleColor(ImGuiCol_Button, {0.30f, 0.18f, 0.10f, 1.0f});
                        else if (is_star)
                            ImGui::PushStyleColor(ImGuiCol_Button, {0.12f, 0.22f, 0.38f, 1.0f});
                        else
                            ImGui::PushStyleColor(ImGuiCol_Button, {0.18f, 0.18f, 0.28f, 1.0f});
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.38f, 0.35f, 0.55f, 1.0f});

                        char label[48];
                        if (slot.isEmpty())
                            snprintf(label, sizeof(label), "##s%d", idx);
                        else if (is_weapon)
                        {
                            const auto *def = game::weapon::getWeaponDef(slot.item->id);
                            snprintf(label, sizeof(label), "%s##s%d",
                                def ? def->icon_label.c_str() : "[W]", idx);
                        }
                        else
                            snprintf(label, sizeof(label), "##s%d", idx);

                        ImGui::Button(label, {SLOT, SLOT});

                        // 物品图标（在按钮区域绘制）
                        if (!slot.isEmpty() && !is_weapon)
                        {
                            auto* idl = ImGui::GetWindowDrawList();
                            ImVec2 imin = ImGui::GetItemRectMin();
                            ImVec2 imax = ImGui::GetItemRectMax();
                            float  icx  = (imin.x + imax.x) * 0.5f;
                            float  icy  = (imin.y + imax.y) * 0.5f;
                            if (is_star)
                            {
                                drawStarGemSphereIcon(idl, {icx, icy}, SLOT * 0.38f,
                                                      slot.item->id, 1.0f);
                            }
                            else
                            {
                                drawItemIcon(idl, imin, imax,
                                             slot.item->category, slot.count);
                            }
                        }

                        // 右键星技珠 → 放入第一个空星技槽
                        if (is_star && ImGui::IsItemClicked(ImGuiMouseButton_Right))
                        {
                            for (auto& sk : m_starSockets)
                            {
                                if (sk.isEmpty())
                                {
                                    sk.item  = slot.item;
                                    sk.count = 1;
                                    slot.item.reset();
                                    slot.count = 0;
                                    break;
                                }
                            }
                        }

                        // 拖放源：格子不空即可拖
                        if (!slot.isEmpty() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                        {
                            ImGui::SetDragDropPayload("INV_SLOT", &idx, sizeof(int));
                            ImGui::TextUnformatted(slot.item->name.c_str());
                            if (is_weapon)
                                ImGui::TextDisabled("%s", locale::T("weapon_bar.drag_hint").c_str());
                            if (is_star)
                                ImGui::TextDisabled("右键快速装备 | 拖入技能格子");
                            ImGui::EndDragDropSource();
                        }

                        // 拖放目标：接受来自星技槽的 STAR_SLOT 拖回背包
                        if (slot.isEmpty() && ImGui::BeginDragDropTarget())
                        {
                            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("STAR_SLOT"))
                            {
                                int srcSocketIdx = *static_cast<const int*>(p->Data);
                                auto& srcSocket = m_starSockets[srcSocketIdx];
                                if (!srcSocket.isEmpty())
                                {
                                    slot.item  = srcSocket.item;
                                    slot.count = srcSocket.count;
                                    srcSocket.item.reset();
                                    srcSocket.count = 0;
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        // Tooltip
                        if (!slot.isEmpty() && ImGui::IsItemHovered())
                        {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted(slot.item->name.c_str());
                            if (is_weapon)
                            {
                                const auto *def = game::weapon::getWeaponDef(slot.item->id);
                                if (def)
                                {
                                    ImGui::Separator();
                                    ImGui::TextDisabled("%s: %d", locale::T("weapon.damage").c_str(), def->damage);
                                    ImGui::TextDisabled("%s: %.1f/s", locale::T("weapon.speed").c_str(), def->attack_speed);
                                }
                                ImGui::TextDisabled("%s", locale::T("weapon_bar.drag_hint").c_str());
                            }
                            else if (is_star)
                            {
                                ImGui::TextDisabled("星技珠子");
                                ImGui::Separator();
                                ImGui::TextDisabled("右键 → 装入技能格子");
                                ImGui::TextDisabled("拖拽 → 放到技能格子指定槽");
                            }
                            else
                                ImGui::TextDisabled("%s: %d / %d",
                                    locale::T("inventory.quantity").c_str(), slot.count, slot.item->max_stack);
                            ImGui::EndTooltip();
                        }

                        ImGui::PopStyleColor(2);
                    }
                }

                ImGui::EndChild(); // ##inv_grid

                // ── 右侧：技能格子 ────────────────────────────────────
                ImGui::SameLine(0.0f, PANEL_GAP);
                ImGui::BeginChild("##skill_panel", {SKILL_W, 0.0f}, false,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

                // 面板标题
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.90f, 1.0f, 1.0f));
                ImGui::TextUnformatted("★ 技能格子");
                ImGui::PopStyleColor();
                ImGui::Separator();
                ImGui::Spacing();

                // 2×3 技能槽网格
                auto* skillDrawList = ImGui::GetWindowDrawList();
                for (int sk = 0; sk < SK_COUNT; ++sk)
                {
                    if (sk % SK_COLS != 0) ImGui::SameLine();

                    auto& skSlot  = m_starSockets[sk];
                    bool  occupied = !skSlot.isEmpty();

                    ImGui::PushStyleColor(ImGuiCol_Button,
                        occupied ? ImVec4(0.15f, 0.30f, 0.55f, 1.0f)
                                 : ImVec4(0.10f, 0.12f, 0.22f, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.28f, 0.48f, 0.75f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(0.20f, 0.38f, 0.65f, 1.0f));

                    char skId[16];
                    snprintf(skId, sizeof(skId), "##sk%d", sk);
                    ImGui::Button(skId, {SLOT, SLOT});

                    // 在槽中央绘制球形图标或序号
                    {
                        ImVec2 bmin = ImGui::GetItemRectMin();
                        ImVec2 bmax = ImGui::GetItemRectMax();
                        float  bcx  = (bmin.x + bmax.x) * 0.5f;
                        float  bcy  = (bmin.y + bmax.y) * 0.5f;
                        if (occupied)
                        {
                            drawStarGemSphereIcon(skillDrawList, {bcx, bcy},
                                                  SLOT * 0.38f, skSlot.item->id, 1.0f);
                        }
                        else
                        {
                            // 空槽：序号
                            char numBuf[4];
                            snprintf(numBuf, sizeof(numBuf), "%d", sk + 1);
                            ImVec2 ns = ImGui::CalcTextSize(numBuf);
                            skillDrawList->AddText(
                                {bcx - ns.x * 0.5f, bcy - ns.y * 0.5f},
                                IM_COL32(80, 100, 150, 140), numBuf);
                        }
                    }

                    // 右键取下，归还背包
                    if (occupied && ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    {
                        m_inventory.addItem(*skSlot.item, skSlot.count);
                        skSlot.item.reset();
                        skSlot.count = 0;
                    }

                    // 拖放源：已装备的珠子可拖出
                    if (occupied && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        ImGui::SetDragDropPayload("STAR_SLOT", &sk, sizeof(int));
                        ImGui::TextUnformatted(skSlot.item->name.c_str());
                        ImGui::TextDisabled("右键卸下 | 拖至背包取走 | 拖至其他槽换位");
                        ImGui::EndDragDropSource();
                    }

                    // 拖放目标：接受来自背包的 INV_SLOT（仅 StarSkill）
                    //           或来自其他技能槽的 STAR_SLOT（交换位置）
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("INV_SLOT"))
                        {
                            int srcIdx = *static_cast<const int*>(p->Data);
                            auto& src  = m_inventory.getSlot(srcIdx);
                            if (!src.isEmpty() &&
                                src.item->category == game::inventory::ItemCategory::StarSkill)
                            {
                                if (occupied)
                                    m_inventory.addItem(*skSlot.item, skSlot.count);
                                skSlot.item  = src.item;
                                skSlot.count = 1;
                                src.item.reset();
                                src.count = 0;
                            }
                        }
                        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("STAR_SLOT"))
                        {
                            int srcSocket = *static_cast<const int*>(p->Data);
                            if (srcSocket != sk)
                                std::swap(m_starSockets[srcSocket], m_starSockets[sk]);
                        }
                        ImGui::EndDragDropTarget();
                    }

                    // Tooltip
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::BeginTooltip();
                        if (occupied)
                        {
                            ImGui::TextUnformatted(skSlot.item->name.c_str());
                            ImGui::Separator();
                            ImGui::TextDisabled("右键 → 取下归还背包");
                            ImGui::TextDisabled("拖拽 → 移至其他槽位");
                        }
                        else
                        {
                            ImGui::TextDisabled("空技能槽 #%d", sk + 1);
                            ImGui::Separator();
                            ImGui::TextDisabled("从背包右键星技珠装备");
                            ImGui::TextDisabled("或拖拽星技珠到此处");
                        }
                        ImGui::EndTooltip();
                    }

                    ImGui::PopStyleColor(3);
                }

                ImGui::EndChild(); // ##skill_panel

                ImGui::EndTabItem();
            }

            // ── 星技标签（圆形环视图） ─────────────────────────────────
            if (ImGui::BeginTabItem("星技"))
            {
                renderStarSocketPage();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void GameScene::createPlayer()
    {
        m_player = actor_manager->createActor("player");

        // 玩家初始位置（从天空下落）
        glm::vec2 startPos = {0.0f, 0.0f};

        auto* transform = m_player->addComponent<engine::component::TransformComponent>(startPos);
        m_player->addComponent<engine::component::SpriteComponent>(
            "assets/textures/Characters/player_sheet.svg",
            engine::utils::Alignment::CENTER,
            engine::utils::FRect{{0.0f, 0.0f}, {32.0f, 32.0f}});
        m_player->addComponent<engine::component::ControllerComponent>(25.0f, 30.0f);

        // 动画管理组件：新精灵表 256×256，8×8 网格，每帧 32×32 像素
        // 行布局：0=待机  1=跑动  2=喷气背包  3=(备用)  4-5=(备用)  6=跳跃  7=下落
        auto* anim = m_player->addComponent<engine::component::AnimationComponent>(32.0f, 32.0f);
        anim->addClip("idle",    engine::component::AnimationClip{0, 0, 8, 0.30f, true});
        anim->addClip("run",     engine::component::AnimationClip{1, 0, 8, 0.09f, true});
        anim->addClip("jetpack", engine::component::AnimationClip{2, 0, 8, 0.10f, true});
        anim->addClip("jump",    engine::component::AnimationClip{6, 0, 4, 0.12f, false});
        anim->addClip("fall",    engine::component::AnimationClip{7, 0, 4, 0.12f, false});
        anim->play("idle");

        // 创建物理体并添加 PhysicsComponent（32×32 像素 ≈ 1×1 米）
        b2BodyId bodyId = physics_manager->createDynamicBody({startPos.x, startPos.y}, {0.5f, 0.5f}, m_player);
        m_player->addComponent<engine::component::PhysicsComponent>(bodyId, physics_manager.get());

        // 属性组件（血量、星能、速度倍率等）
        m_player->addComponent<game::component::AttributeComponent>();

        // 相机跟随玩家
        _context.getCamera().setFollowTarget(&transform->getPosition(), 5.0f);
    }

    void GameScene::executeCommand()
    {
        std::string command = m_commandBuffer.data();
        command.erase(std::remove_if(command.begin(), command.end(), [](unsigned char c) {
            return std::isspace(c) != 0;
        }), command.end());

        if (command == "001")
        {
            spawnMechDrop();
            m_showCommandInput = false;
            m_commandBuffer[0] = '\0';
            return;
        }

        spdlog::warn("未知指令: {}", command.empty() ? "<empty>" : command);
    }

    void GameScene::spawnMechDrop()
    {
        if (m_isPlayerInMech)
        {
            spdlog::info("机甲已连接驾驶员，忽略重复空投指令");
            return;
        }

        constexpr float kPixelsPerMeter = 32.0f;
        glm::vec2 playerPos = getActorWorldPosition(m_player);
        glm::vec2 dropTarget = {playerPos.x + 180.0f, playerPos.y - 32.0f};
        glm::vec2 spawnPos = {dropTarget.x, dropTarget.y - 640.0f};

        if (!m_mech)
        {
            m_mech = actor_manager->createActor("mech_drop");
            auto* mechTransform = m_mech->addComponent<engine::component::TransformComponent>(spawnPos, glm::vec2{0.28f, 0.28f});
            mechTransform->setScale({0.28f, 0.28f});
            // 初始帧：第1行（待机/瞄准），第0帧
            m_mech->addComponent<engine::component::SpriteComponent>(
                "assets/textures/Characters/mech_sheet.png",
                engine::utils::Alignment::CENTER,
                engine::utils::FRect{{0.0f, 256.0f}, {172.0f, 256.0f}});
            m_mechAnimRow = 1; m_mechAnimFrame = 0; m_mechAnimTimer = 0.0f; m_mechShootTimer = 0.0f;
            auto* mechController = m_mech->addComponent<engine::component::ControllerComponent>(18.0f, 8.0f);
            mechController->setGroundAcceleration(58.0f);
            mechController->setAirAcceleration(28.0f);
            mechController->setJumpSpeed(10.5f);
            mechController->setJumpCutFactor(0.78f);
            mechController->setCoyoteTime(0.16f);
            mechController->setGroundedThreshold(0.18f);
            mechController->setJetpackEnabled(false);
            mechController->setJetpackProfile(0.0f, 0.0f, 0.0f, 0.0f);
            mechController->setEnabled(false);
            b2BodyId bodyId = physics_manager->createDynamicBody(
                {spawnPos.x / kPixelsPerMeter, spawnPos.y / kPixelsPerMeter},
                {0.75f, 1.1f},
                m_mech);
            m_mech->addComponent<engine::component::PhysicsComponent>(bodyId, physics_manager.get());
        }
        else
        {
            if (auto* transform = m_mech->getComponent<engine::component::TransformComponent>())
                transform->setPosition(spawnPos);
            if (auto* sprite = m_mech->getComponent<engine::component::SpriteComponent>())
                sprite->setHidden(false);
            if (auto* physics = m_mech->getComponent<engine::component::PhysicsComponent>())
            {
                physics->setWorldPosition(spawnPos);
                physics->setVelocity({0.0f, 4.0f});
            }
            if (auto* controller = m_mech->getComponent<engine::component::ControllerComponent>())
            {
                controller->setGroundAcceleration(58.0f);
                controller->setAirAcceleration(28.0f);
                controller->setJumpSpeed(10.5f);
                controller->setJumpCutFactor(0.78f);
                controller->setCoyoteTime(0.16f);
                controller->setGroundedThreshold(0.18f);
                controller->setJetpackEnabled(false);
                controller->setEnabled(false);
            }
        }

        m_mechAttackCooldown = 0.0f;
        m_mechLastAttackHits = 0;

        spdlog::info("指令 001 已执行：机甲开始空投，目标坐标 ({:.1f}, {:.1f})", dropTarget.x, dropTarget.y);
    }

    void GameScene::tryEnterMech()
    {
        if (!m_mech || m_isPlayerInMech || !m_player)
            return;

        glm::vec2 playerPos = getActorWorldPosition(m_player);
        glm::vec2 mechPos = getActorWorldPosition(m_mech);
        if (glm::distance(playerPos, mechPos) > 220.0f)
            return;

        auto* mechController = m_mech->getComponent<engine::component::ControllerComponent>();
        auto* mechTransform = m_mech->getComponent<engine::component::TransformComponent>();
        auto* playerController = m_player->getComponent<engine::component::ControllerComponent>();
        auto* playerPhysics = m_player->getComponent<engine::component::PhysicsComponent>();
        auto* playerTransform = m_player->getComponent<engine::component::TransformComponent>();
        auto* playerSprite = m_player->getComponent<engine::component::SpriteComponent>();
        if (!mechController || !mechTransform || !playerController || !playerPhysics || !playerTransform || !playerSprite)
            return;

        playerController->setEnabled(false);
        playerPhysics->setVelocity({0.0f, 0.0f});
        playerPhysics->setWorldPosition({-4096.0f, -4096.0f});
        playerTransform->setPosition({-4096.0f, -4096.0f});
        playerSprite->setHidden(true);

        mechController->setEnabled(true);
        _context.getCamera().setFollowTarget(&mechTransform->getPosition(), 4.2f);
        m_isPlayerInMech = true;
        spdlog::info("驾驶员已进入机甲");
    }

    void GameScene::exitMech()
    {
        if (!m_isPlayerInMech || !m_mech || !m_player)
            return;

        auto* mechController = m_mech->getComponent<engine::component::ControllerComponent>();
        auto* playerController = m_player->getComponent<engine::component::ControllerComponent>();
        auto* playerPhysics = m_player->getComponent<engine::component::PhysicsComponent>();
        auto* playerTransform = m_player->getComponent<engine::component::TransformComponent>();
        auto* playerSprite = m_player->getComponent<engine::component::SpriteComponent>();
        if (!mechController || !playerController || !playerPhysics || !playerTransform || !playerSprite)
            return;

        glm::vec2 exitPos = findSafeDisembarkPosition();
        mechController->setEnabled(false);
        playerSprite->setHidden(false);
        playerTransform->setPosition(exitPos);
        playerPhysics->setWorldPosition(exitPos);
        playerPhysics->setVelocity({0.0f, 0.0f});
        playerController->setEnabled(true);
        _context.getCamera().setFollowTarget(&playerTransform->getPosition(), 5.0f);
        m_isPlayerInMech = false;
        spdlog::info("驾驶员已离开机甲");
    }

    void GameScene::performMeleeAttack(glm::vec2 targetPos)
    {
        if (!m_player || m_weaponAttackCooldown > 0.0f)
            return;

        const auto &activeSlot = m_weaponBar.getActiveSlot();
        if (activeSlot.isEmpty() || !activeSlot.item)
            return;

        const auto *weaponDef = game::weapon::getWeaponDef(activeSlot.item->id);
        if (!weaponDef || weaponDef->attack_type != game::weapon::AttackType::Melee)
            return;

        auto *ctrl = m_player->getComponent<engine::component::ControllerComponent>();
        if (!ctrl)
            return;

        float facing = ctrl->getFacingDirection() == engine::component::ControllerComponent::FacingDirection::Left
            ? -1.0f : 1.0f;
        glm::vec2 playerPos = getActorWorldPosition(m_player);
        glm::vec2 slashCenter = playerPos + glm::vec2(44.0f * facing, -8.0f);

        using engine::world::TileType;
        bool changedTiles = false;
        int destroyedTiles = 0;
        const glm::ivec2 tileMin = chunk_manager->worldToTile(slashCenter - glm::vec2{weaponDef->range, 56.0f});
        const glm::ivec2 tileMax = chunk_manager->worldToTile(slashCenter + glm::vec2{weaponDef->range, 56.0f});
        for (int ty = tileMin.y; ty <= tileMax.y; ++ty)
        {
            for (int tx = tileMin.x; tx <= tileMax.x; ++tx)
            {
                glm::vec2 tileCenter = chunk_manager->tileToWorld({tx, ty}) + glm::vec2{8.0f, 8.0f};
                glm::vec2 delta = tileCenter - playerPos;
                if (glm::length(delta) > weaponDef->range)
                    continue;
                if (delta.x * facing < -8.0f || std::abs(delta.y) > 54.0f)
                    continue;

                TileType type = chunk_manager->tileAt(tx, ty).type;
                if (type == TileType::Air)
                    continue;

                if (type == TileType::Wood || type == TileType::Leaves)
                {
                    m_treeManager.digTile(tx, ty, *chunk_manager, m_treeManager.getDrops(), false);
                    changedTiles = true;
                    ++destroyedTiles;
                }
                else if (type == TileType::Ore)
                {
                    chunk_manager->setTileSilent(tx, ty, engine::world::TileData(TileType::Air));
                    changedTiles = true;
                    ++destroyedTiles;
                    using Cat = game::inventory::ItemCategory;
                    m_inventory.addItem({"ore", "矿石", 64, Cat::Material}, 1);
                }
            }
        }
        if (changedTiles)
            chunk_manager->rebuildDirtyChunks();

        std::vector<glm::vec2> defeatPositions;
        int slain = m_monsterManager
            ? m_monsterManager->slashMonsters(playerPos, facing, weaponDef->range + 20.0f, 72.0f, &defeatPositions)
            : 0;

        m_slashVfxList.push_back({slashCenter, facing, 0.0f, 0.20f, weaponDef->range});
        for (const glm::vec2 &pos : defeatPositions)
        {
            for (int i = 0; i < 14; ++i)
            {
                float spread = (-0.9f + 1.8f * (static_cast<float>(i) / 13.0f));
                glm::vec2 dir = glm::normalize(glm::vec2(facing * (1.5f + std::abs(spread) * 1.8f), spread * 1.2f - 0.35f));
                float speed = 240.0f + 26.0f * static_cast<float>(i);
                m_combatFragments.push_back({
                    pos,
                    dir * speed,
                    0.0f,
                    0.55f + 0.02f * static_cast<float>(i % 4),
                    4.0f + static_cast<float>(i % 3)
                });
            }
        }

        triggerAttackStarSkills(targetPos);
        m_weaponAttackCooldown = std::max(1.0f / weaponDef->attack_speed, 0.08f);
        spdlog::debug("合金巨剑：劈砍 @({:.0f},{:.0f})  斩杀={} 破坏瓦片={}",
                      slashCenter.x, slashCenter.y, slain, destroyedTiles);
    }

    void GameScene::performMechAttack()
    {
        if (!m_isPlayerInMech || !m_mech || m_mechAttackCooldown > 0.0f)
            return;

        auto* mechController = m_mech->getComponent<engine::component::ControllerComponent>();
        auto* mechTransform = m_mech->getComponent<engine::component::TransformComponent>();
        auto* mechPhysics = m_mech->getComponent<engine::component::PhysicsComponent>();
        if (!mechController || !mechTransform || !mechPhysics)
            return;

        const float facing = mechController->getFacingDirection() == engine::component::ControllerComponent::FacingDirection::Left ? -1.0f : 1.0f;
        glm::vec2 center = mechTransform->getPosition() + glm::vec2{facing * 128.0f, 28.0f};
        constexpr float radius = 92.0f;
        int destroyedTiles = 0;

        using engine::world::TileData;
        using engine::world::TileType;
        const glm::ivec2 tileMin = chunk_manager->worldToTile(center - glm::vec2{radius, radius});
        const glm::ivec2 tileMax = chunk_manager->worldToTile(center + glm::vec2{radius, radius});
        std::unordered_set<long long> processedTreeTiles;
        bool hasBatchedTileChanges = false;

        for (int ty = tileMin.y; ty <= tileMax.y; ++ty)
        {
            for (int tx = tileMin.x; tx <= tileMax.x; ++tx)
            {
                glm::vec2 tileCenter = chunk_manager->tileToWorld({tx, ty}) + glm::vec2{8.0f, 8.0f};
                if (glm::distance(tileCenter, center) > radius)
                    continue;

                TileType type = chunk_manager->tileAt(tx, ty).type;
                if (type == TileType::Air)
                    continue;

                if (type == TileType::Wood || type == TileType::Leaves)
                {
                    long long key = (static_cast<long long>(tx) << 32) ^ static_cast<unsigned int>(ty);
                    if (!processedTreeTiles.insert(key).second)
                        continue;
                    m_treeManager.digTile(tx, ty, *chunk_manager, m_treeManager.getDrops(), false);
                    hasBatchedTileChanges = true;
                    ++destroyedTiles;
                    continue;
                }

                if (type == TileType::Ore)
                {
                    using Cat = game::inventory::ItemCategory;
                    m_inventory.addItem({"ore", "矿石", 64, Cat::Material}, 1);
                }
                else if (type == TileType::Stone || type == TileType::Gravel)
                {
                    using Cat = game::inventory::ItemCategory;
                    m_inventory.addItem({"stone", "石块", 64, Cat::Material}, 1);
                }

                chunk_manager->setTileSilent(tx, ty, TileData(TileType::Air));
                hasBatchedTileChanges = true;
                ++destroyedTiles;
            }
        }

        if (hasBatchedTileChanges)
            chunk_manager->rebuildDirtyChunks();

        int crushedMonsters = m_monsterManager ? m_monsterManager->crushMonstersInRadius(center, radius + 32.0f) : 0;
        glm::vec2 vel = mechPhysics->getVelocity();
        vel.x += facing * 1.5f;
        mechPhysics->setVelocity(vel);

        m_mechShootTimer    = 0.56f;   // 触发射击行动画（7帧 × 0.07s ≈ 0.49s）
        m_mechAttackCooldown = 0.65f;
        m_mechAttackFlashTimer = 0.16f;
        m_mechLastAttackHits = destroyedTiles + crushedMonsters;
        spdlog::info("机甲重击：摧毁 {} 个瓦片，击退 {} 个怪物", destroyedTiles, crushedMonsters);
    }

    // ──────────────────────────────────────────────────────────────────────────
    //  星技被动 Tick（每帧调用）
    //    - 冷却倒计时
    //    - 疾风：装备状态变化时调整移速/喷气包
    //    - 寒冰：光环自动消灭近身怪物
    // ──────────────────────────────────────────────────────────────────────────
    void GameScene::tickStarSkillPassives(float dt)
    {
        if (!m_player) return;

        // 冷却倒计时
        for (auto& cd : m_skillCooldowns)
            cd = std::max(0.0f, cd - dt);

        bool windEquipped = false;

        for (int i = 0; i < 6; ++i)
        {
            if (m_starSockets[i].isEmpty()) continue;
            const auto* def = game::skill::getStarSkillDef(m_starSockets[i].item->id);
            if (!def) continue;

            switch (def->effect)
            {
            // ── 疾风：被动速度/喷气检测 ──────────────────────────────────
            case game::skill::SkillEffect::WindBoost:
                windEquipped = true;
                // 每帧消耗星能维持疾风效果
                if (auto* attr = m_player->getComponent<game::component::AttributeComponent>())
                    attr->consumeStarEnergy(2.0f * dt);  // 2 SE/s
                break;

            // ── 寒冰：被动光环，冷却到期时消灭附近怪物 ──────────────────
            case game::skill::SkillEffect::IceAura:
                if (m_skillCooldowns[i] <= 0.0f && m_monsterManager)
                {
                    auto* attr = m_player->getComponent<game::component::AttributeComponent>();
                    bool hasEnergy = !attr || attr->consumeStarEnergy(8.0f);
                    if (hasEnergy)
                    {
                        glm::vec2 ppos = getActorWorldPosition(m_player);
                        int cnt = m_monsterManager->crushMonstersInRadius(ppos, def->range);
                        if (cnt > 0)
                        {
                            m_skillCooldowns[i] = def->cooldown;
                            m_skillVfxList.push_back({game::skill::SkillEffect::IceAura, ppos, 0.0f, 0.75f, 0.0f});
                            spdlog::debug("寒冰光环：冻结 {} 个怪物", cnt);
                        }
                        else if (attr)
                            attr->restoreStarEnergy(8.0f);  // 没有怪物则退还费用
                    }
                }
                break;

            default:
                break;
            }
        }

        // 疾风状态变更：通过属性修改器改变速度，保持 syncPlayerPresentation 同步
        if (windEquipped != m_windStarEquipped)
        {
            m_windStarEquipped = windEquipped;
            auto* attr = m_player->getComponent<game::component::AttributeComponent>();
            auto* ctrl = m_player->getComponent<engine::component::ControllerComponent>();
            if (windEquipped)
            {
                if (attr)
                {
                    attr->addModifier({"star_wind", game::component::StatType::Speed,     0.0f, 0.35f, -1.0f});
                    attr->addModifier({"star_wind", game::component::StatType::JumpPower, 0.0f, 0.15f, -1.0f});
                }
                if (ctrl) ctrl->setJetpackProfile(1.5f, 20.0f, 8.0f, 25.0f);
                spdlog::debug("疾风星技：速度增强");
            }
            else
            {
                if (attr) attr->removeAllModifiers("star_wind");
                if (ctrl) ctrl->setJetpackProfile(0.75f, 20.0f, 5.5f, 20.0f);
                spdlog::debug("疾风星技：速度恢复默认");
            }
        }
    }

    // ──────────────────────────────────────────────────────────────────────────
    //  攻击触发型星技（左键/K攻击时调用）
    //    - 炎焰：从人物手中发射火球，碰撞或抵达目标后爆炸
    // ──────────────────────────────────────────────────────────────────────────
    void GameScene::triggerAttackStarSkills(glm::vec2 attackPos)
    {
        m_lastAttackSkillTarget = attackPos;
        m_hasLastAttackSkillTarget = true;

        for (int i = 0; i < 6; ++i)
        {
            if (m_starSockets[i].isEmpty()) continue;
            if (m_skillCooldowns[i] > 0.0f)  continue;

            const auto* def = game::skill::getStarSkillDef(m_starSockets[i].item->id);
            if (!def || def->effect != game::skill::SkillEffect::FireBlast) continue;

            // 消耗星能（炎焰：15 SE/次）
            if (auto* attr = m_player->getComponent<game::component::AttributeComponent>())
            {
                if (!attr->consumeStarEnergy(15.0f))
                    continue;
            }

            glm::vec2 origin = getPlayerCastOrigin(attackPos);
            glm::vec2 delta = attackPos - origin;
            float distance = glm::length(delta);
            if (distance < 1.0f)
            {
                delta = {1.0f, 0.0f};
                distance = 1.0f;
            }
            else
            {
                delta /= distance;
            }

            constexpr float kFireProjectileSpeed = 520.0f;
            float flightTime = std::max(distance / kFireProjectileSpeed, 0.12f);
            m_skillProjectiles.push_back({
                game::skill::SkillEffect::FireBlast,
                origin,
                origin,
                origin,
                attackPos,
                delta * kFireProjectileSpeed,
                0.0f,
                std::min(flightTime + 0.18f, 1.4f),
                def->range
            });
            m_skillVfxList.push_back({game::skill::SkillEffect::FireBlast, origin, 0.0f, 0.18f, -1.0f});

            m_skillCooldowns[i] = def->cooldown;
            spdlog::debug("炎焰星技：投射物发射 ({:.0f},{:.0f}) -> ({:.0f},{:.0f})",
                          origin.x, origin.y, attackPos.x, attackPos.y);
        }
    }

    void GameScene::explodeFireBlast(glm::vec2 attackPos, float radius)
    {
        using engine::world::TileData;
        using engine::world::TileType;

        const glm::ivec2 tileMin = chunk_manager->worldToTile(attackPos - glm::vec2{radius, radius});
        const glm::ivec2 tileMax = chunk_manager->worldToTile(attackPos + glm::vec2{radius, radius});

        int destroyedTiles = 0;
        bool hasBatchedTileChanges = false;
        for (int ty = tileMin.y; ty <= tileMax.y; ++ty)
        {
            for (int tx = tileMin.x; tx <= tileMax.x; ++tx)
            {
                glm::vec2 tileCenter = chunk_manager->tileToWorld({tx, ty}) + glm::vec2{8.0f, 8.0f};
                if (glm::distance(tileCenter, attackPos) > radius) continue;

                TileType t = chunk_manager->tileAt(tx, ty).type;
                if (t == TileType::Air) continue;

                if (t == TileType::Wood || t == TileType::Leaves)
                {
                    m_treeManager.digTile(tx, ty, *chunk_manager, m_treeManager.getDrops(), false);
                    hasBatchedTileChanges = true;
                }
                else if (t == TileType::Ore)
                {
                    using Cat = game::inventory::ItemCategory;
                    m_inventory.addItem({"ore", "矿石", 64, Cat::Material}, 1);
                    chunk_manager->setTileSilent(tx, ty, TileData(TileType::Air));
                    hasBatchedTileChanges = true;
                }
                else
                {
                    chunk_manager->setTileSilent(tx, ty, TileData(TileType::Air));
                    hasBatchedTileChanges = true;
                }
                ++destroyedTiles;
            }
        }

        if (hasBatchedTileChanges)
            chunk_manager->rebuildDirtyChunks();

        int crushed = m_monsterManager
            ? m_monsterManager->crushMonstersInRadius(attackPos, radius + 18.0f)
            : 0;

        m_lastAttackSkillTarget = attackPos;
        m_hasLastAttackSkillTarget = true;
        m_skillVfxList.push_back({game::skill::SkillEffect::FireBlast, attackPos, 0.0f, 0.72f, radius});
        spdlog::debug("炎焰星技：爆炸 @({:.0f},{:.0f})  瓦片={} 怪物={}",
                      attackPos.x, attackPos.y, destroyedTiles, crushed);
    }

    // ──────────────────────────────────────────────────────────────────────────
    //  主动技能（Q键触发）
    //    - 闪光：向面朝方向瞬间赋予高速，形成冲刺效果
    // ──────────────────────────────────────────────────────────────────────────
    void GameScene::triggerActiveStarSkills()
    {
        if (!m_player) return;

        for (int i = 0; i < 6; ++i)
        {
            if (m_starSockets[i].isEmpty()) continue;
            if (m_skillCooldowns[i] > 0.0f)  continue;

            const auto* def = game::skill::getStarSkillDef(m_starSockets[i].item->id);
            if (!def || def->effect != game::skill::SkillEffect::LightDash) continue;

            auto* ctrl = m_player->getComponent<engine::component::ControllerComponent>();
            auto* phys = m_player->getComponent<engine::component::PhysicsComponent>();
            if (!ctrl || !phys) continue;

            // 消耗星能（闪光：20 SE/次）
            auto* attr = m_player->getComponent<game::component::AttributeComponent>();
            if (attr && !attr->consumeStarEnergy(20.0f)) continue;  // 星能不足则跳过

            float facing = (ctrl->getFacingDirection() ==
                engine::component::ControllerComponent::FacingDirection::Left) ? -1.0f : 1.0f;

            glm::vec2 vel = phys->getVelocity();
            vel.x = facing * def->param;      // 直接设速，非叠加
            vel.y = std::min(vel.y, -2.0f);   // 轻微上跃感
            phys->setVelocity(vel);

            m_skillCooldowns[i] = def->cooldown;
            {
                glm::vec2 ppos = getActorWorldPosition(m_player);
                m_skillVfxList.push_back({game::skill::SkillEffect::LightDash, ppos, 0.0f, 0.45f, facing});
            }
            spdlog::debug("闪光星技：冲刺 {} 方向", facing > 0 ? "右" : "左");
            break;  // 每次Q键只触发一个冲刺星
        }
    }

    // ──────────────────────────────────────────────────────────────────────────
    //  技能 HUD：屏幕底部居中，显示各星技槽图标 + 冷却进度条
    // ──────────────────────────────────────────────────────────────────────────
    void GameScene::renderSkillHUD()
    {
        // 收集已装备的槽
        struct SlotInfo { int socketIdx; const game::skill::StarSkillDef* def; };
        std::array<SlotInfo, 6> slots{};
        int count = 0;
        for (int i = 0; i < 6; ++i)
        {
            if (m_starSockets[i].isEmpty()) continue;
            const auto* def = game::skill::getStarSkillDef(m_starSockets[i].item->id);
            if (def) slots[count++] = {i, def};
        }
        if (count == 0) return;

        constexpr float ICON_SIZE = 44.0f;
        constexpr float PAD       = 6.0f;
        const float winW = count * (ICON_SIZE + PAD) - PAD + 16.0f;
        const float winH = ICON_SIZE + 28.0f;  // 图标 + 底部文字

        ImVec2 disp = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos({ (disp.x - winW) * 0.5f, disp.y - winH - 58.0f }, ImGuiCond_Always);
        ImGui::SetNextWindowSize({ winW, winH }, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.55f);

        ImGui::Begin("##skill_hud", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoInputs     | ImGuiWindowFlags_NoNav  |
            ImGuiWindowFlags_NoSavedSettings);

        auto* drawList = ImGui::GetWindowDrawList();

        for (int k = 0; k < count; ++k)
        {
            if (k > 0) ImGui::SameLine(0.0f, PAD);
            const auto& si  = slots[k];
            float cd         = m_skillCooldowns[si.socketIdx];
            float maxCd      = si.def->cooldown > 0.0f ? si.def->cooldown : 1.0f;
            float ratio      = (maxCd > 0.0f) ? std::min(cd / maxCd, 1.0f) : 0.0f;
            bool  onCooldown = cd > 0.01f;

            // 底色
            ImVec4 bgCol = onCooldown
                ? ImVec4(0.08f, 0.08f, 0.12f, 1.0f)
                : ImVec4(0.05f, 0.10f, 0.20f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button,        bgCol);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.10f, 0.20f, 0.35f, 1.0f));

            // 确定触发键提示
            const char* keyHint = "";
            switch (si.def->effect)
            {
            case game::skill::SkillEffect::FireBlast:  keyHint = "[攻击]"; break;
            case game::skill::SkillEffect::IceAura:    keyHint = "[被动]"; break;
            case game::skill::SkillEffect::WindBoost:  keyHint = "[被动]"; break;
            case game::skill::SkillEffect::LightDash:  keyHint = "[Q]";    break;
            }

            char btnId[32];
            snprintf(btnId, sizeof(btnId), "##hsk%d", k);
            ImGui::Button(btnId, { ICON_SIZE, ICON_SIZE });

            ImVec2 bmin = ImGui::GetItemRectMin();
            ImVec2 bmax = ImGui::GetItemRectMax();
            float  bcx  = (bmin.x + bmax.x) * 0.5f;
            float  bcy  = (bmin.y + bmax.y) * 0.5f;
            float  r    = ICON_SIZE * 0.41f;

            // 球形图标（冷却时变暗）
            float iconAlpha = onCooldown ? 0.45f : 1.0f;
            drawStarGemSphereIcon(drawList, {bcx, bcy}, r,
                                  m_starSockets[si.socketIdx].item->id, iconAlpha);

            // 冷却遮罩（从底部向上的半透明覆盖）
            if (onCooldown && ratio > 0.0f)
            {
                float maskTop = bmin.y + (bmax.y - bmin.y) * (1.0f - ratio);
                drawList->AddRectFilled(
                    { bmin.x, maskTop }, bmax,
                    IM_COL32(0, 0, 0, 110));

                // 冷却数字
                char cdBuf[8];
                snprintf(cdBuf, sizeof(cdBuf), "%.1f", cd);
                ImVec2 ts = ImGui::CalcTextSize(cdBuf);
                drawList->AddText(
                    { bcx - ts.x * 0.5f, bcy - ts.y * 0.5f },
                    IM_COL32(255, 220, 100, 230), cdBuf);
            }

            ImGui::PopStyleColor(2);

            // 槽编号 + 触发键提示（图标下方）
            {
                const char* name = m_starSockets[si.socketIdx].item->name.c_str();
                ImVec2 ns = ImGui::CalcTextSize(name);
                // 物品名太长则只显示 keyHint
                if (ns.x <= ICON_SIZE)
                    drawList->AddText({ bcx - ns.x * 0.5f, bmax.y + 2.0f },
                        IM_COL32(180, 210, 255, 200), name);
                else
                {
                    ImVec2 ks = ImGui::CalcTextSize(keyHint);
                    drawList->AddText({ bcx - ks.x * 0.5f, bmax.y + 2.0f },
                        IM_COL32(140, 180, 230, 180), keyHint);
                }
            }
        }

        ImGui::End();
    }

    // ──────────────────────────────────────────────────────────────────────────
    //  角色状态 HUD：血量位于屏幕正下方，属性位于右下角纵向排列
    // ──────────────────────────────────────────────────────────────────────────
    void GameScene::renderPlayerStatusHUD()
    {
        auto* attr = m_player
            ? m_player->getComponent<game::component::AttributeComponent>()
            : nullptr;
        if (!attr) return;

        constexpr float HP_WIN_W = 360.0f;
        constexpr float HP_WIN_H = 34.0f;
        constexpr float HP_BAR_W = 320.0f;
        constexpr float HP_BAR_H = 16.0f;
        constexpr float PANEL_W = 190.0f;
        constexpr float PANEL_H = 166.0f;

        ImVec2 disp = ImGui::GetIO().DisplaySize;

        ImGui::SetNextWindowPos({(disp.x - HP_WIN_W) * 0.5f, disp.y - HP_WIN_H - 10.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({HP_WIN_W, HP_WIN_H}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.60f);

        ImGui::Begin("##status_hp_hud", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoInputs     | ImGuiWindowFlags_NoNav  |
            ImGuiWindowFlags_NoSavedSettings);

        auto* dl = ImGui::GetWindowDrawList();
        ImVec2 p  = ImGui::GetCursorScreenPos();

        auto drawBottomBar = [&](float y, ImU32 fillCol, float ratio,
                           const char* label, float current, float max)
        {
            float x = p.x + (HP_WIN_W - HP_BAR_W) * 0.5f;
            dl->AddRectFilled({x, y}, {x + HP_BAR_W, y + HP_BAR_H},
                              IM_COL32(22, 24, 30, 210), 5.0f);
            float fillW = HP_BAR_W * std::clamp(ratio, 0.0f, 1.0f);
            if (fillW > 0.5f)
                dl->AddRectFilled({x, y}, {x + fillW, y + HP_BAR_H}, fillCol, 5.0f);
            dl->AddRect({x, y}, {x + HP_BAR_W, y + HP_BAR_H},
                        IM_COL32(110, 120, 140, 220), 5.0f, 0, 1.3f);
            char buf[32];
            snprintf(buf, sizeof(buf), "%s %.0f/%.0f", label, current, max);
            ImVec2 ts = ImGui::CalcTextSize(buf);
            dl->AddText({x + (HP_BAR_W - ts.x) * 0.5f, y + (HP_BAR_H - ts.y) * 0.5f - 1.0f},
                        IM_COL32(245, 245, 245, 230), buf);
        };

        float hpRatio = attr->getHpRatio();
        float seRatio = attr->getStarEnergyRatio();
        float maxHp   = attr->get(game::component::StatType::MaxHp);
        float maxSe   = attr->get(game::component::StatType::MaxStarEnergy);

        drawBottomBar(p.y + 6.0f, IM_COL32(215, 58, 58, 235), hpRatio, "HP", attr->getHp(), maxHp);
        ImGui::End();

        float atk   = attr->get(game::component::StatType::Attack);
        float def   = attr->get(game::component::StatType::Defense);
        float spd   = attr->get(game::component::StatType::Speed);
        float crit  = attr->get(game::component::StatType::CritRate) * 100.0f;

        ImGui::SetNextWindowPos({disp.x - PANEL_W - 14.0f, disp.y - PANEL_H - 14.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({PANEL_W, PANEL_H}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.58f);

        ImGui::Begin("##status_attr_hud", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoInputs     | ImGuiWindowFlags_NoNav  |
            ImGuiWindowFlags_NoSavedSettings);

        ImGui::TextColored({0.78f, 0.86f, 1.0f, 0.96f}, "人物属性");
        ImGui::Separator();
        ImGui::Text("星能 %.0f / %.0f", attr->getStarEnergy(), maxSe);
        ImGui::ProgressBar(seRatio, { -1.0f, 10.0f }, "");
        ImGui::Spacing();
        ImGui::Text("攻击  %.0f", atk);
        ImGui::Text("防御  %.0f", def);
        ImGui::Text("速度  %.2fx", spd);
        ImGui::Text("暴击  %.0f%%", crit);

        if (hpRatio < 0.25f)
        {
            static float blinkTimer = 0.0f;
            blinkTimer += ImGui::GetIO().DeltaTime;
            float alpha = 0.4f + 0.4f * std::sin(blinkTimer * 6.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.25f, 0.25f, alpha));
            ImGui::TextUnformatted("⚠ 生命值危急");
            ImGui::PopStyleColor();
        }
        else if (seRatio < 0.15f)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.6f, 1.0f, 0.75f));
            ImGui::TextUnformatted("★ 星能不足");
            ImGui::PopStyleColor();
        }

        ImGui::End();
    }

    // ──────────────────────────────────────────────────────────────────────────
    //  技能特效 Tick：推进寿命，删除过期特效
    // ──────────────────────────────────────────────────────────────────────────
    void GameScene::tickSkillVFX(float dt)
    {
        for (auto& vfx : m_skillVfxList)
            vfx.age += dt;
        m_skillVfxList.erase(
            std::remove_if(m_skillVfxList.begin(), m_skillVfxList.end(),
                           [](const SkillVFX& v){ return v.age >= v.maxAge; }),
            m_skillVfxList.end());
    }

    void GameScene::tickSkillProjectiles(float dt)
    {
        if (m_skillProjectiles.empty() || !chunk_manager)
            return;

        for (auto &proj : m_skillProjectiles)
        {
            proj.age += dt;
            proj.lastWorldPos = proj.worldPos;
            proj.worldPos += proj.velocity * dt;

            bool explode = false;
            glm::vec2 impactPos = proj.worldPos;
            if (proj.age > 0.04f)
            {
                glm::ivec2 tile = chunk_manager->worldToTile(proj.worldPos);
                if (isProjectileBlockingTile(chunk_manager->tileAt(tile.x, tile.y).type))
                    explode = true;
            }

            glm::vec2 travel = proj.targetPos - proj.originPos;
            float travelLenSq = glm::dot(travel, travel);
            if (!explode && travelLenSq > 1.0f)
            {
                glm::vec2 progressed = proj.worldPos - proj.originPos;
                if (glm::dot(progressed, travel) >= travelLenSq)
                {
                    explode = true;
                    impactPos = proj.targetPos;
                }
            }

            if (!explode && proj.age >= proj.maxAge)
                explode = true;

            if (explode)
            {
                explodeFireBlast(impactPos, proj.radius);
                proj.age = proj.maxAge;
            }
        }

        m_skillProjectiles.erase(
            std::remove_if(m_skillProjectiles.begin(), m_skillProjectiles.end(),
                           [](const SkillProjectile &proj) { return proj.age >= proj.maxAge; }),
            m_skillProjectiles.end());
    }

    void GameScene::tickCombatEffects(float dt)
    {
        for (auto &slash : m_slashVfxList)
            slash.age += dt;
        m_slashVfxList.erase(
            std::remove_if(m_slashVfxList.begin(), m_slashVfxList.end(),
                           [](const SlashVFX &slash) { return slash.age >= slash.maxAge; }),
            m_slashVfxList.end());

        for (auto &fragment : m_combatFragments)
        {
            fragment.age += dt;
            fragment.velocity *= std::max(0.0f, 1.0f - dt * 1.6f);
            fragment.velocity.y += 520.0f * dt;
            fragment.worldPos += fragment.velocity * dt;
        }
        m_combatFragments.erase(
            std::remove_if(m_combatFragments.begin(), m_combatFragments.end(),
                           [](const CombatFragment &fragment) { return fragment.age >= fragment.maxAge; }),
            m_combatFragments.end());
    }

    // ──────────────────────────────────────────────────────────────────────────
    //  技能特效渲染（ImGui 前景层，世界坐标→屏幕坐标）
    // ──────────────────────────────────────────────────────────────────────────
    void GameScene::renderSkillVFX()
    {
        if (m_skillVfxList.empty()) return;

        auto* dl = ImGui::GetForegroundDrawList();
        const auto& cam = _context.getCamera();

        for (const auto& vfx : m_skillVfxList)
        {
            float t = vfx.age / vfx.maxAge;         // 0 → 1
            float ease = 1.0f - t * t;               // 二次缓出
            int   alpha = static_cast<int>(ease * 220.0f);
            if (alpha <= 0) continue;

            glm::vec2 screenLogical = cam.worldToScreen(vfx.worldPos);
            ImVec2 center = logicalToImGuiScreen(_context, screenLogical);

            switch (vfx.type)
            {
            // ── 炎焰：向外扩散的橙红色环 + 中心闪光 + 火星粒子 ──────────
            case game::skill::SkillEffect::FireBlast:
            {
                if (vfx.param < 0.0f)
                {
                    float castPulse = 1.0f - t;
                    float flareR = (10.0f + 18.0f * t) * m_zoomSliderValue;
                    dl->AddCircleFilled(center, flareR,
                        IM_COL32(255, 170, 70, static_cast<int>(castPulse * 150.0f)));
                    for (int ray = 0; ray < 6; ++ray)
                    {
                        float ang = ray * (3.14159f / 3.0f) + t * 1.5f;
                        ImVec2 tip = { center.x + cosf(ang) * flareR * 1.6f,
                                       center.y + sinf(ang) * flareR * 1.6f };
                        dl->AddLine(center, tip,
                            IM_COL32(255, 220, 120, static_cast<int>(castPulse * 220.0f)), 2.0f);
                    }
                    break;
                }

                float blastScale = (vfx.param > 1.0f ? vfx.param / 80.0f : 1.0f);
                // 两圈扩散环
                for (int ring = 0; ring < 4; ++ring)
                {
                    float delay = ring * 0.08f;
                    float rt    = std::clamp((vfx.age - delay) / (vfx.maxAge - delay), 0.0f, 1.0f);
                    if (rt <= 0.0f) continue;
                    float rad   = (36.0f + ring * 24.0f) * rt * m_zoomSliderValue * blastScale;
                    int   a2    = static_cast<int>((1.0f - rt) * 200.0f);
                    ImU32 col   = ring < 2
                        ? IM_COL32(255, 140, 25, a2)
                        : IM_COL32(255, 70, 8, a2 * 3 / 4);
                    dl->AddCircle(center, rad, col, 40, 2.8f);
                }
                if (t < 0.38f)
                {
                    float fr = t / 0.38f;
                    float fr2 = 1.0f - fr;
                    float inner = 22.0f * fr2 * m_zoomSliderValue * blastScale;
                    dl->AddCircleFilled(center, inner,
                        IM_COL32(255,220,120, static_cast<int>(fr2 * 220)));
                }
                for (int ray = 0; ray < 10; ++ray)
                {
                    float ang = ray * (3.14159f * 2.0f / 10.0f) + t * 0.45f;
                    float len = (22.0f + 42.0f * ease) * m_zoomSliderValue * blastScale;
                    ImVec2 tip = { center.x + cosf(ang) * len,
                                   center.y + sinf(ang) * len };
                    dl->AddLine(center, tip, IM_COL32(255, 200, 90, alpha), 1.8f);
                }
                for (int p = 0; p < 16; ++p)
                {
                    float ang = p * (3.14159f * 2.0f / 16.0f) + t * 2.0f;
                    float dist = (30.0f + 34.0f * (p % 3)) * t * m_zoomSliderValue * blastScale;
                    ImVec2 ppos = { center.x + cosf(ang) * dist,
                                    center.y + sinf(ang) * dist };
                    float pSize = (1.4f + (p % 4)) * (1.0f - t) * m_zoomSliderValue;
                    dl->AddCircleFilled(ppos, pSize,
                        IM_COL32(255, 140, 0, alpha));
                }
                dl->AddCircle(center, 18.0f * ease * m_zoomSliderValue,
                              IM_COL32(255, 255, 255, alpha / 2), 32, 1.3f);
                break;
            }

            // ── 寒冰：蓝白扩散圆环 + 6根冰晶射线 ────────────────────────
            case game::skill::SkillEffect::IceAura:
            {
                float outerRad = 60.0f * m_zoomSliderValue;
                float ringRad  = outerRad * (0.3f + 0.7f * t);
                dl->AddCircle(center, ringRad, IM_COL32(80, 200, 255, alpha), 48, 2.0f);
                dl->AddCircle(center, ringRad * 0.6f,
                              IM_COL32(180, 240, 255, alpha / 2), 32, 1.2f);

                // 6 根冰晶射线
                for (int c = 0; c < 6; ++c)
                {
                    float ang  = c * (3.14159f / 3.0f) + t * 0.5f;
                    float len  = outerRad * ease;
                    ImVec2 tip = { center.x + cosf(ang)*len,
                                   center.y + sinf(ang)*len };
                    dl->AddLine(center, tip, IM_COL32(140, 210, 255, alpha), 1.5f);
                    // 尖端小圆
                    dl->AddCircleFilled(tip, 2.5f * ease * m_zoomSliderValue,
                                        IM_COL32(220, 240, 255, alpha));
                }
                break;
            }

            // ── 闪光冲刺：水平残影线 + 源头星爆 ─────────────────────────
            case game::skill::SkillEffect::LightDash:
            {
                float dir     = vfx.param;  // ±1
                float streakL = 50.0f * ease * m_zoomSliderValue;
                // 3条水平残影线（上中下）
                for (int s = -1; s <= 1; ++s)
                {
                    ImVec2 from = { center.x - dir * streakL,
                                    center.y + s * 4.0f * m_zoomSliderValue };
                    ImVec2 to   = { center.x, center.y + s * 4.0f * m_zoomSliderValue };
                    dl->AddLine(from, to, IM_COL32(255, 240, 80, alpha - std::abs(s)*60), 2.0f);
                }
                // 中心星爆 8 线
                for (int sp = 0; sp < 8; ++sp)
                {
                    float ang  = sp * (3.14159f / 4.0f);
                    float len2 = 14.0f * ease * m_zoomSliderValue;
                    ImVec2 tip = { center.x + cosf(ang)*len2,
                                   center.y + sinf(ang)*len2 };
                    dl->AddLine(center, tip, IM_COL32(255, 230, 60, alpha), 1.5f);
                }
                dl->AddCircleFilled(center, 4.0f*ease*m_zoomSliderValue,
                                    IM_COL32(255, 240, 120, alpha));
                break;
            }

            // ── 疾风：绿色旋转残影弧 ─────────────────────────────────────
            case game::skill::SkillEffect::WindBoost:
            {
                for (int arc = 0; arc < 4; ++arc)
                {
                    float baseAng = arc * (3.14159f / 2.0f) + t * 2.0f;
                    float rad2    = 22.0f * m_zoomSliderValue;
                    ImVec2 p0 = { center.x + cosf(baseAng)*rad2,
                                  center.y + sinf(baseAng)*rad2 };
                    ImVec2 p1 = { center.x + cosf(baseAng + 0.8f)*rad2,
                                  center.y + sinf(baseAng + 0.8f)*rad2 };
                    dl->AddLine(p0, p1, IM_COL32(80, 230, 100, alpha), 2.0f);
                }
                break;
            }

            default: break;
            }
        }
    }

    void GameScene::renderSkillProjectiles()
    {
        if (m_skillProjectiles.empty()) return;

        auto *dl = ImGui::GetForegroundDrawList();
        const auto &cam = _context.getCamera();

        for (const auto &proj : m_skillProjectiles)
        {
            glm::vec2 curLogical = cam.worldToScreen(proj.worldPos);
            glm::vec2 prevLogical = cam.worldToScreen(proj.lastWorldPos);
            ImVec2 center = logicalToImGuiScreen(_context, curLogical);
            ImVec2 prev = logicalToImGuiScreen(_context, prevLogical);

            float pulse = 0.72f + 0.28f * std::sin(proj.age * 28.0f);
            dl->AddLine(prev, center, IM_COL32(255, 120, 20, 220), 5.0f * pulse);
            dl->AddLine(prev, center, IM_COL32(255, 220, 120, 180), 2.0f * pulse);
            dl->AddCircleFilled(center, 7.5f * pulse, IM_COL32(255, 150, 30, 230));
            dl->AddCircle(center, 12.0f * pulse, IM_COL32(255, 215, 120, 170), 24, 2.0f);

            glm::vec2 dir = proj.velocity;
            float len = glm::length(dir);
            if (len > 0.001f)
                dir /= len;
            else
                dir = {1.0f, 0.0f};

            glm::vec2 side(-dir.y, dir.x);
            for (int i = 0; i < 3; ++i)
            {
                float back = 10.0f + 8.0f * static_cast<float>(i);
                glm::vec2 emberWorld = proj.worldPos - dir * back + side * ((i - 1) * 4.0f);
                ImVec2 ember = logicalToImGuiScreen(_context, cam.worldToScreen(emberWorld));
                dl->AddCircleFilled(ember, 2.0f + i, IM_COL32(255, 180, 70, 160 - i * 40));
            }
        }
    }

    void GameScene::renderCombatEffects()
    {
        if (m_slashVfxList.empty() && m_combatFragments.empty())
            return;

        auto *dl = ImGui::GetForegroundDrawList();
        const auto &cam = _context.getCamera();

        for (const auto &slash : m_slashVfxList)
        {
            float t = slash.age / slash.maxAge;
            float fade = 1.0f - t;
            glm::vec2 screenLogical = cam.worldToScreen(slash.worldPos);
            ImVec2 center = logicalToImGuiScreen(_context, screenLogical);
            float radius = (32.0f + slash.radius * 0.56f * t) * m_zoomSliderValue;
            float width = (20.0f + 18.0f * fade) * m_zoomSliderValue;
            ImVec2 dir = {slash.facing, -0.15f};
            ImVec2 normal = {-dir.y, dir.x};
            ImVec2 p0 = {center.x - dir.x * width - normal.x * width * 0.55f,
                         center.y - dir.y * width - normal.y * width * 0.55f};
            ImVec2 p1 = {center.x + dir.x * radius - normal.x * radius * 0.18f,
                         center.y + dir.y * radius - normal.y * radius * 0.18f};
            ImVec2 p2 = {center.x + dir.x * (radius + 18.0f * fade) + normal.x * width,
                         center.y + dir.y * (radius + 18.0f * fade) + normal.y * width};
            ImVec2 p3 = {center.x - dir.x * width + normal.x * width * 0.7f,
                         center.y - dir.y * width + normal.y * width * 0.7f};

            dl->AddQuadFilled(p0, p1, p2, p3,
                              IM_COL32(255, 250, 235, static_cast<int>(160.0f * fade)));
            dl->AddQuad(p0, p1, p2, p3,
                        IM_COL32(130, 220, 255, static_cast<int>(220.0f * fade)), 2.0f);
            dl->AddCircle(center, radius * 0.72f,
                          IM_COL32(255, 255, 255, static_cast<int>(90.0f * fade)), 32, 1.2f);
        }

        for (const auto &fragment : m_combatFragments)
        {
            float fade = 1.0f - fragment.age / fragment.maxAge;
            glm::vec2 screenLogical = cam.worldToScreen(fragment.worldPos);
            ImVec2 center = logicalToImGuiScreen(_context, screenLogical);
            glm::vec2 dir = fragment.velocity;
            float len = glm::length(dir);
            if (len > 0.001f)
                dir /= len;
            else
                dir = {1.0f, 0.0f};
            ImVec2 tail = {center.x - dir.x * fragment.size * 2.8f,
                           center.y - dir.y * fragment.size * 2.8f};
            dl->AddLine(tail, center,
                        IM_COL32(255, 180, 140, static_cast<int>(210.0f * fade)), fragment.size);
            dl->AddCircleFilled(center, fragment.size * 0.8f,
                                IM_COL32(255, 245, 225, static_cast<int>(235.0f * fade)));
        }
    }

    void GameScene::renderSkillDebugOverlay()
    {
        if (!m_hasHoveredTile) return;

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 mouseImGui = logicalToImGuiScreen(_context, m_lastMouseLogicalPos);
        ImVec2 mouseWorldImGui = logicalToImGuiScreen(
            _context, _context.getCamera().worldToScreen(m_lastMouseWorldPos));
        ImVec2 tileCenterImGui = logicalToImGuiScreen(
            _context, _context.getCamera().worldToScreen(m_lastHoveredTileCenter));

        drawDebugCross(dl, mouseImGui, IM_COL32(255, 255, 255, 230), 7.0f);
        drawDebugCross(dl, mouseWorldImGui, IM_COL32(80, 220, 255, 230), 8.0f);
        drawDebugCross(dl, tileCenterImGui, IM_COL32(255, 220, 80, 230), 9.0f);

        if (m_hasLastAttackSkillTarget)
        {
            ImVec2 attackImGui = logicalToImGuiScreen(
                _context, _context.getCamera().worldToScreen(m_lastAttackSkillTarget));
            drawDebugCross(dl, attackImGui, IM_COL32(255, 90, 90, 240), 10.0f);
        }

        for (const auto& vfx : m_skillVfxList)
        {
            ImVec2 vfxImGui = logicalToImGuiScreen(
                _context, _context.getCamera().worldToScreen(vfx.worldPos));
            drawDebugCross(dl, vfxImGui, IM_COL32(120, 255, 120, 220), 6.0f);
        }

        for (const auto& proj : m_skillProjectiles)
        {
            ImVec2 projImGui = logicalToImGuiScreen(
                _context, _context.getCamera().worldToScreen(proj.worldPos));
            drawDebugCross(dl, projImGui, IM_COL32(255, 150, 40, 220), 7.0f);
        }

        ImGui::SetNextWindowPos({ImGui::GetIO().DisplaySize.x - 330.0f, 12.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({318.0f, 126.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.72f);
        ImGui::Begin("##skill_target_debug", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextColored({1.0f, 0.95f, 0.60f, 1.0f}, "技能目标调试");
        ImGui::Text("鼠标逻辑: (%.1f, %.1f)", m_lastMouseLogicalPos.x, m_lastMouseLogicalPos.y);
        ImGui::Text("鼠标世界: (%.1f, %.1f)", m_lastMouseWorldPos.x, m_lastMouseWorldPos.y);
        ImGui::Text("悬停格:   (%d, %d)", m_hoveredTile.x, m_hoveredTile.y);
        ImGui::Text("格子中心: (%.1f, %.1f)", m_lastHoveredTileCenter.x, m_lastHoveredTileCenter.y);
        if (m_hasLastAttackSkillTarget)
            ImGui::Text("技能中心: (%.1f, %.1f)", m_lastAttackSkillTarget.x, m_lastAttackSkillTarget.y);
        else
            ImGui::TextUnformatted("技能中心: <尚未触发>");
        ImGui::Text("活动特效: %d", static_cast<int>(m_skillVfxList.size()));
        ImGui::Text("飞行投射物: %d", static_cast<int>(m_skillProjectiles.size()));
        ImGui::TextColored({0.85f, 0.85f, 0.85f, 1.0f}, "白=鼠标屏幕 蓝=鼠标世界 黄=格中心 红=技能 绿=特效 橙=投射物");
        ImGui::End();
    }

    engine::object::GameObject* GameScene::getControlledActor() const
    {
        if (m_isPlayerInMech && m_mech)
            return m_mech;
        return m_player;
    }

    glm::vec2 GameScene::getActorWorldPosition(const engine::object::GameObject* actor) const
    {
        if (!actor)
            return {0.0f, 0.0f};

        auto* transform = actor->getComponent<engine::component::TransformComponent>();
        if (!transform)
            return {0.0f, 0.0f};

        return transform->getPosition();
    }

    glm::vec2 GameScene::getPlayerCastOrigin(glm::vec2 targetPos) const
    {
        glm::vec2 base = getActorWorldPosition(m_player);
        float facing = 1.0f;
        if (auto *ctrl = m_player ? m_player->getComponent<engine::component::ControllerComponent>() : nullptr)
        {
            facing = ctrl->getFacingDirection() == engine::component::ControllerComponent::FacingDirection::Left
                ? -1.0f : 1.0f;
        }
        else if (targetPos.x < base.x)
        {
            facing = -1.0f;
        }

        return base + glm::vec2(12.0f * facing, -10.0f);
    }

    glm::vec2 GameScene::findSafeDisembarkPosition() const
    {
        if (!m_mech)
            return {0.0f, 0.0f};

        auto* mechTransform = m_mech->getComponent<engine::component::TransformComponent>();
        auto* mechController = m_mech->getComponent<engine::component::ControllerComponent>();
        if (!mechTransform || !mechController)
            return {0.0f, 0.0f};

        glm::vec2 mechPos = mechTransform->getPosition();
        int primaryDir = mechController->getFacingDirection() == engine::component::ControllerComponent::FacingDirection::Left ? -1 : 1;
        const int directions[2] = {primaryDir, -primaryDir};

        for (int dir : directions)
        {
            for (int dx = 6; dx <= 12; ++dx)
            {
                int tileX = static_cast<int>(mechPos.x / 16.0f) + dir * dx;
                for (int tileY = 6; tileY < 140; ++tileY)
                {
                    auto below = chunk_manager->tileAt(tileX, tileY);
                    auto above = chunk_manager->tileAt(tileX, tileY - 1);
                    auto above2 = chunk_manager->tileAt(tileX, tileY - 2);
                    if (engine::world::isSolid(below.type) &&
                        above.type == engine::world::TileType::Air &&
                        above2.type == engine::world::TileType::Air)
                    {
                        return {tileX * 16.0f + 8.0f, (tileY - 1) * 16.0f};
                    }
                }
            }
        }

        return mechPos + glm::vec2{primaryDir * 120.0f, -48.0f};
    }
}