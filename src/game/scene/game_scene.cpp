#include "game_scene.h"
#include "menu_scene.h"
#include "ship_scene.h"
#include "../animation/frame_animation_loader.h"
#include "../component/attribute_component.h"
#include "../../engine/scene/scene_manager.h"
#include "../../engine/object/game_object.h"
#include "../../engine/component/transform_component.h"
#include "../../engine/component/sprite_component.h"
#include "../../engine/component/controller_component.h"
#include "../../engine/component/physics_component.h"
#include "../../engine/component/animation_component.h"
#include "../../engine/component/parallax_component.h"
#include "../../engine/core/context.h"
#include "../../engine/core/time.h"
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
#include "dnf_terrain_generator.h"
#include "../../engine/render/renderer.h"
#include "../../engine/render/sdl_renderer.h"
#include "../../engine/render/opengl_renderer.h"
#include "../../engine/ecs/components.h"
#include "../locale/locale_manager.h"
#include "../../engine/utils/math.h"
#include <spdlog/spdlog.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_timer.h>
#include <SDL3_mixer/SDL_mixer.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <mach/mach.h>
#include <filesystem>

namespace game::scene
{

enum class InventoryOwner
{
    Player = 0,
    Mech = 1,
};

struct InventoryDragSlot
{
    int owner = 0;
    int index = 0;
};

struct GroundActorRecord
{
    std::string name;
    std::string texture;
    glm::vec2 position = {0.0f, 0.0f};
    glm::vec2 scale = {1.0f, 1.0f};
    glm::vec2 colliderHalf = {48.0f, 10.0f};
    float rotation = 0.0f;
    bool usePhysics = true;
};

static glm::vec2 readJsonVec2(const nlohmann::json& json, const char* key, const glm::vec2& fallback)
{
    if (!json.contains(key) || !json[key].is_object())
        return fallback;

    const auto& node = json[key];
    return {
        node.value("x", fallback.x),
        node.value("y", fallback.y)
    };
}

static nlohmann::json writeJsonVec2(const glm::vec2& value)
{
    return nlohmann::json{
        {"x", value.x},
        {"y", value.y}
    };
}

static GroundActorRecord readGroundActorRecord(const nlohmann::json& json)
{
    GroundActorRecord record;
    record.name = json.value("name", std::string{"ground_platform"});
    record.texture = json.value("texture", std::string{"assets/textures/Props/platform-long.png"});
    record.position = readJsonVec2(json, "position", {0.0f, 96.0f});
    record.scale = readJsonVec2(json, "scale", {1.0f, 1.0f});
    record.colliderHalf = readJsonVec2(json, "collider_half", {48.0f, 10.0f});
    record.rotation = json.value("rotation", 0.0f);
    record.usePhysics = json.value("use_physics", true);
    return record;
}

static nlohmann::json writeGroundActorRecord(const GroundActorRecord& record)
{
    return nlohmann::json{
        {"name", record.name},
        {"texture", record.texture},
        {"position", writeJsonVec2(record.position)},
        {"scale", writeJsonVec2(record.scale)},
        {"collider_half", writeJsonVec2(record.colliderHalf)},
        {"rotation", record.rotation},
        {"use_physics", record.usePhysics}
    };
}

static bool rectIntersects(const glm::vec4& a, const glm::vec4& b)
{
    return !(a.z < b.x || b.z < a.x || a.w < b.y || b.w < a.y);
}

static const char* inventoryOwnerLabel(InventoryOwner owner)
{
    return owner == InventoryOwner::Mech ? "机甲" : "人物";
}

static void recordPerfMetric(PerfMetric& metric, float elapsedMs)
{
    metric.lastMs = elapsedMs;
    metric.avgMs = (metric.avgMs <= 0.0f) ? elapsedMs : (metric.avgMs * 0.88f + elapsedMs * 0.12f);
    metric.peakMs = std::max(metric.peakMs * 0.985f, elapsedMs);
}

static float perfPercent(float partMs, float totalMs)
{
    if (totalMs <= 0.0001f)
        return 0.0f;
    return std::clamp(partMs / totalMs * 100.0f, 0.0f, 999.0f);
}

static float elapsedMilliseconds(Uint64 startCounter, Uint64 endCounter, Uint64 frequency)
{
    if (endCounter <= startCounter || frequency == 0)
        return 0.0f;
    return static_cast<float>(static_cast<double>(endCounter - startCounter) * 1000.0 / static_cast<double>(frequency));
}

template <typename T, typename FillFn>
static void acquirePooledSlot(std::vector<T>& pool, size_t maxPoolSize, FillFn&& fill)
{
    for (auto& entry : pool)
    {
        if (!entry.active)
        {
            fill(entry);
            entry.active = true;
            return;
        }
    }

    if (pool.size() < maxPoolSize)
    {
        pool.emplace_back();
        fill(pool.back());
        pool.back().active = true;
        return;
    }

    auto it = std::max_element(pool.begin(), pool.end(), [](const T& a, const T& b) {
        const float ar = a.maxAge > 0.0001f ? (a.age / a.maxAge) : a.age;
        const float br = b.maxAge > 0.0001f ? (b.age / b.maxAge) : b.age;
        return ar < br;
    });

    if (it != pool.end())
    {
        fill(*it);
        it->active = true;
    }
}

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
    case game::inventory::ItemCategory::Equipment:
        col = IM_COL32(180,170,120,220);
        sym = "EQ";
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

static float loadFloatSetting(const char* key, float defaultValue = 0.0f)
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

static nlohmann::json loadConfigJsonObject()
{
    std::ifstream file("assets/config.json");
    if (!file.is_open())
        return nlohmann::json::object();

    try
    {
        nlohmann::json j;
        file >> j;
        return j;
    }
    catch (const std::exception&)
    {
        return nlohmann::json::object();
    }
}

static bool loadConfigBool(const char* section, const char* key, bool defaultValue)
{
    const nlohmann::json j = loadConfigJsonObject();
    if (!j.contains(section) || !j[section].is_object())
        return defaultValue;
    return j[section].value(key, defaultValue);
}

static int loadConfigInt(const char* section, const char* key, int defaultValue)
{
    const nlohmann::json j = loadConfigJsonObject();
    if (!j.contains(section) || !j[section].is_object())
        return defaultValue;
    return j[section].value(key, defaultValue);
}

static std::string loadConfigString(const char* section, const char* key, const std::string& defaultValue)
{
    const nlohmann::json j = loadConfigJsonObject();
    if (!j.contains(section) || !j[section].is_object())
        return defaultValue;
    const auto& sec = j[section];
    if (!sec.contains(key) || !sec[key].is_string())
        return defaultValue;
    return sec[key].get<std::string>();
}

template <typename T>
static void saveConfigValue(const char* section, const char* key, T value)
{
    nlohmann::json j = loadConfigJsonObject();
    if (!j.is_object())
        j = nlohmann::json::object();
    if (!j.contains(section) || !j[section].is_object())
        j[section] = nlohmann::json::object();
    j[section][key] = value;

    std::ofstream file("assets/config.json");
    if (!file.is_open())
        return;
    file << j.dump(4);
}

static void applyRendererVSync(engine::render::Renderer& renderer, bool enabled)
{
    if (dynamic_cast<engine::render::OpenGLRenderer*>(&renderer))
    {
        const int adaptiveMode = enabled ? -1 : 0;
        if (!SDL_GL_SetSwapInterval(adaptiveMode) && enabled)
            SDL_GL_SetSwapInterval(1);
        return;
    }

    if (auto* sdlRenderer = dynamic_cast<engine::render::SDLRenderer*>(&renderer))
    {
        const int mode = enabled ? SDL_RENDERER_VSYNC_ADAPTIVE : SDL_RENDERER_VSYNC_DISABLED;
        SDL_SetRenderVSync(sdlRenderer->getSDLRenderer(), mode);
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

static void saveFloatSetting(const char* key, float value)
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

    j[key] = value;

    std::ofstream file("assets/settings.json");
    if (!file.is_open())
        return;
    file << j.dump(4);
}

bool GameScene::isGroundActor(const engine::object::GameObject* actor) const
{
    return actor && actor->getTag() == "Ground";
}

void GameScene::pruneGroundSelection()
{
    if (!actor_manager)
    {
        m_groundSelection.clear();
        return;
    }

    std::unordered_set<const engine::object::GameObject*> validActors;
    for (const auto& holder : actor_manager->getActors())
    {
        const auto* actor = holder.get();
        if (!isGroundActor(actor) || actor->isNeedRemove())
            continue;
        validActors.insert(actor);
    }

    for (auto it = m_groundSelection.begin(); it != m_groundSelection.end();)
    {
        if (!validActors.contains(*it))
            it = m_groundSelection.erase(it);
        else
            ++it;
    }

    for (auto it = m_groundColliderHalfByActor.begin(); it != m_groundColliderHalfByActor.end();)
    {
        if (!validActors.contains(it->first))
            it = m_groundColliderHalfByActor.erase(it);
        else
            ++it;
    }
}

void GameScene::clearPersistedGroundActors()
{
    if (!actor_manager)
        return;

    for (const auto& holder : actor_manager->getActors())
    {
        auto* actor = holder.get();
        if (isGroundActor(actor))
            actor->setNeedRemove(true);
    }

    m_groundSelection.clear();
    m_groundColliderHalfByActor.clear();
    m_selectedActorIndex = -1;
    actor_manager->update(0.0f);
}

void GameScene::loadGroundActorsFromConfig(bool clearExisting)
{
    if (!actor_manager)
        return;

    if (clearExisting)
        clearPersistedGroundActors();

    const nlohmann::json config = loadConfigJsonObject();
    if (!config.contains("editor") || !config["editor"].is_object())
        return;

    const auto& editor = config["editor"];
    if (!editor.contains("ground_actors") || !editor["ground_actors"].is_array())
        return;

    for (const auto& entry : editor["ground_actors"])
    {
        const GroundActorRecord record = readGroundActorRecord(entry);

        auto* ground = actor_manager->createActor(record.name.empty() ? "ground_platform" : record.name);
        if (!ground)
            continue;

        ground->setTag("Ground");
        auto* transform = ground->addComponent<engine::component::TransformComponent>(record.position, record.scale, record.rotation);
        transform->setRotation(record.rotation);
        ground->addComponent<engine::component::SpriteComponent>(record.texture, engine::utils::Alignment::CENTER);

        if (record.usePhysics && physics_manager)
        {
            constexpr float kPixelsPerMeter = 32.0f;
            const glm::vec2 bodyHalfMeters = {
                std::max(1.0f, record.colliderHalf.x) / kPixelsPerMeter,
                std::max(1.0f, record.colliderHalf.y) / kPixelsPerMeter
            };
            const b2BodyId bodyId = physics_manager->createStaticBody(
                {record.position.x / kPixelsPerMeter, record.position.y / kPixelsPerMeter},
                {bodyHalfMeters.x, bodyHalfMeters.y},
                ground);
            ground->addComponent<engine::component::PhysicsComponent>(bodyId, physics_manager.get());
        }

        m_groundColliderHalfByActor[ground] = record.colliderHalf;
    }

    pruneGroundSelection();
    m_groundConfigDirty = false;
}

void GameScene::saveGroundActorsToConfig()
{
    if (!actor_manager)
        return;

    nlohmann::json config = loadConfigJsonObject();
    if (!config.is_object())
        config = nlohmann::json::object();
    if (!config.contains("editor") || !config["editor"].is_object())
        config["editor"] = nlohmann::json::object();

    auto records = nlohmann::json::array();
    for (const auto& holder : actor_manager->getActors())
    {
        const auto* actor = holder.get();
        if (!isGroundActor(actor) || actor->isNeedRemove())
            continue;

        const auto* transform = actor->getComponent<engine::component::TransformComponent>();
        const auto* sprite = actor->getComponent<engine::component::SpriteComponent>();
        if (!transform || !sprite)
            continue;

        GroundActorRecord record;
        record.name = actor->getName();
        record.texture = sprite->getTextureId();
        record.position = transform->getPosition();
        record.scale = transform->getScale();
        record.rotation = transform->getRotation();
        record.usePhysics = actor->hasComponent<engine::component::PhysicsComponent>();
        record.colliderHalf = m_groundColliderHalfByActor.contains(actor)
            ? m_groundColliderHalfByActor.at(actor)
            : glm::vec2{
                std::max(8.0f, sprite->getSpriteSize().x * std::abs(record.scale.x) * 0.5f),
                std::max(8.0f, sprite->getSpriteSize().y * std::abs(record.scale.y) * 0.5f)
            };

        records.push_back(writeGroundActorRecord(record));
    }

    config["editor"]["ground_actors"] = std::move(records);

    std::ofstream file("assets/config.json");
    if (!file.is_open())
        return;
    file << config.dump(4);
    m_groundConfigDirty = false;
}

void GameScene::snapSelectedGroundActorsToGrid()
{
    if (!actor_manager)
        return;

    pruneGroundSelection();
    for (const auto* actor : m_groundSelection)
    {
        if (!isGroundActor(actor))
            continue;

        auto* mutableActor = const_cast<engine::object::GameObject*>(actor);
        auto* transform = mutableActor->getComponent<engine::component::TransformComponent>();
        if (!transform)
            continue;

        const glm::vec2 snappedPos = snapGroundMakerPosition(transform->getPosition());
        transform->setPosition(snappedPos);

        if (auto* physics = mutableActor->getComponent<engine::component::PhysicsComponent>())
            physics->setWorldPosition(snappedPos);
    }

    if (!m_groundSelection.empty())
        m_groundConfigDirty = true;
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

        loadActorRoleConfig();

        physics_manager = std::make_unique<engine::physics::PhysicsManager>();
        // DNF 2.5D: Y轴为深度方向，由控制器软边界管理，不需要 Box2D 重力
        physics_manager->init({0.0f, 0.0f});

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
        m_vsyncEnabled = loadConfigBool("graphics", "vsync", true);
        m_maxFpsSlider = loadConfigInt("performance", "target_fps", 60);
        m_showSkillDebugOverlay = loadBoolSetting("show_skill_debug_overlay");
        m_showActiveChunkHighlights = loadBoolSetting("show_active_chunk_highlights");
        m_invertPlayerFacing = loadBoolSetting("invert_player_facing");
        m_showEditorToolbar = loadBoolSetting("show_editor_toolbar", true);
        m_showHierarchyPanel = loadBoolSetting("show_hierarchy_panel", true);
        m_showInspectorPanel = loadBoolSetting("show_inspector_panel", true);
        m_toolbarShowPlayControls = loadBoolSetting("toolbar_show_play_controls", true);
        m_toolbarShowWindowControls = loadBoolSetting("toolbar_show_window_controls", true);
        m_toolbarShowDebugControls = loadBoolSetting("toolbar_show_debug_controls", false);
        m_hierarchyGroupByTag = loadBoolSetting("hierarchy_group_by_tag", false);
        m_hierarchyFavoritesOnly = loadBoolSetting("hierarchy_favorites_only", false);
        m_enablePlayRollback = loadBoolSetting("enable_play_rollback", true);
        m_screenRainOverlay = loadBoolSetting("screen_rain_overlay");
        m_screenRainOverlayStrength = std::clamp(loadFloatSetting("screen_rain_overlay_strength", 1.0f), 0.2f, 2.0f);
        m_screenRainMotionStrength = std::clamp(loadFloatSetting("screen_rain_motion_strength", 1.0f), 0.2f, 3.0f);
        m_weatherSystem.setScreenRainOverlayEnabled(m_screenRainOverlay);
        m_weatherSystem.setScreenRainOverlayStrength(m_screenRainOverlayStrength);
        m_weatherSystem.setScreenRainMotionScale(m_screenRainMotionStrength);
        // 编辑器优先：启动后先进入编辑态，点击“启动游戏”才真正推进玩法。
        m_gameplayRunning = false;
        m_devMode = true;
        m_showSettings = true;

        setupGroundTileScene(config);

        actor_manager = std::make_unique<engine::actor::ActorManager>(_context);
    loadGroundActorsFromConfig(false);
        createPlayer();
        // 专注地面玩法：暂时禁用天空/建筑背景层。

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
        }

        if (m_glContext)
        {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO &io = ImGui::GetIO();
            (void)io;
            ImGui::StyleColorsDark();

            if (!io.Fonts->AddFontFromFileTTF(
                    "assets/fonts/VonwaonBitmap-16px.ttf",
                    16.0f,
                    nullptr,
                    io.Fonts->GetGlyphRangesChineseSimplifiedCommon()))
            {
                spdlog::error("GameScene: 加载 ImGui 字体失败 assets/fonts/VonwaonBitmap-16px.ttf");
            }

            if (!ImGui_ImplSDL3_InitForOpenGL(window, m_glContext))
                spdlog::error("Failed to init ImGui SDL3 backend");
            if (!ImGui_ImplOpenGL3_Init("#version 330"))
                spdlog::error("Failed to init ImGui OpenGL3 backend");
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

        warmupSceneTextures();
        initializeGroundChunksAndTrees();
        preallocateRuntimeBuffers();

        // 在目标区域注入矿脉（需在区块加载后）
        injectOreVeins();
        generateRockObstacles();
    }

    void GameScene::setupGroundTileScene(const engine::world::WorldConfig& config)
    {
        chunk_manager = std::make_unique<engine::world::ChunkManager>(
            "assets/textures/Tiles/tileset.svg",
            config.TILE_SIZE,
            &_context.getResourceManager(),
            physics_manager.get());

        auto generator = std::make_unique<game::scene::DnfTerrainGenerator>(config);
        // 地面瓦片按路线地形生成（地面主场景层）。
        if (m_routeData.isValid())
        {
            auto rd = m_routeData;
            generator->setBiomeLookup([rd](int tileX) -> int {
                int zone = tileX / game::route::RouteData::TILES_PER_CELL;
                if (zone < 0 || zone >= static_cast<int>(rd.path.size()))
                    return 0;
                auto cell = rd.path[zone];
                return static_cast<int>(rd.terrain[cell.y][cell.x]);
            });
        }
        chunk_manager->setTerrainGenerator(std::move(generator));
    }

    void GameScene::setupSkyBackgroundScene()
    {
        // 空中场景：天空背景层（最远层，Y 轴视差最小）。
        auto *sky = actor_manager->createActor("bg_sky");
        sky->addComponent<engine::component::TransformComponent>(
            glm::vec2{0.0f, 48.0f}, glm::vec2{1.2f, 1.2f});
        sky->addComponent<engine::component::ParallaxComponent>(
            "assets/textures/Layers/back.png",
            glm::vec2{0.08f, 0.05f},
            glm::bvec2{true, false});
    }

    void GameScene::setupGroundBuildingBackgroundScene()
    {
        // 地面场景：建筑背景层（远景 / 中景）。
        auto *bldgFar = actor_manager->createActor("bg_building_far");
        bldgFar->addComponent<engine::component::TransformComponent>(
            glm::vec2{0.0f, 155.0f}, glm::vec2{2.0f, 1.2f});
        bldgFar->addComponent<engine::component::ParallaxComponent>(
            "assets/textures/Props/big-house.png",
            glm::vec2{0.30f, 0.22f},
            glm::bvec2{true, false});

        auto *bldgMid = actor_manager->createActor("bg_building_mid");
        bldgMid->addComponent<engine::component::TransformComponent>(
            glm::vec2{80.0f, 184.0f}, glm::vec2{1.8f, 1.0f});
        bldgMid->addComponent<engine::component::ParallaxComponent>(
            "assets/textures/Props/tree-house.png",
            glm::vec2{0.55f, 0.45f},
            glm::bvec2{true, false});
    }

    void GameScene::warmupSceneTextures()
    {
        auto& resMgr = _context.getResourceManager();
        resMgr.getGLTexture("assets/textures/Tiles/tileset.svg");
        resMgr.getGLTexture("assets/textures/Characters/player_sheet.svg");
        resMgr.getGLTexture("assets/textures/Actors/eagle-attack.png");
        resMgr.getGLTexture("assets/textures/Actors/opossum.png");
        resMgr.getGLTexture("assets/textures/Actors/frog.png");
        resMgr.getGLTexture("assets/textures/Props/tileset_atlas.svg");
        resMgr.getGLTexture("assets/textures/Props/rock.png");
        resMgr.getGLTexture("assets/textures/Props/rock-1.png");
        resMgr.getGLTexture("assets/textures/Props/rock-2.png");
        // 背景纹理：天空 + 地面建筑。
        resMgr.getGLTexture("assets/textures/Layers/back.png");
        resMgr.getGLTexture("assets/textures/Props/big-house.png");
        resMgr.getGLTexture("assets/textures/Props/tree-house.png");
        spdlog::info("纹理预热完毕（天空层/地面建筑层/地面瓦片层）");
    }

    void GameScene::initializeGroundChunksAndTrees()
    {
        // 地面瓦片运行区：固定在地面走廊 chunk row。
        constexpr float DNF_ROW_Y = 80.0f;
        chunk_manager->setHorizontalOnly(true, DNF_ROW_Y);
        chunk_manager->updateVisibleChunks({0.0f, DNF_ROW_Y}, 3);
        for (int cx = -3; cx <= 3; ++cx)
            for (int cy = 0; cy <= 0; ++cy)
                m_treeManager.generateTreesForChunk(cx, cy, *chunk_manager, m_worldConfig);

        spdlog::info("TreeManager: 初始区域树木生成完毕（min height={}, max height={}, spacing={})",
            m_worldConfig.treeMinTrunkHeight, m_worldConfig.treeMaxTrunkHeight, m_worldConfig.treeSpacing);
    }

    void GameScene::preallocateRuntimeBuffers()
    {
        // 减少运行时频繁扩容导致的卡顿尖峰（不是固定内存锁死，只是提前保留容量）。
        m_skillVfxList.reserve(256);
        m_skillProjectiles.reserve(128);
        m_slashVfxList.reserve(128);
        m_combatFragments.reserve(256);
    }

    void GameScene::updateFlightAmbientSound(float dt)
    {
        constexpr const char* kFlightAmbientPath = "assets/audio/hurry_up_and_run.ogg";
        constexpr const char* kTakeoffWhooshPath = "assets/audio/cartoon-jump-6462.mp3";

        auto* controlled = getControlledActor();
        auto* ctrl = controlled ? controlled->getComponent<engine::component::ControllerComponent>() : nullptr;
        auto* phys = controlled ? controlled->getComponent<engine::component::PhysicsComponent>() : nullptr;

        const bool flyMode = ctrl && ctrl->isFlyModeActive();
        const float altitudeMeters = ctrl ? std::max(0.0f, ctrl->getPosZ() / 32.0f) : 0.0f;
        const float verticalSpeed = phys ? std::abs(phys->getVelocity().y) : 0.0f;

        auto smoothstep = [](float edge0, float edge1, float x) {
            float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 0.0001f), 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };

        float targetGain = 0.0f;
        if (flyMode)
        {
            const float altT = smoothstep(60.0f, 900.0f, altitudeMeters);
            const float speedT = std::clamp(verticalSpeed / 220.0f, 0.0f, 1.0f);
            targetGain = std::clamp(0.18f + altT * 0.54f + speedT * 0.16f, 0.0f, 0.92f);
        }

        auto& resourceManager = _context.getResourceManager();
        MIX_Mixer* mixer = resourceManager.getAudioMixer();

        if (flyMode && !m_flightAmbientWasFlyMode && mixer)
        {
            if (MIX_Audio* liftAudio = resourceManager.getAudio(kTakeoffWhooshPath))
            {
                if (!MIX_PlayAudio(mixer, liftAudio))
                    spdlog::debug("起飞瞬态音播放失败: {}", SDL_GetError());
            }
        }

        if (!m_flightAmbientReady && mixer)
        {
            MIX_Audio* ambientAudio = resourceManager.getAudio(kFlightAmbientPath);
            if (ambientAudio)
            {
                m_flightAmbientTrack = MIX_CreateTrack(mixer);
                if (m_flightAmbientTrack)
                {
                    MIX_SetTrackAudio(m_flightAmbientTrack, ambientAudio);
                    MIX_SetTrackLoops(m_flightAmbientTrack, -1);
                    MIX_SetTrackGain(m_flightAmbientTrack, 0.0f);
                    if (MIX_PlayTrack(m_flightAmbientTrack, 0))
                    {
                        m_flightAmbientReady = true;
                        spdlog::info("飞行环境音已启动");
                    }
                    else
                    {
                        spdlog::warn("飞行环境音播放失败: {}", SDL_GetError());
                    }
                }
            }
        }

        const float response = (targetGain > m_flightAmbientGain) ? 2.8f : 2.0f;
        const float lerpT = std::clamp(dt * response, 0.0f, 1.0f);
        m_flightAmbientGain += (targetGain - m_flightAmbientGain) * lerpT;

        if (m_flightAmbientTrack)
            MIX_SetTrackGain(m_flightAmbientTrack, m_flightAmbientGain);

        m_flightAmbientWasFlyMode = flyMode;
    }

    void GameScene::shutdownFlightAmbientSound()
    {
        if (m_flightAmbientTrack)
        {
            MIX_StopTrack(m_flightAmbientTrack, 0);
            MIX_DestroyTrack(m_flightAmbientTrack);
            m_flightAmbientTrack = nullptr;
        }

        m_flightAmbientReady = false;
        m_flightAmbientGain = 0.0f;
        m_flightAmbientWasFlyMode = false;
    }

    void GameScene::emitSkillVFX(game::skill::SkillEffect type, glm::vec2 worldPos, float maxAge, float param)
    {
        acquirePooledSlot(m_skillVfxList, 512, [&](SkillVFX& vfx) {
            vfx.type = type;
            vfx.worldPos = worldPos;
            vfx.age = 0.0f;
            vfx.maxAge = maxAge;
            vfx.param = param;
        });
    }

    void GameScene::emitSkillProjectile(game::skill::SkillEffect type,
                                        glm::vec2 originPos,
                                        glm::vec2 worldPos,
                                        glm::vec2 lastWorldPos,
                                        glm::vec2 targetPos,
                                        glm::vec2 velocity,
                                        float maxAge,
                                        float radius)
    {
        acquirePooledSlot(m_skillProjectiles, 192, [&](SkillProjectile& proj) {
            proj.type = type;
            proj.originPos = originPos;
            proj.worldPos = worldPos;
            proj.lastWorldPos = lastWorldPos;
            proj.targetPos = targetPos;
            proj.velocity = velocity;
            proj.age = 0.0f;
            proj.maxAge = maxAge;
            proj.radius = radius;
        });
    }

    void GameScene::emitSlashVFX(glm::vec2 worldPos, float facing, float maxAge, float radius)
    {
        acquirePooledSlot(m_slashVfxList, 256, [&](SlashVFX& slash) {
            slash.worldPos = worldPos;
            slash.facing = facing;
            slash.age = 0.0f;
            slash.maxAge = maxAge;
            slash.radius = radius;
        });
    }

    void GameScene::emitCombatFragment(glm::vec2 worldPos, glm::vec2 velocity, float maxAge, float size)
    {
        acquirePooledSlot(m_combatFragments, 640, [&](CombatFragment& fragment) {
            fragment.worldPos = worldPos;
            fragment.velocity = velocity;
            fragment.age = 0.0f;
            fragment.maxAge = maxAge;
            fragment.size = size;
        });
    }

    void GameScene::update(float delta_time)
    {
        const Uint64 perfFreq = SDL_GetPerformanceFrequency();
        const Uint64 updateStart = SDL_GetPerformanceCounter();
        m_frameProfiler.frameDeltaMs = delta_time * 1000.0f;

        auto measure = [&](PerfMetric& metric, auto&& fn) {
            const Uint64 start = SDL_GetPerformanceCounter();
            fn();
            recordPerfMetric(metric, elapsedMilliseconds(start, SDL_GetPerformanceCounter(), perfFreq));
        };

        const bool runGameplayTick = m_gameplayRunning && (!m_gameplayPaused || m_stepOneFrame);
        if (!runGameplayTick)
        {
            measure(m_frameProfiler.coreLogic, [&] {
                Scene::update(delta_time);
                updateSettingsParticles(delta_time);
            });
            measure(m_frameProfiler.cameraUpdate, [&] {
                _context.getCamera().update(delta_time);
            });
            measure(m_frameProfiler.chunkStreamUpdate, [&] {
                if (chunk_manager)
                    chunk_manager->rebuildDirtyChunks(2);
            });

            m_frameProfiler.loadedChunks = chunk_manager ? chunk_manager->loadedChunkCount() : 0;
            m_frameProfiler.pendingChunkLoads = chunk_manager ? chunk_manager->pendingChunkLoadCount() : 0;
            recordPerfMetric(m_frameProfiler.updateTotal,
                             elapsedMilliseconds(updateStart, SDL_GetPerformanceCounter(), perfFreq));
            return;
        }

        measure(m_frameProfiler.coreLogic, [&] {
            Scene::update(delta_time);

            m_mechAttackCooldown = std::max(0.0f, m_mechAttackCooldown - delta_time);
            m_mechAttackFlashTimer = std::max(0.0f, m_mechAttackFlashTimer - delta_time);
            m_weaponAttackCooldown = std::max(0.0f, m_weaponAttackCooldown - delta_time);
            m_possessedAttackCooldown = std::max(0.0f, m_possessedAttackCooldown - delta_time);
            m_possessedSkillCooldown = std::max(0.0f, m_possessedSkillCooldown - delta_time);
            m_timeOfDaySystem.update(delta_time);
            updateMechFlightCapability();
            updateEquipmentAttributeBonuses();

            // ── 飞行/陆地模式切换提示 ──
            m_modeSwitchHintTimer = std::max(0.0f, m_modeSwitchHintTimer - delta_time);
            if (auto *controlled = getControlledActor())
            {
                auto* ctrl = controlled->getComponent<engine::component::ControllerComponent>();
                if (ctrl)
                {
                    bool curFly = ctrl->isFlyModeActive();
                    if (curFly != m_prevFlyModeActive)
                    {
                        m_modeSwitchHintText  = curFly ? "飞行模式" : "陆地模式";
                        m_modeSwitchHintTimer = 1.8f;
                        m_prevFlyModeActive   = curFly;
                    }
                }
            }

            updateFlightAmbientSound(delta_time);

            // 机甲飞行时解除相机 Y 轴锁定，让视角跟随机甲上升/下降。
            bool unlockCameraY = false;
            if (m_isPlayerInMech && m_mech)
            {
                if (auto* mechCtrl = m_mech->getComponent<engine::component::ControllerComponent>())
                    unlockCameraY = mechCtrl->isFlyModeActive() || mechCtrl->getPosZ() > 1.0f;

                if (auto* mechTransform = m_mech->getComponent<engine::component::TransformComponent>())
                    _context.getCamera().setFollowTarget(&mechTransform->getPosition(), unlockCameraY ? 8.5f : 4.2f);
            }
            _context.getCamera().setLockY(!unlockCameraY, 40.0f);

            if (m_comboResetTimer > 0.0f)
            {
                m_comboResetTimer -= delta_time;
                if (m_comboResetTimer <= 0.0f)
                    m_comboCount = 0;
            }

            m_doubleTapTimer = std::max(0.0f, m_doubleTapTimer - delta_time);
            m_dashCooldown = std::max(0.0f, m_dashCooldown - delta_time);
            if (m_isDashing && m_player)
            {
                auto* physics = m_player->getComponent<engine::component::PhysicsComponent>();
                if (physics)
                {
                    glm::vec2 vel = physics->getVelocity();
                    vel.x = m_dashFacing * 320.0f;
                    vel.y *= 0.5f;
                    physics->setVelocity(vel);

                    if (auto* ctrl = m_player->getComponent<engine::component::ControllerComponent>())
                        ctrl->setEnabled(false);
                }

                m_dashTimer -= delta_time;
                if (m_dashTimer <= 0.0f)
                {
                    m_isDashing = false;
                    if (auto* ctrl = m_player->getComponent<engine::component::ControllerComponent>())
                        ctrl->setEnabled(true);
                    if (auto* physics = m_player->getComponent<engine::component::PhysicsComponent>())
                    {
                        glm::vec2 vel = physics->getVelocity();
                        vel.x *= 0.4f;
                        physics->setVelocity(vel);
                    }
                }
            }

            tickStarSkillPassives(delta_time);
            tickSkillVFX(delta_time);
            tickSkillProjectiles(delta_time);
            tickCombatEffects(delta_time);
            updateSettingsParticles(delta_time);

            m_attackAnimTimer  = std::max(0.0f, m_attackAnimTimer  - delta_time);
            m_heavyAttackTimer = std::max(0.0f, m_heavyAttackTimer - delta_time);
            m_ultimateTimer    = std::max(0.0f, m_ultimateTimer    - delta_time);
            m_cannonTimer      = std::max(0.0f, m_cannonTimer      - delta_time);
        });

        measure(m_frameProfiler.monsterUpdate, [&] {
            if (m_monsterManager)
            {
                m_monsterManager->setAnchorActor(getControlledActor());
                m_monsterManager->setHostileTarget(m_possessedMonster ? m_possessedMonster : m_player);
                m_monsterManager->update(delta_time);
            }
        });

        measure(m_frameProfiler.cameraUpdate, [&] {
            _context.getCamera().update(delta_time);
        });

        measure(m_frameProfiler.physicsUpdate, [&] {
            if (physics_manager)
            {
                const float physDt = std::min(delta_time, 1.0f / 30.0f);
                physics_manager->update(physDt, 2);
            }
        });

        measure(m_frameProfiler.actorUpdate, [&] {
            if (actor_manager)
                actor_manager->update(delta_time);
        });

        updatePossession(delta_time);

        measure(m_frameProfiler.stateMachineUpdate, [&] {
            tickPlayerSM(delta_time);
            syncPlayerPresentation();
        });

        // 每秒输出一次玩家信息
        static float timer = 0.0f;
        timer += delta_time;
        if (timer >= 1.0f && m_player)
        {
            auto* transform = m_player->getComponent<engine::component::TransformComponent>();
            if (transform)
            {
                float height = transform->getPosition().y;
                spdlog::debug("Player: {} | Height: {:.1f}", m_player->getName(), height);
            }
            timer = 0.0f;
        }

        // 更新掉落物（重力、拾取）
        measure(m_frameProfiler.dropUpdate, [&] {
            glm::vec2 ppos = getActorWorldPosition(getControlledActor());
            m_treeManager.updateDrops(delta_time, ppos, m_inventory, *chunk_manager);
        });

        // 更新天气
        measure(m_frameProfiler.weatherUpdate, [&] {
            const auto &io = ImGui::GetIO();
            glm::vec2 rainMotion{0.0f, 0.0f};
            if (auto* actor = getControlledActor())
            {
                if (auto* physics = actor->getComponent<engine::component::PhysicsComponent>())
                    rainMotion = physics->getVelocity();
            }
            const auto& cam = _context.getCamera();
            const glm::vec2 camPos = cam.getPosition();
            m_weatherSystem.setViewMotion(rainMotion.x, rainMotion.y);
            m_weatherSystem.setCameraState(camPos.x, camPos.y, cam.getZoom(), cam.getPseudo3DVerticalScale());
            m_weatherSystem.update(delta_time, io.DisplaySize.x, io.DisplaySize.y);
        });

        // 更新星球任务规划 UI
        measure(m_frameProfiler.missionUpdate, [&] {
            glm::vec2 ppos = getActorWorldPosition(getControlledActor());
            m_missionUI.update(delta_time, ppos, *chunk_manager);
        });

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

        const float chunkWorldWidth = static_cast<float>(engine::world::Chunk::SIZE * chunk_manager->getTileSize().x);
        const int currentChunkX = static_cast<int>(std::floor(playerPos.x / chunkWorldWidth));
        const int lastChunkX = static_cast<int>(std::floor(m_lastChunkUpdatePos.x / chunkWorldWidth));
        measure(m_frameProfiler.chunkStreamUpdate, [&] {
            if (currentChunkX != lastChunkX)
            {
                chunk_manager->updateVisibleChunks({playerPos.x, 0.0f}, 3);
                m_lastChunkUpdatePos = playerPos;
            }

            // 限额重建脏区块，避免技能批量破坏时单帧重建过多 chunk 导致卡顿
            chunk_manager->rebuildDirtyChunks(1);
        });

        m_frameProfiler.loadedChunks = chunk_manager ? chunk_manager->loadedChunkCount() : 0;
        m_frameProfiler.pendingChunkLoads = chunk_manager ? chunk_manager->pendingChunkLoadCount() : 0;
        if (m_stepOneFrame)
            m_stepOneFrame = false;
        recordPerfMetric(m_frameProfiler.updateTotal,
                         elapsedMilliseconds(updateStart, SDL_GetPerformanceCounter(), perfFreq));
    }

    void GameScene::render()
    {
        const Uint64 perfFreq = SDL_GetPerformanceFrequency();
        const Uint64 renderStart = SDL_GetPerformanceCounter();
        auto measure = [&](PerfMetric& metric, auto&& fn) {
            const Uint64 start = SDL_GetPerformanceCounter();
            fn();
            recordPerfMetric(metric, elapsedMilliseconds(start, SDL_GetPerformanceCounter(), perfFreq));
        };

        {
            const Uint64 backgroundStart = SDL_GetPerformanceCounter();

            const Uint64 sceneStart = SDL_GetPerformanceCounter();
            Scene::render();
            recordPerfMetric(m_frameProfiler.sceneRender,
                             elapsedMilliseconds(sceneStart, SDL_GetPerformanceCounter(), perfFreq));

            // 专注地面玩法：暂时关闭天空背景渲染。
            recordPerfMetric(m_frameProfiler.skyBackgroundRender, 0.0f);

            recordPerfMetric(m_frameProfiler.backgroundRender,
                             elapsedMilliseconds(backgroundStart, SDL_GetPerformanceCounter(), perfFreq));
        }
        measure(m_frameProfiler.chunkRender, [&] { chunk_manager->renderAll(_context); });
        measure(m_frameProfiler.parallaxRender, [&] { _context.getParallaxRenderSystem().renderAll(_context); });
        measure(m_frameProfiler.spriteRender, [&] { _context.getSpriteRenderSystem().renderAll(_context); });
        measure(m_frameProfiler.tileRender, [&] { _context.getTilelayerRenderSystem().renderAll(_context); });
        measure(m_frameProfiler.shadowRender, [&] { renderActorGroundShadows(); });

        measure(m_frameProfiler.actorRender, [&] {
            if (actor_manager)
                actor_manager->render();
        });

        measure(m_frameProfiler.lightingRender, [&] {
            m_timeOfDaySystem.renderLighting(_context);
        });

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
            // 极淡底色，ImGui 前景层负责主要效果
            _context.getRenderer().drawRect(_context.getCamera(), tileWorldPos.x, tileWorldPos.y, tileSize.x, tileSize.y,
                                            glm::vec4(1.0f, 0.88f, 0.2f, 0.07f));
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
        measure(m_frameProfiler.imguiRender, [&] {
        if (m_glContext)
        {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();

            if (m_showFpsOverlay)
                renderPerformanceOverlay();

            renderEditorToolbar();
            renderHierarchyPanel();
            renderInspectorPanel();

            renderSettingsPage();
            renderMapEditor();

            float displayW = ImGui::GetIO().DisplaySize.x;
            float displayH = ImGui::GetIO().DisplaySize.y;

            // ── 高度驱动环境混合：地面态 -> 高空态 ──
            float altitudeMeters = 0.0f;
            float verticalSpeed = 0.0f;
            bool flyMode = false;
            if (auto *controlled = getControlledActor())
            {
                if (auto *ctrl = controlled->getComponent<engine::component::ControllerComponent>())
                {
                    altitudeMeters = std::max(0.0f, ctrl->getPosZ() / 32.0f);
                    flyMode = ctrl->isFlyModeActive();
                }
                if (auto *phys = controlled->getComponent<engine::component::PhysicsComponent>())
                    verticalSpeed = phys->getVelocity().y;
            }

            auto smoothstep = [](float edge0, float edge1, float x) {
                float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 0.0001f), 0.0f, 1.0f);
                return t * t * (3.0f - 2.0f * t);
            };

            const float altitudeT = smoothstep(120.0f, 900.0f, altitudeMeters);   // 高度主混合
            const float cloudT = smoothstep(220.0f, 1400.0f, altitudeMeters);      // 云层增强
            const float speedT = std::clamp(std::abs(verticalSpeed) / 220.0f, 0.0f, 1.0f);

            // 专注地面玩法：禁用 ImGui 背景氛围层。

            // 告知天气系统地面屏幕 Y 波段（整个 GroundDecor 走廊范围）
            {
                glm::vec2 logMin = _context.getCamera().worldToScreen({0.0f, 16.0f});  // wy=1 顶边
                glm::vec2 logMax = _context.getCamera().worldToScreen({0.0f, 96.0f});  // wy=5 底边
                ImVec2 dispMin   = logicalToImGuiScreen(_context, logMin);
                ImVec2 dispMax   = logicalToImGuiScreen(_context, logMax);
                m_weatherSystem.setGroundScreenBand(dispMin.y, dispMax.y);
            }

            // 专注地面玩法：禁用天气背景与前景层。

            // 天气 / 时间 HUD（右上角常驻，按 ESC 或 ` 打开完整设置）
            ImGui::SetNextWindowPos(ImVec2(displayW - 16.0f, 20.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
            ImGui::Begin("##weather_hud", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove);
            ImGui::Text("%02d:%02d %s  %s",
                        m_timeOfDaySystem.getHour24(),
                        m_timeOfDaySystem.getMinute(),
                        m_timeOfDaySystem.getPhaseName(),
                        m_weatherSystem.getCurrentWeatherName());
            if (m_isPlayerInMech)
            {
                const std::string mechName = !m_hudMechName.empty()
                    ? m_hudMechName
                    : (m_mech ? m_mech->getName() : m_mechActorKey);
                ImGui::Text("%s%s", m_hudMechPrefix.c_str(), mechName.c_str());
            }
            else
            {
                ImGui::TextUnformatted(m_hudPlayerText.c_str());
            }

            constexpr float kAltitudeGaugeMaxMeters = 2000.0f;
            const float altitudeRatio = std::clamp(altitudeMeters / kAltitudeGaugeMaxMeters, 0.0f, 1.0f);
            ImGui::Separator();
            ImGui::Text("高度: %.1f m", altitudeMeters);
            ImGui::ProgressBar(altitudeRatio, ImVec2(120.0f, 0.0f));
            ImGui::TextDisabled("%s", flyMode ? "飞行中" : "地面模式");

            if (auto* controlled = getControlledActor())
            {
                const glm::vec2 worldPos = getActorWorldPosition(controlled);
                const int tileX = static_cast<int>(std::floor(worldPos.x / 16.0f));
                const int tileY = static_cast<int>(std::floor(worldPos.y / 16.0f));
                if (m_routeData.isValid() && !m_routeData.path.empty())
                {
                    int zone = tileX / game::route::RouteData::TILES_PER_CELL;
                    zone = std::clamp(zone, 0, static_cast<int>(m_routeData.path.size()) - 1);
                    const glm::ivec2 cell = m_routeData.path[zone];
                    const int localTileX = tileX - zone * game::route::RouteData::TILES_PER_CELL;
                    ImGui::Text("位置: %s  区域 %d/%d",
                                game::route::RouteData::cellLabel(cell).c_str(),
                                zone + 1,
                                static_cast<int>(m_routeData.path.size()));
                    ImGui::TextDisabled("地块: X%d Y%d  区内 %d/%d",
                                        tileX,
                                        tileY,
                                        std::max(localTileX, 0),
                                        game::route::RouteData::TILES_PER_CELL);
                }
                else
                {
                    ImGui::Text("位置: X%d  Y%d", tileX, tileY);
                }
            }

            ImGui::TextDisabled("ESC / ` 设置");
            ImGui::End();

            // 左侧武器栏（常显示）
            renderWeaponBar();

            // 左上角属性面板（血量 / 星能 / 属性）
            renderPlayerStatusHUD();

            // 底部居中星技 HUD
            renderSkillHUD();

            // 玩家头顶状态名
            renderPlayerStateTag();
            renderModeSwitchHint();
            renderFlightThrusterFX();
            renderMonsterIFFMarkers();

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

            // ── 动作序列帧编辑器 ─────────────────────────────────────────────
            m_frameEditor.render(_context.getResourceManager());
            // 帧编辑器内部"跳转→状态机编辑器"按钮
            if (m_frameEditor.wantsSmEditor())
            {
                m_frameEditor.clearSmEditorRequest();
                m_smEditor.open();
            }

            // ── 状态机编辑器 ────────────────────────────────────────────────
            m_smEditor.render();
            // 用户在 SM 编辑器里刚选了新文件 → 同步加载到游戏状态机
            if (m_smEditor.takeJustLoaded())
                loadPlayerSM(m_smEditor.getSavePath());

            // 开发模式：右上角角色选择器覆盖层
            if (m_devMode) renderDevModeOverlay();

            // 星球任务规划 UI
            {
                glm::vec2 ppos = getActorWorldPosition(getControlledActor());
                m_missionUI.render(*chunk_manager, ppos);
            }

            // 技能特效（前景层，覆盖在所有 ImGui 窗口之上）
            renderCombatEffects();
            renderSkillProjectiles();
            renderSkillVFX();
            if (m_devMode && m_showSkillDebugOverlay)
                renderSkillDebugOverlay();

            // ── Chunk 调试红线边框（仅开发模式 + 勾选时显示）────────────────────
            if (chunk_manager && m_devMode && m_showActiveChunkHighlights)
            {
                auto* fg = ImGui::GetForegroundDrawList();
                const auto &cam = _context.getCamera();
                for (const auto &[worldPos, worldSize] : chunk_manager->getLoadedChunkBounds())
                {
                    glm::vec2 tlL = cam.worldToScreen(worldPos);
                    glm::vec2 brL = cam.worldToScreen(worldPos + worldSize);
                    ImVec2 sTL    = logicalToImGuiScreen(_context, tlL);
                    ImVec2 sBR    = logicalToImGuiScreen(_context, brL);
                    fg->AddRect(sTL, sBR, IM_COL32(255, 50, 50, 200), 0.0f, 0, 1.8f);
                    // 左上角显示 chunk 坐标
                    char label[24];
                    int cx = static_cast<int>(worldPos.x / worldSize.x);
                    int cy = static_cast<int>(worldPos.y / worldSize.y);
                    snprintf(label, sizeof(label), "C%d,%d", cx, cy);
                    fg->AddText(ImVec2(sTL.x + 3.0f, sTL.y + 3.0f),
                                IM_COL32(255, 120, 120, 220), label);
                }
            }

            // ── DNF 瓦片悬停魔法光圈（脉动金色 L 形角标 + 外发光）──
            if (m_hasHoveredTile && m_devMode && chunk_manager)
            {
                auto* fg = ImGui::GetForegroundDrawList();
                const auto& cam  = _context.getCamera();
                const auto& ts   = chunk_manager->getTileSize();
                glm::vec2 twp    = chunk_manager->tileToWorld(m_hoveredTile);
                glm::vec2 tlL    = cam.worldToScreen(twp);
                glm::vec2 brL    = cam.worldToScreen(twp + glm::vec2(ts.x, ts.y));
                ImVec2 sTL       = logicalToImGuiScreen(_context, tlL);
                ImVec2 sBR       = logicalToImGuiScreen(_context, brL);

                const float T    = static_cast<float>(ImGui::GetTime());
                const float pulse = 0.5f + 0.5f * std::sin(T * 5.0f);
                const float w_px = sBR.x - sTL.x;
                const float h_px = sBR.y - sTL.y;
                const float cLen = std::min(w_px, h_px) * 0.38f;  // L 形角标长度

                // 外发光圆角矩形（两层，产生晕染感）
                fg->AddRect(ImVec2(sTL.x - 4, sTL.y - 4), ImVec2(sBR.x + 4, sBR.y + 4),
                    IM_COL32(255, 200, 60, static_cast<int>(35 + 28 * pulse)), 3.0f, 0, 4.0f);
                fg->AddRect(ImVec2(sTL.x - 2, sTL.y - 2), ImVec2(sBR.x + 2, sBR.y + 2),
                    IM_COL32(255, 220, 100, static_cast<int>(55 + 45 * pulse)), 2.0f, 0, 2.5f);

                // 主边框
                fg->AddRect(sTL, sBR,
                    IM_COL32(255, 235, 80, static_cast<int>(130 + 100 * pulse)), 0.0f, 0, 1.5f);

                // 四角 L 形标记（DNF 选框特色）
                const float lw = 2.2f;
                const ImU32 cc = IM_COL32(255, 248, 140, static_cast<int>(190 + 65 * pulse));
                // 左上
                fg->AddLine({sTL.x, sTL.y}, {sTL.x + cLen, sTL.y}, cc, lw);
                fg->AddLine({sTL.x, sTL.y}, {sTL.x, sTL.y + cLen}, cc, lw);
                // 右上
                fg->AddLine({sBR.x, sTL.y}, {sBR.x - cLen, sTL.y}, cc, lw);
                fg->AddLine({sBR.x, sTL.y}, {sBR.x, sTL.y + cLen}, cc, lw);
                // 左下
                fg->AddLine({sTL.x, sBR.y}, {sTL.x + cLen, sBR.y}, cc, lw);
                fg->AddLine({sTL.x, sBR.y}, {sTL.x, sBR.y - cLen}, cc, lw);
                // 右下
                fg->AddLine({sBR.x, sBR.y}, {sBR.x - cLen, sBR.y}, cc, lw);
                fg->AddLine({sBR.x, sBR.y}, {sBR.x, sBR.y - cLen}, cc, lw);

                // 中心旋转菱形指示器
                const float cx   = (sTL.x + sBR.x) * 0.5f;
                const float cy   = (sTL.y + sBR.y) * 0.5f;
                const float dmR  = std::min(w_px, h_px) * 0.16f;
                const float ang  = T * 1.6f;
                ImVec2 dm[4];
                for (int di = 0; di < 4; ++di)
                {
                    float a = ang + di * 1.5707963f;
                    dm[di]  = {cx + dmR * std::cos(a), cy + dmR * std::sin(a)};
                }
                fg->AddQuad(dm[0], dm[1], dm[2], dm[3],
                    IM_COL32(255, 240, 100, static_cast<int>(100 + 80 * pulse)), 1.2f);
            }

            pruneGroundSelection();

            if (!m_groundSelection.empty())
            {
                auto* fg = ImGui::GetForegroundDrawList();
                const auto& cam = _context.getCamera();
                for (const auto* actor : m_groundSelection)
                {
                    if (!isGroundActor(actor) || actor->isNeedRemove())
                        continue;

                    const auto* transform = actor->getComponent<engine::component::TransformComponent>();
                    const auto* sprite = actor->getComponent<engine::component::SpriteComponent>();
                    if (!transform || !sprite)
                        continue;

                    const glm::vec2 scale = transform->getScale();
                    const glm::vec2 spriteSize = sprite->getSpriteSize();
                    const glm::vec2 half = {
                        std::max(10.0f, spriteSize.x * std::abs(scale.x) * 0.5f),
                        std::max(10.0f, spriteSize.y * std::abs(scale.y) * 0.5f)
                    };
                    const glm::vec2 center = transform->getPosition();
                    const ImVec2 sTL = logicalToImGuiScreen(_context, cam.worldToScreen({center.x - half.x, center.y - half.y}));
                    const ImVec2 sBR = logicalToImGuiScreen(_context, cam.worldToScreen({center.x + half.x, center.y + half.y}));
                    fg->AddRect(sTL, sBR, IM_COL32(80, 220, 255, 220), 3.0f, 0, 2.0f);
                }
            }

            if (m_groundMakerPlaceMode && m_groundMakerDragPlacing)
            {
                auto* fg = ImGui::GetForegroundDrawList();
                const auto& cam = _context.getCamera();
                const glm::vec2 dragEnd = snapGroundMakerPosition(m_lastHoveredTileCenter);
                const glm::vec2 center = snapGroundMakerPosition((m_groundMakerDragStartWorld + dragEnd) * 0.5f);
                const float widthPx = groundMakerWidthFromCells();
                const float halfWidth = widthPx * 0.5f;
                const float halfHeight = std::max(8.0f, m_groundMakerBodyHalfPx.y * m_groundMakerScale.y);
                const float angleRad = m_groundMakerRotation * 0.01745329252f;
                const float cosA = std::cos(angleRad);
                const float sinA = std::sin(angleRad);
                const glm::vec2 corners[4] = {
                    {-halfWidth, -halfHeight},
                    { halfWidth, -halfHeight},
                    { halfWidth,  halfHeight},
                    {-halfWidth,  halfHeight}
                };
                ImVec2 quad[4];
                float minX = std::numeric_limits<float>::max();
                float minY = std::numeric_limits<float>::max();
                for (int i = 0; i < 4; ++i)
                {
                    const glm::vec2 rotated = {
                        corners[i].x * cosA - corners[i].y * sinA,
                        corners[i].x * sinA + corners[i].y * cosA
                    };
                    quad[i] = logicalToImGuiScreen(_context, cam.worldToScreen(center + rotated));
                    minX = std::min(minX, quad[i].x);
                    minY = std::min(minY, quad[i].y);
                }

                fg->AddConvexPolyFilled(quad, 4, IM_COL32(255, 220, 90, 34));
                fg->AddPolyline(quad, 4, IM_COL32(255, 220, 90, 220), ImDrawFlags_Closed, 2.0f);
                fg->AddText(ImVec2(minX, minY - 18.0f), IM_COL32(255, 235, 150, 230), "地面放置预览");
            }

            if (m_groundBoxSelecting)
            {
                auto* fg = ImGui::GetForegroundDrawList();
                const auto& cam = _context.getCamera();
                const glm::vec2 minWorld = {
                    std::min(m_groundBoxSelectStartWorld.x, m_groundBoxSelectEndWorld.x),
                    std::min(m_groundBoxSelectStartWorld.y, m_groundBoxSelectEndWorld.y)
                };
                const glm::vec2 maxWorld = {
                    std::max(m_groundBoxSelectStartWorld.x, m_groundBoxSelectEndWorld.x),
                    std::max(m_groundBoxSelectStartWorld.y, m_groundBoxSelectEndWorld.y)
                };
                const ImVec2 sTL = logicalToImGuiScreen(_context, cam.worldToScreen(minWorld));
                const ImVec2 sBR = logicalToImGuiScreen(_context, cam.worldToScreen(maxWorld));
                fg->AddRectFilled(sTL, sBR, IM_COL32(90, 180, 255, 26), 2.0f);
                fg->AddRect(sTL, sBR, IM_COL32(90, 180, 255, 220), 2.0f, 0, 1.6f);
            }

            if (m_groundMakerShowGrid && chunk_manager)
            {
                auto* fg = ImGui::GetForegroundDrawList();
                const auto& cam = _context.getCamera();
                const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
                const glm::vec2 worldMin = cam.screenToWorld({0.0f, 0.0f});
                const glm::vec2 worldMax = cam.screenToWorld({displaySize.x, displaySize.y});
                const float gridX = static_cast<float>(std::max(1, m_groundMakerGridSize.x));
                const float gridY = static_cast<float>(std::max(1, m_groundMakerGridSize.y));
                const float minX = std::min(worldMin.x, worldMax.x);
                const float maxX = std::max(worldMin.x, worldMax.x);
                const float minY = std::min(worldMin.y, worldMax.y);
                const float maxY = std::max(worldMin.y, worldMax.y);

                if (m_groundMakerSnapX)
                {
                    const float startX = std::floor(minX / gridX) * gridX;
                    for (float x = startX; x <= maxX + gridX; x += gridX)
                    {
                        const ImVec2 s0 = logicalToImGuiScreen(_context, cam.worldToScreen({x, minY}));
                        const ImVec2 s1 = logicalToImGuiScreen(_context, cam.worldToScreen({x, maxY}));
                        fg->AddLine(s0, s1, IM_COL32(120, 190, 255, 44), 1.0f);
                    }
                }

                if (m_groundMakerSnapY)
                {
                    const float startY = std::floor(minY / gridY) * gridY;
                    for (float y = startY; y <= maxY + gridY; y += gridY)
                    {
                        const ImVec2 s0 = logicalToImGuiScreen(_context, cam.worldToScreen({minX, y}));
                        const ImVec2 s1 = logicalToImGuiScreen(_context, cam.worldToScreen({maxX, y}));
                        fg->AddLine(s0, s1, IM_COL32(120, 190, 255, 44), 1.0f);
                    }
                }
            }

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
        });

        recordPerfMetric(m_frameProfiler.renderTotal,
                         elapsedMilliseconds(renderStart, SDL_GetPerformanceCounter(), perfFreq));
    }

    void GameScene::renderActorGroundShadows()
    {
        if (!actor_manager)
            return;

        const auto &camera = _context.getCamera();

        for (const auto &holder : actor_manager->getActors())
        {
            auto *actor = holder.get();
            if (!actor || actor->isNeedRemove())
                continue;

            auto *transform = actor->getComponent<engine::component::TransformComponent>();
            auto *sprite = actor->getComponent<engine::component::SpriteComponent>();
            if (!transform || !sprite || sprite->isHidden())
                continue;

            const std::string tag = actor->getTag();
            if (tag.rfind("bg_", 0) == 0 || tag == "sky" || tag == "parallax")
                continue;

            auto *ctrl = actor->getComponent<engine::component::ControllerComponent>();
            const float posZ = ctrl ? ctrl->getPosZ() : 0.0f;
            const glm::vec2 groundPos = transform->getPosition() + glm::vec2(0.0f, posZ);

            glm::vec2 spriteSize = sprite->getSpriteSize();
            if (spriteSize.x <= 0.0f || spriteSize.y <= 0.0f)
                spriteSize = glm::vec2{32.0f, 32.0f};

            glm::vec2 baseSize{
                std::clamp(spriteSize.x * 0.35f, 14.0f, 42.0f),
                std::clamp(spriteSize.y * 0.11f, 4.5f, 12.0f)
            };
            float baseAlpha = 0.16f;

            if (actor == m_mech)
            {
                baseSize = glm::vec2{38.0f, 11.0f};
                baseAlpha = 0.20f;
            }
            else if (tag == "monster")
            {
                baseAlpha = 0.15f;
            }

            const float zFactor = 1.0f / (1.0f + std::max(posZ, 0.0f) * 0.007f);
            const glm::vec2 size = baseSize * zFactor;
            const float alpha = baseAlpha * zFactor;
            const glm::vec2 shadowCenter = groundPos + glm::vec2(0.0f, std::max(10.0f, spriteSize.y * 0.12f));

            if (!camera.isBoxInView(shadowCenter - size * 0.5f, size))
                continue;

            drawWorldShadow(_context, shadowCenter, size, alpha);
        }
    }

    void GameScene::handleInput()
    {
        Scene::handleInput();

        auto &input = _context.getInputManager();

        {
            // F8: 地图编辑器开关
            const bool* keys = SDL_GetKeyboardState(nullptr);
            static bool s_mapEditorToggleWas = false;
            const bool toggleDown = keys && keys[SDL_SCANCODE_F8] != 0;
            if (toggleDown && !s_mapEditorToggleWas)
                m_showMapEditor = !m_showMapEditor;
            s_mapEditorToggleWas = toggleDown;

            // F5: Unity 风格 Play/Stop 切换
            static bool s_playToggleWas = false;
            const bool playToggleDown = keys && keys[SDL_SCANCODE_F5] != 0;
            if (playToggleDown && !s_playToggleWas)
                setGameplayRunning(!m_gameplayRunning);
            s_playToggleWas = playToggleDown;

            // F6: 运行态暂停/继续
            static bool s_pauseToggleWas = false;
            const bool pauseToggleDown = keys && keys[SDL_SCANCODE_F6] != 0;
            if (pauseToggleDown && !s_pauseToggleWas && m_gameplayRunning)
                m_gameplayPaused = !m_gameplayPaused;
            s_pauseToggleWas = pauseToggleDown;

            // F10: 暂停时单帧推进
            static bool s_stepToggleWas = false;
            const bool stepToggleDown = keys && keys[SDL_SCANCODE_F10] != 0;
            if (stepToggleDown && !s_stepToggleWas && m_gameplayRunning && m_gameplayPaused)
                m_stepOneFrame = true;
            s_stepToggleWas = stepToggleDown;
        }

        if (input.isActionPressed("command_mode"))
        {
            m_showCommandInput = !m_showCommandInput;
            m_focusCommandInput = m_showCommandInput;
            if (!m_showCommandInput)
                m_commandBuffer[0] = '\0';
        }

        if (m_showCommandInput)
            return;

        if (m_frameEditor.isOpen())
            return;

        if (m_showSettings)
            return;

        if (!m_gameplayRunning)
        {
            if (input.isActionPressed("open_inventory"))
                m_showInventory = !m_showInventory;
            if (input.isActionPressed("open_settings") || input.isActionPressed("pause"))
                m_showSettings = !m_showSettings;
            if (input.isActionPressed("open_map"))
                m_missionUI.showWindow = !m_missionUI.showWindow;
            return;
        }

        if (m_gameplayPaused)
        {
            if (input.isActionPressed("open_inventory"))
                m_showInventory = !m_showInventory;
            if (input.isActionPressed("open_settings") || input.isActionPressed("pause"))
                m_showSettings = !m_showSettings;
            if (input.isActionPressed("open_map"))
                m_missionUI.showWindow = !m_missionUI.showWindow;
            return;
        }

        if (actor_manager)
            actor_manager->handleInput();

        if (input.isActionPressed("possess"))
        {
            if (m_possessedMonster)
                releasePossessedMonster(false);
            else
                tryPossessNearestMonster();
        }

        {
            auto* ctrl = m_player
                ? m_player->getComponent<engine::component::ControllerComponent>()
                : nullptr;
            if (ctrl)
            {
                bool leftPressed  = input.isActionPressed("move_left");
                bool rightPressed = input.isActionPressed("move_right");
                bool leftDown     = input.isActionDown("move_left");
                bool rightDown    = input.isActionDown("move_right");

                if (!leftDown && !rightDown)
                    ctrl->setRunMode(false);

                if (leftPressed || rightPressed)
                {
                    int pressDir = rightPressed ? 1 : -1;
                    if (m_doubleTapTimer > 0.0f && m_doubleTapLastDir == pressDir)
                    {
                        ctrl->setRunMode(true);
                        m_doubleTapTimer = 0.0f;
                    }
                    else
                    {
                        m_doubleTapTimer   = 0.20f;
                        m_doubleTapLastDir = pressDir;
                    }
                }
            }
        }

        glm::vec2 mousePos = input.getLogicalMousePosition();
        glm::vec2 worldPos = _context.getCamera().screenToWorld(mousePos);
        m_lastMouseLogicalPos = mousePos;
        m_lastMouseWorldPos = worldPos;
        m_hoveredTile = chunk_manager->worldToTile(worldPos);
        m_hasHoveredTile = true;
        glm::vec2 hoveredTileCenter = chunk_manager->tileToWorld(m_hoveredTile)
            + glm::vec2(chunk_manager->getTileSize()) * 0.5f;
        m_lastHoveredTileCenter = hoveredTileCenter;

        const Uint32 mouseButtons = SDL_GetMouseState(nullptr, nullptr);
        const bool leftMouseDown = (mouseButtons & SDL_BUTTON_LMASK) != 0;
        const bool rightMouseDown = (mouseButtons & SDL_BUTTON_RMASK) != 0;
        const bool* keys = SDL_GetKeyboardState(nullptr);
        const bool shiftDown = (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) != 0;

        if (!m_gameplayRunning && !m_showMapEditor && !m_groundMakerPlaceMode && !ImGui::GetIO().WantCaptureMouse)
        {
            if (shiftDown && leftMouseDown && !m_groundBoxSelecting)
            {
                m_groundBoxSelecting = true;
                m_groundBoxSelectStartWorld = worldPos;
                m_groundBoxSelectEndWorld = worldPos;
            }
            else if (m_groundBoxSelecting && leftMouseDown)
            {
                m_groundBoxSelectEndWorld = worldPos;
            }
            else if (m_groundBoxSelecting && !leftMouseDown)
            {
                m_groundBoxSelecting = false;
                m_groundBoxSelectEndWorld = worldPos;
                m_groundSelection.clear();
                m_selectedActorIndex = -1;

                const glm::vec4 selectRect = {
                    std::min(m_groundBoxSelectStartWorld.x, m_groundBoxSelectEndWorld.x),
                    std::min(m_groundBoxSelectStartWorld.y, m_groundBoxSelectEndWorld.y),
                    std::max(m_groundBoxSelectStartWorld.x, m_groundBoxSelectEndWorld.x),
                    std::max(m_groundBoxSelectStartWorld.y, m_groundBoxSelectEndWorld.y)
                };

                for (int index = 0; actor_manager && index < static_cast<int>(actor_manager->getActors().size()); ++index)
                {
                    const auto* actor = actor_manager->getActors()[static_cast<size_t>(index)].get();
                    if (!isGroundActor(actor) || actor->isNeedRemove())
                        continue;

                    const auto* transform = actor->getComponent<engine::component::TransformComponent>();
                    const auto* sprite = actor->getComponent<engine::component::SpriteComponent>();
                    if (!transform || !sprite)
                        continue;

                    const glm::vec2 scale = transform->getScale();
                    const glm::vec2 spriteSize = sprite->getSpriteSize();
                    const glm::vec2 half = {
                        std::max(10.0f, spriteSize.x * std::abs(scale.x) * 0.5f),
                        std::max(10.0f, spriteSize.y * std::abs(scale.y) * 0.5f)
                    };
                    const glm::vec2 center = transform->getPosition();
                    const glm::vec4 actorRect = {
                        center.x - half.x,
                        center.y - half.y,
                        center.x + half.x,
                        center.y + half.y
                    };
                    if (!rectIntersects(selectRect, actorRect))
                        continue;

                    m_groundSelection.insert(actor);
                    if (m_selectedActorIndex < 0)
                        m_selectedActorIndex = index;
                }

                if (m_groundSelection.empty())
                    m_selectedActorIndex = -1;
            }
        }
        else if (!leftMouseDown)
        {
            m_groundBoxSelecting = false;
        }

        if (m_groundMakerPlaceMode && !m_showMapEditor)
        {
            const glm::vec2 snappedHoveredCenter = snapGroundMakerPosition(hoveredTileCenter);
            if (rightMouseDown && !m_groundMakerRightMouseWasDown)
            {
                m_groundMakerDragPlacing = true;
                m_groundMakerDragStartWorld = snappedHoveredCenter;
                m_groundMakerSpawnPos = snappedHoveredCenter;
            }
            else if (!rightMouseDown && m_groundMakerRightMouseWasDown && m_groundMakerDragPlacing)
            {
                const glm::vec2 dragEnd = snappedHoveredCenter;
                const glm::vec2 delta = dragEnd - m_groundMakerDragStartWorld;
                const float gridX = static_cast<float>(std::max(1, m_groundMakerGridSize.x));
                const float baseWidthPx = m_groundMakerUseGridSnap ? gridX : 32.0f;
                const float widthPx = snapGroundMakerWidth(std::max(baseWidthPx, std::abs(delta.x) + baseWidthPx));
                m_groundMakerLengthCells = std::max(1, static_cast<int>(std::round(widthPx / gridX)));
                m_groundMakerSpawnPos = snapGroundMakerPosition((m_groundMakerDragStartWorld + dragEnd) * 0.5f);
                m_groundMakerBodyHalfPx.x = std::max(8.0f, widthPx * 0.5f);
                createGroundActor();
                m_groundMakerDragPlacing = false;
            }
        }
        else
        {
            m_groundMakerDragPlacing = false;
        }
        m_groundMakerRightMouseWasDown = rightMouseDown;

        if (m_showMapEditor)
        {
            const bool paintDown = (mouseButtons & SDL_BUTTON_LMASK) != 0;
            const bool eraseDown = (mouseButtons & SDL_BUTTON_RMASK) != 0;

            if (paintDown || eraseDown)
            {
                const engine::world::TileType paintType = eraseDown
                    ? engine::world::TileType::Air
                    : m_mapEditorPaintTile;
                const int r = std::max(0, m_mapEditorBrushRadius);
                for (int dy = -r; dy <= r; ++dy)
                {
                    for (int dx = -r; dx <= r; ++dx)
                    {
                        if (dx * dx + dy * dy > r * r)
                            continue;
                        const int tx = m_hoveredTile.x + dx;
                        const int ty = m_hoveredTile.y + dy;
                        chunk_manager->setTileSilent(tx, ty, engine::world::TileData(paintType));
                    }
                }
                chunk_manager->rebuildDirtyChunks(4);
            }

            // 编辑模式下屏蔽战斗输入，避免左键同时攻击。
            return;
        }

        if (m_possessedMonster && input.isActionPressed("attack"))
            performPossessedMonsterAttack();

        // ── DNF 卷轴战斗：K 键近战攻击（方向由角色朝向决定，不依赖鼠标）──
        if (!m_possessedMonster && input.isActionPressed("attack"))
        {
            if (m_isPlayerInMech)
            {
                performMechAttack();
            }
            else
            {
                const auto &activeSlot = m_weaponBar.getActiveSlot();
                const auto *weaponDef = (!activeSlot.isEmpty() && activeSlot.item)
                    ? game::weapon::getWeaponDef(activeSlot.item->id)
                    : nullptr;

                if (weaponDef && weaponDef->attack_type == game::weapon::AttackType::Melee)
                {
                    glm::vec2 playerPos = getActorWorldPosition(m_player);
                    auto* ctrl = m_player
                        ? m_player->getComponent<engine::component::ControllerComponent>()
                        : nullptr;
                    float facing = (!ctrl || ctrl->getFacingDirection() ==
                        engine::component::ControllerComponent::FacingDirection::Right)
                        ? 1.0f : -1.0f;
                    glm::vec2 attackTarget = playerPos + glm::vec2(40.0f * facing, -8.0f);

                    // 连击计数：在窗口内连续按键递进段数
                    if (m_comboResetTimer > 0.0f)
                        m_comboCount = std::min(m_comboCount + 1, 2);
                    else
                        m_comboCount = 0;
                    m_comboResetTimer = 0.40f;

                    performMeleeAttack(attackTarget);

                    // ── Gundam 连招动画驱动 ──────────────────────────────
                    if (auto* anim = m_player
                            ? m_player->getComponent<engine::component::AnimationComponent>()
                            : nullptr)
                    {
                        bool inAttack = (m_attackAnimTimer > 0.0f);
                        if (!inAttack)
                        {
                            // 开始新连招序列
                            m_attackComboStep   = 0;
                            m_attackQueuedCount = 0;
                            m_attackAnimTimer   = kAttackADur;
                            anim->forcePlay("attack_a");
                        }
                        else
                        {
                            // 攻击中再按：缓存后续连招段，支持快速三连按键
                            m_attackQueuedCount = std::min(m_attackQueuedCount + 1, 2);
                        }
                    }

                    // 攻击位移冲量
                    if (m_player)
                    {
                        auto* physics = m_player->getComponent<engine::component::PhysicsComponent>();
                        if (physics)
                        {
                            glm::vec2 avel = physics->getVelocity();
                            float impulse = (m_comboCount == 0) ? 5.0f
                                          : (m_comboCount == 1) ? 7.0f : 4.0f;
                            avel.x = facing * impulse;
                            physics->setVelocity(avel);
                        }
                    }
                }
            }
        }

        // ── DNF 冲刺：Shift 键触发（冲刺方向 = 当前移动方向或朝向）──────
        if (!m_isPlayerInMech && !m_possessedMonster)
        {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            bool shiftNow = (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) != 0;
            if (shiftNow && !m_dashKeyWas && m_dashCooldown <= 0.0f && !m_isDashing)
            {
                // 确定冲刺方向：A/D 键决定；都没按则用角色朝向
                bool leftDown  = (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  != 0;
                bool rightDown = (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) != 0;
                if (rightDown)
                    m_dashFacing = 1.0f;
                else if (leftDown)
                    m_dashFacing = -1.0f;
                else
                {
                    auto* ctrl = m_player
                        ? m_player->getComponent<engine::component::ControllerComponent>()
                        : nullptr;
                    m_dashFacing = (!ctrl || ctrl->getFacingDirection() ==
                        engine::component::ControllerComponent::FacingDirection::Right)
                        ? 1.0f : -1.0f;
                }
                m_isDashing  = true;
                m_dashTimer  = 0.18f;     // 冲刺持续 0.18s
                m_dashCooldown = 0.65f;   // 冷却 0.65s
            }
            m_dashKeyWas = shiftNow;
        }
        else
        {
            m_dashKeyWas = false;
        }


        // E键切换背包
        if (input.isActionPressed("open_inventory"))
            m_showInventory = !m_showInventory;

        // ~键切换设置页面
        if (input.isActionPressed("open_settings"))
            m_showSettings = !m_showSettings;

        // ESC / P键切换设置页面
        if (input.isActionPressed("pause"))
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

        // Q键：主动展光冲刺星技 + Gundam 炮击动画
        if (m_possessedMonster && input.isActionPressed("skill_use"))
            performPossessedMonsterSkill();

        if (!m_possessedMonster && input.isActionPressed("skill_use"))
        {
            triggerActiveStarSkills();
            // Gundam 炮击动画（仅非攻击动画期间触发）
            if (m_player && m_attackAnimTimer <= 0.0f && m_cannonTimer <= 0.0f
                && m_ultimateTimer <= 0.0f)
            {
                if (auto* anim = m_player->getComponent<engine::component::AnimationComponent>())
                {
                    anim->forcePlay("cannon");
                    m_cannonTimer = kCannonDur;
                }
            }
        }

        // Ctrl/X键：Gundam 大招动画 (重击)
        if (!m_possessedMonster && !m_isPlayerInMech && m_player)
        {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            bool heavyDown = (keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_LCTRL]) != 0;
            // 边沿检测：只在按下首帧触发
            static bool s_heavyWas = false;
            if (heavyDown && !s_heavyWas && m_attackAnimTimer <= 0.0f
                && m_heavyAttackTimer <= 0.0f && m_ultimateTimer <= 0.0f)
            {
                if (auto* anim = m_player->getComponent<engine::component::AnimationComponent>())
                {
                    anim->forcePlay("attack_d");
                    m_heavyAttackTimer = kAttackDDur;
                }
            }
            s_heavyWas = heavyDown;
        }

        // R键：Gundam 大招剑气动画
        if (!m_possessedMonster && !m_isPlayerInMech && m_player)
        {
            const bool* keys2 = SDL_GetKeyboardState(nullptr);
            static bool s_ultimateWas = false;
            bool ultiDown = keys2[SDL_SCANCODE_R] != 0;
            if (ultiDown && !s_ultimateWas && m_ultimateTimer <= 0.0f
                && m_attackAnimTimer <= 0.0f)
            {
                if (auto* anim = m_player->getComponent<engine::component::AnimationComponent>())
                {
                    anim->forcePlay("ultimate");
                    m_ultimateTimer = kUltimateDur;
                }
            }
            s_ultimateWas = ultiDown;
        }

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

        shutdownFlightAmbientSound();

        saveBoolSetting("show_editor_toolbar", m_showEditorToolbar);
        saveBoolSetting("show_hierarchy_panel", m_showHierarchyPanel);
        saveBoolSetting("show_inspector_panel", m_showInspectorPanel);
        saveBoolSetting("toolbar_show_play_controls", m_toolbarShowPlayControls);
        saveBoolSetting("toolbar_show_window_controls", m_toolbarShowWindowControls);
        saveBoolSetting("toolbar_show_debug_controls", m_toolbarShowDebugControls);
        saveBoolSetting("hierarchy_group_by_tag", m_hierarchyGroupByTag);
        saveBoolSetting("hierarchy_favorites_only", m_hierarchyFavoritesOnly);
        saveBoolSetting("enable_play_rollback", m_enablePlayRollback);

        if (m_glContext)
        {
            ImGui::SaveIniSettingsToDisk("imgui.ini");
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

    void GameScene::generateRockObstacles()
    {
        if (!actor_manager || !physics_manager || !chunk_manager || !m_routeData.isValid())
            return;

        constexpr float kPixelsPerMeter = engine::world::WorldConfig::PIXELS_PER_METER;
        constexpr int kSectionWidth = game::route::RouteData::TILES_PER_CELL;
        constexpr float kRoomMinY = 28.0f;
        constexpr float kRoomMaxY = 76.0f;

        const std::array<const char *, 3> rockTextures = {
            "assets/textures/Props/rock.png",
            "assets/textures/Props/rock-1.png",
            "assets/textures/Props/rock-2.png"
        };
        // 2.5D 脚部矩形：halfX 保持精灵宽度，halfY 统一压缩为 ~4px 薄片（深度碰撞域）
        const std::array<glm::vec2, 3> bodyHalfSizesPx = {
            glm::vec2{13.0f, 4.0f},
            glm::vec2{10.0f, 4.0f},
            glm::vec2{15.0f, 4.0f}
        };
        const std::array<glm::vec2, 3> spriteScales = {
            glm::vec2{0.92f, 0.92f},
            glm::vec2{0.82f, 0.82f},
            glm::vec2{1.04f, 1.04f}
        };

        auto mixSeed = [](uint64_t value) -> uint64_t {
            value ^= value >> 33;
            value *= 0xff51afd7ed558ccdULL;
            value ^= value >> 33;
            value *= 0xc4ceb9fe1a85ec53ULL;
            value ^= value >> 33;
            return value;
        };

        auto obstacleCountForTerrain = [](game::route::CellTerrain terrain) -> int {
            switch (terrain)
            {
            case game::route::CellTerrain::Plains: return 1;
            case game::route::CellTerrain::Forest: return 1;
            case game::route::CellTerrain::Rocky: return 3;
            case game::route::CellTerrain::Mountain: return 2;
            case game::route::CellTerrain::Cave: return 2;
            }
            return 1;
        };

        int spawned = 0;
        for (int zone = 0; zone < static_cast<int>(m_routeData.path.size()); ++zone)
        {
            if (zone == 0)
                continue;

            const glm::ivec2 cell = m_routeData.path[zone];
            const game::route::CellTerrain terrain = m_routeData.terrain[cell.y][cell.x];
            const int obstacleCount = obstacleCountForTerrain(terrain);
            for (int obstacleIdx = 0; obstacleIdx < obstacleCount; ++obstacleIdx)
            {
                const uint64_t seed = mixSeed(
                    static_cast<uint64_t>(m_routeData.planetSeed) +
                    static_cast<uint64_t>((zone + 1) * 131) +
                    static_cast<uint64_t>((obstacleIdx + 3) * 977));

                const int variant = static_cast<int>(seed % rockTextures.size());
                // 在 section 内按 obstacleCount 等间距放置（像素坐标）
                const float sectionPx  = static_cast<float>(kSectionWidth * 16); // 1600 px/section
                const float slotW      = sectionPx / static_cast<float>(obstacleCount + 1);
                const float jitterX    = static_cast<float>((seed >> 8) % 41) - 20.0f;  // ±20 px
                const float jitterY    = static_cast<float>((seed >> 20) % 17) - 8.0f;  // ±8 px
                const float worldX     = static_cast<float>(zone * kSectionWidth * 16)
                                         + slotW * static_cast<float>(obstacleIdx + 1)
                                         + jitterX;
                const float worldY = std::clamp(54.0f + jitterY, kRoomMinY, kRoomMaxY);

                const int tileX = static_cast<int>(worldX / 16.0f);

                // DNF 模式地板全为 GroundDecor，worldY 已 clamp 至 [28,76] 故必在走廊内。
                // 只需跳过每 section 末尾 2 格隔墙区，不依赖 chunk 是否已加载。
                const int posInSection = ((tileX % kSectionWidth) + kSectionWidth) % kSectionWidth;
                if (posInSection >= kSectionWidth - 2)
                    continue;

                auto *rock = actor_manager->createActor(std::string{"rock_obstacle_"} + std::to_string(zone) + "_" + std::to_string(obstacleIdx));
                rock->setTag("rock_obstacle");
                rock->addComponent<engine::component::TransformComponent>(
                    glm::vec2{worldX, worldY}, spriteScales[variant]);
                rock->addComponent<engine::component::SpriteComponent>(
                    rockTextures[variant], engine::utils::Alignment::CENTER);

                const glm::vec2 bodyHalfMeters = bodyHalfSizesPx[variant] / kPixelsPerMeter;
                b2BodyId bodyId = physics_manager->createStaticBody(
                    {worldX / kPixelsPerMeter, worldY / kPixelsPerMeter},
                    {bodyHalfMeters.x, bodyHalfMeters.y},
                    rock);
                rock->addComponent<engine::component::PhysicsComponent>(bodyId, physics_manager.get());
                ++spawned;
            }
        }

        spdlog::info("背景障碍石块生成完毕：{} 个", spawned);
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
        auto* sprite     = m_player->getComponent<engine::component::SpriteComponent>();
        auto* anim       = m_player->getComponent<engine::component::AnimationComponent>();
        if (!controller || !sprite) return;

        bool shouldFlip = controller->getFacingDirection() == engine::component::ControllerComponent::FacingDirection::Left;
        if (m_invertPlayerFacing)
            shouldFlip = !shouldFlip;
        if (sprite->isFlipped() != shouldFlip)
            sprite->setFlipped(shouldFlip);

        // ── Gundam 攻击动画优先级：攻击/技能动画期间不切换移动动画 ──────
        // 状态机加载时，动画切换交给 tickPlayerSM()，但后续视觉位移逻辑仍需执行。
        if (anim && !m_playerSMLoaded)
        {
            // 倒计时减少（由 syncPlayerPresentation 每帧统一处理）
            // 注意：delta_time 在 update() 里，此处通过 ImGui framerate 近似
            // 实际已由 update() 循环在调用前更新 delta_time，此处直接使用 m_attackAnimTimer
            // m_attackAnimTimer 的递减由 update() 内 tickCombatEffects 之前处理

            bool inAttackAnim = (m_attackAnimTimer > 0.0f)
                             || (m_heavyAttackTimer > 0.0f)
                             || (m_ultimateTimer    > 0.0f)
                             || (m_cannonTimer      > 0.0f);

            // 攻击动画未结束时不覆盖
            if (!inAttackAnim)
            {
                // 移动动画：将 ControllerComponent 状态映射到 gundam 动画 key
                const std::string moveKey = controller->getAnimationStateKey();
                // ControllerComponent 返回 "run"，映射到 gundam 的 "run"；其余同名
                anim->play(moveKey);
            }
            else if (anim->isFinished())
            {
                // 如果攻击期间有连招排队输入，立即触发下一段
                if (m_attackQueuedCount > 0)
                {
                    --m_attackQueuedCount;
                    m_attackComboStep = (m_attackComboStep + 1) % 3;
                    const char* nextClips[] = {"attack_a", "attack_b", "attack_c"};
                    const float nextDurs[]  = {kAttackADur, kAttackBDur, kAttackCDur};
                    anim->forcePlay(nextClips[m_attackComboStep]);
                    // 重置辅助计时器，设置新段时长
                    m_attackAnimTimer   = nextDurs[m_attackComboStep];
                    m_heavyAttackTimer  = 0.0f;
                    m_ultimateTimer     = 0.0f;
                    m_cannonTimer       = 0.0f;
                }
                // 无连招输入：等 m_attackAnimTimer 自然倒计至 0 后由 !inAttackAnim 分支切回 idle
                // （让最后一帧完整展示其 duration，而非提前截断）
            }
        }

        // ── DNF Z轴视觉偏移：将精灵沿屏幕 Y 上移 posZ 像素 ──
        auto* transform = m_player->getComponent<engine::component::TransformComponent>();
        if (transform)
        {
            float posZ = controller->getPosZ();
            glm::vec2 pos = transform->getPosition();
            // 每帧先把上帧 Z 偏移还原（物理系统已将 Y 重置回地面），再施加当前 Z
            // 注意：PhysicsComponent::update() 在 actor_manager->update() 同帧已将 Y 设为 Box2D 值
            if (posZ > 0.0f)
                pos.y -= posZ;
            transform->setPosition(pos);

            // ── Y轴透视缩放：走廊深度越深→角色越大（视差感）──
            // 以走廊中心 Y=56 为基准：Y<56 (场景后方) 角色偏小，Y>56(场景前方) 角色偏大
            constexpr float kBaseY   = 56.0f;
            constexpr float kFactor  = 0.0008f;
            float perspScale = 1.0f + (pos.y - kBaseY) * kFactor;
            perspScale = std::clamp(perspScale, 0.92f, 1.08f);
            transform->setScale({perspScale, perspScale});
        }

        if (m_mech)
        {
            auto* mechController = m_mech->getComponent<engine::component::ControllerComponent>();
            auto* mechSprite = m_mech->getComponent<engine::component::SpriteComponent>();
            auto* mechAnim = m_mech->getComponent<engine::component::AnimationComponent>();
            auto* mechTransform = m_mech->getComponent<engine::component::TransformComponent>();
            if (mechController && mechSprite)
            {
                // 朝向翻转
                bool mechFlip = mechController->getFacingDirection() == engine::component::ControllerComponent::FacingDirection::Left;
                if (mechSprite->isFlipped() != mechFlip)
                    mechSprite->setFlipped(mechFlip);

                if (mechAnim)
                {
                    const std::string mechMoveKey = mechController->getAnimationStateKey();
                    mechAnim->play(mechMoveKey);
                }

                if (mechTransform)
                {
                    float posZ = mechController->getPosZ();
                    glm::vec2 pos = mechTransform->getPosition();
                    if (posZ > 0.0f)
                        pos.y -= posZ;
                    mechTransform->setPosition(pos);

                    constexpr float kBaseY   = 56.0f;
                    constexpr float kFactor  = 0.0008f;
                    float perspScale = 1.0f + (pos.y - kBaseY) * kFactor;
                    perspScale = std::clamp(perspScale, 0.92f, 1.08f);
                    mechTransform->setScale({perspScale, perspScale});
                }
            }
        }

        // 属性系统 → 控制器参数同步（每帧，保证淨畫 buff 立即生效）
        if (auto* attr = m_player->getComponent<game::component::AttributeComponent>())
        {
            if (auto* ctrl = m_player->getComponent<engine::component::ControllerComponent>())
            {
                constexpr float BASE_SPEED = 12.0f;
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
        ImGui::TextUnformatted("输入 001 或 r001 呼叫空投机甲。再次按 R 可退出指令模式。");
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

        ImGui::TextDisabled("当前可用指令: 001 / r001");
        ImGui::End();
    }

    void GameScene::updateSettingsParticles(float dt)
    {
        // FPS 历史采样：每 0.25s 记录一次实时帧率
        m_fpsHistoryTimer += dt;
        if (m_fpsHistoryTimer >= 0.25f)
        {
            m_fpsHistoryTimer = 0.0f;
            float fps = ImGui::GetIO().Framerate;
            m_fpsHistory[m_fpsHistoryIdx] = fps;
            m_fpsHistoryIdx = (m_fpsHistoryIdx + 1) % kMemHistoryLen;
            if (fps > m_fpsPeak) m_fpsPeak = fps;
        }

        // 内存历史采样：每 0.5s 记录一次 RSS
        m_rssHistoryTimer += dt;
        if (m_rssHistoryTimer >= 0.5f)
        {
            m_rssHistoryTimer = 0.0f;
            struct mach_task_basic_info ti;
            mach_msg_type_number_t cnt = MACH_TASK_BASIC_INFO_COUNT;
            float mb = 0.0f;
            if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                          reinterpret_cast<task_info_t>(&ti), &cnt) == KERN_SUCCESS)
                mb = static_cast<float>(ti.resident_size) / (1024.0f * 1024.0f);
            m_rssHistory[m_rssHistoryIdx] = mb;
            m_rssHistoryIdx = (m_rssHistoryIdx + 1) % kMemHistoryLen;
            if (mb > m_rssPeakMB) m_rssPeakMB = mb;
        }

        if (m_uiParticleLevel == UiParticleLevel::None) { m_uiDusts.clear(); return; }

        // 目标粒子数量
        int target = 0;
        switch (m_uiParticleLevel)
        {
            case UiParticleLevel::Low:    target = 18;  break;
            case UiParticleLevel::Medium: target = 50;  break;
            case UiParticleLevel::High:   target = 120; break;
            default: break;
        }

        // 生成不足时补充
        while (static_cast<int>(m_uiDusts.size()) < target)
        {
            UiDust d;
            d.x     = static_cast<float>(rand() % 1000) / 1000.0f;
            d.y     = static_cast<float>(rand() % 1000) / 1000.0f;
            d.vy    = 0.008f + static_cast<float>(rand() % 100) / 10000.0f;
            d.alpha = 0.15f + static_cast<float>(rand() % 50) / 200.0f;
            d.size  = 1.5f + static_cast<float>(rand() % 3);
            // 蓝紫能量色系（DNF 风格）
            int palette = rand() % 3;
            if (palette == 0) { d.r = 80;  d.g = 140; d.b = 255; }  // 蓝
            else if (palette == 1) { d.r = 160; d.g = 80;  d.b = 220; }  // 紫
            else               { d.r = 200; d.g = 220; d.b = 255; }  // 浅蓝
            m_uiDusts.push_back(d);
        }
        // 超出则截断
        if (static_cast<int>(m_uiDusts.size()) > target)
            m_uiDusts.resize(static_cast<size_t>(target));

        // 每帧更新粒子位置
        for (auto &d : m_uiDusts)
        {
            d.y -= d.vy * dt;           // 向上飘动
            d.x += (static_cast<float>(rand() % 200 - 100) / 100000.0f);  // 微小横向漂移
            if (d.y < -0.02f)           // 超出顶部则重生在底部
            {
                d.y = 1.02f;
                d.x = static_cast<float>(rand() % 1000) / 1000.0f;
            }
        }
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
        size_t chunkMemEst = chunkCount * (sizeof(engine::world::Chunk) + 16 * 16 * 6 * 4 * sizeof(float));
        size_t actorCount  = actor_manager ? actor_manager->actorCount() : 0;
        size_t actorMemEst = actorCount * 2048;
        size_t monsterCount  = m_monsterManager ? m_monsterManager->monsterCount() : 0;
        size_t monsterMemEst = monsterCount * 2048;
        size_t dropCount  = m_treeManager.getDrops().size();
        size_t dropMemEst = dropCount * 256;

        ImGui::SetNextWindowSize({600.0f, 560.0f}, ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(
            {ImGui::GetIO().DisplaySize.x * 0.5f - 300.0f,
             ImGui::GetIO().DisplaySize.y * 0.5f - 280.0f},
            ImGuiCond_Appearing);
        if (!ImGui::Begin("设置  [ESC / ` 关闭]", &m_showSettings))
        {
            ImGui::End();
            return;
        }

        // ── 右上角：打开帧编辑器 + 状态机编辑器 ──────────────────────────
        {
            ImGui::SameLine(ImGui::GetWindowWidth() - 280.0f);
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.45f, 0.85f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.58f, 1.00f, 1.00f));
            if (ImGui::Button("帧编辑器", ImVec2(130, 0)))
                m_frameEditor.open();
            ImGui::PopStyleColor(2);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.28f, 0.05f, 0.90f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.42f, 0.12f, 1.00f));
            if (ImGui::Button("状态机编辑器", ImVec2(130, 0)))
                m_smEditor.open();
            ImGui::PopStyleColor(2);
        }
        ImGui::Separator();
        if (ImGui::CollapsingHeader("字体试显", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const char *fontName = (ImGui::GetFont() && ImGui::GetFont()->GetDebugName() && ImGui::GetFont()->GetDebugName()[0] != '\0')
                ? ImGui::GetFont()->GetDebugName()
                : "默认字体";
            ImGui::Text("当前 ImGui 字体: %s", fontName);
            ImGui::TextDisabled("资源路径: assets/fonts/VonwaonBitmap-16px.ttf");
            ImGui::TextDisabled("当前字号: %.1f", ImGui::GetFontSize());
            ImGui::SeparatorText("地名试显");
            ImGui::TextUnformatted("白桦前哨站 / 旧港装卸区 / 晴雪岭观测点 / 南侧采掘平台 / 轨道四号库");
            ImGui::TextUnformatted("东三区补给线 / 地下二层机修间 / 北坡通信塔 / 晨雾农场 / 灰石避难所");
            ImGui::SeparatorText("单位试显");
            ImGui::TextUnformatted("距离 12 m / 240 m / 1.6 km / 23.5 km");
            ImGui::TextUnformatted("质量 3 kg / 85 kg / 1.2 t");
            ImGui::TextUnformatted("功率 600 W / 12 kW / 2.4 MW");
            ImGui::TextUnformatted("速度 4.5 m/s / 72 km/h / 0.82 Mach");
            ImGui::TextUnformatted("温度 -12 C / 23 C / 107 C");
            ImGui::SeparatorText("混排样本");
            ImGui::TextUnformatted("轨道四号库 -> 白桦前哨站 3.4 km, 预计 12 min, 载荷 1.8 t");
            ImGui::TextUnformatted("南侧采掘平台输出 2.4 MW, 冷却液 18 L/min, 压力 0.72 MPa");
        }
        if (m_uiParticleLevel != UiParticleLevel::None && !m_uiDusts.empty())
        {
            ImVec2 wMin = ImGui::GetWindowPos();
            ImVec2 wMax = { wMin.x + ImGui::GetWindowWidth(), wMin.y + ImGui::GetWindowHeight() };
            ImDrawList *dl = ImGui::GetWindowDrawList();
            for (const auto &d : m_uiDusts)
            {
                float px = wMin.x + d.x * (wMax.x - wMin.x);
                float py = wMin.y + d.y * (wMax.y - wMin.y);
                uint8_t a = static_cast<uint8_t>(d.alpha * 255.0f);
                dl->AddRectFilled(ImVec2(px, py), ImVec2(px + d.size, py + d.size),
                    IM_COL32(d.r, d.g, d.b, a));
            }
        }

        auto fmtMB = [](size_t bytes) -> float { return static_cast<float>(bytes) / (1024.0f * 1024.0f); };
        float rssMB = fmtMB(rss);

        // ── 内存折线图 ────────────────────────────────────────────────────
        ImGui::SeparatorText("内存使用");
        ImGui::Text("进程 RSS: %.1f MB   峰值: %.1f MB", rssMB, m_rssPeakMB);
        if (ImGui::Button("重置峰值")) m_rssPeakMB = rssMB;

        // 图表区域
        {
            const ImVec2 graphSize{ImGui::GetContentRegionAvail().x, 80.0f};
            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 p1 = { p0.x + graphSize.x, p0.y + graphSize.y };

            // 背景
            dl->AddRectFilled(p0, p1, IM_COL32(12, 18, 30, 220), 4.0f);
            dl->AddRect(p0, p1, IM_COL32(60, 100, 160, 180), 4.0f);

            // 确定 Y 轴最大值：RSS 峰值上取整到 16 MB 对齐
            float yMax = std::max(m_rssPeakMB * 1.15f, 32.0f);
            yMax = std::ceil(yMax / 16.0f) * 16.0f;

            // 水平网格线（4 条）
            for (int gi = 1; gi < 4; ++gi)
            {
                float gy = p0.y + graphSize.y * (1.0f - static_cast<float>(gi) / 4.0f);
                dl->AddLine(ImVec2(p0.x, gy), ImVec2(p1.x, gy), IM_COL32(50, 80, 120, 80));
                float label = yMax * static_cast<float>(gi) / 4.0f;
                char buf[16]; snprintf(buf, sizeof(buf), "%.0f", label);
                dl->AddText(ImVec2(p0.x + 3, gy - 10), IM_COL32(120, 160, 200, 160), buf);
            }

            // 折线
            int n = kMemHistoryLen;
            for (int i = 0; i < n - 1; ++i)
            {
                int ia = (m_rssHistoryIdx + i)     % n;
                int ib = (m_rssHistoryIdx + i + 1) % n;
                float va = m_rssHistory[ia] / yMax;
                float vb = m_rssHistory[ib] / yMax;
                float x0 = p0.x + graphSize.x * static_cast<float>(i)     / static_cast<float>(n - 1);
                float x1 = p0.x + graphSize.x * static_cast<float>(i + 1) / static_cast<float>(n - 1);
                float y0g = p1.y - graphSize.y * glm::clamp(va, 0.0f, 1.0f);
                float y1g = p1.y - graphSize.y * glm::clamp(vb, 0.0f, 1.0f);

                // 颜色随使用率变化：绿→黄→红
                float ratio = glm::clamp(vb, 0.0f, 1.0f);
                uint8_t cr = static_cast<uint8_t>(glm::mix(40.0f,  255.0f, ratio));
                uint8_t cg = static_cast<uint8_t>(glm::mix(220.0f, 80.0f,  ratio));
                dl->AddLine(ImVec2(x0, y0g), ImVec2(x1, y1g), IM_COL32(cr, cg, 60, 220), 1.5f);

                // 填充区域（半透明）
                float yFloor = p1.y;
                ImVec2 quad[4] = { {x0,y0g},{x1,y1g},{x1,yFloor},{x0,yFloor} };
                dl->AddConvexPolyFilled(quad, 4, IM_COL32(cr, cg, 60, 35));
            }

            // 当前点高亮圆点
            {
                int ic = (m_rssHistoryIdx + n - 1) % n;
                float vc = m_rssHistory[ic] / yMax;
                float xc = p1.x - 1.0f;
                float yc = p1.y - graphSize.y * glm::clamp(vc, 0.0f, 1.0f);
                dl->AddCircleFilled(ImVec2(xc, yc), 3.5f, IM_COL32(255, 240, 100, 220));
            }

            // "MB" y轴标签
            dl->AddText(ImVec2(p0.x + 3, p0.y + 2), IM_COL32(120, 160, 200, 180), "MB");

            ImGui::Dummy(graphSize);  // 占位推进光标
        }

        ImGui::Spacing();
        if (ImGui::BeginTable("##mem_table", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("模块",     ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("数量",     ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("估算内存", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableHeadersRow();

            auto row = [&](const char *name, size_t cnt, size_t mem) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%zu", cnt);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.2f MB", fmtMB(mem));
            };
            row("区块 (Chunk)",       chunkCount,   chunkMemEst);
            row("Actor",              actorCount,   actorMemEst);
            row("怪物 (Monster)",     monsterCount, monsterMemEst);
            row("掉落物 (Drop Item)", dropCount,    dropMemEst);
            ImGui::EndTable();
        }
        ImGui::TextDisabled("* 模块内存为估算值，总 RSS 为实际物理内存占用。");

        // ── FPS 折线图 ────────────────────────────────────────────────────
        ImGui::SeparatorText("帧率监控");
        ImGui::Text("当前 FPS: %.1f   峰值: %.1f", ImGui::GetIO().Framerate, m_fpsPeak);
        ImGui::SameLine();
        if (ImGui::Button("重置峰值##fps")) m_fpsPeak = ImGui::GetIO().Framerate;

        {
            const ImVec2 fpsGraphSize{ImGui::GetContentRegionAvail().x, 72.0f};
            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 p1 = { p0.x + fpsGraphSize.x, p0.y + fpsGraphSize.y };

            dl->AddRectFilled(p0, p1, IM_COL32(12, 18, 30, 220), 4.0f);
            dl->AddRect(p0, p1, IM_COL32(60, 100, 160, 180), 4.0f);

            int targetFps = _context.getTime().getTargetFPS();
            float yMax = (targetFps > 0) ? static_cast<float>(targetFps) * 1.1f
                                         : std::max(m_fpsPeak * 1.1f, 60.0f);
            yMax = std::ceil(yMax / 30.0f) * 30.0f;

            // 参考线：目标帧率
            if (targetFps > 0 && static_cast<float>(targetFps) <= yMax)
            {
                float gy = p1.y - fpsGraphSize.y * (static_cast<float>(targetFps) / yMax);
                dl->AddLine(ImVec2(p0.x, gy), ImVec2(p1.x, gy), IM_COL32(100, 200, 100, 100));
                char buf[16]; snprintf(buf, sizeof(buf), "%d", targetFps);
                dl->AddText(ImVec2(p0.x + 3, gy - 10), IM_COL32(100, 200, 100, 160), buf);
            }
            // 水平网格线
            for (int gi = 1; gi < 4; ++gi)
            {
                float gy = p0.y + fpsGraphSize.y * (1.0f - static_cast<float>(gi) / 4.0f);
                dl->AddLine(ImVec2(p0.x, gy), ImVec2(p1.x, gy), IM_COL32(50, 80, 120, 60));
            }

            int n = kMemHistoryLen;
            for (int i = 0; i < n - 1; ++i)
            {
                int ia = (m_fpsHistoryIdx + i)     % n;
                int ib = (m_fpsHistoryIdx + i + 1) % n;
                float va = m_fpsHistory[ia] / yMax;
                float vb = m_fpsHistory[ib] / yMax;
                float x0 = p0.x + fpsGraphSize.x * static_cast<float>(i)     / static_cast<float>(n - 1);
                float x1 = p0.x + fpsGraphSize.x * static_cast<float>(i + 1) / static_cast<float>(n - 1);
                float y0g = p1.y - fpsGraphSize.y * glm::clamp(va, 0.0f, 1.0f);
                float y1g = p1.y - fpsGraphSize.y * glm::clamp(vb, 0.0f, 1.0f);

                // 低于目标帧率时变红，否则绿色
                float ratio = (targetFps > 0)
                    ? glm::clamp(1.0f - vb * yMax / static_cast<float>(targetFps), 0.0f, 1.0f)
                    : 0.0f;
                uint8_t cr = static_cast<uint8_t>(glm::mix(60.0f,  255.0f, ratio));
                uint8_t cg = static_cast<uint8_t>(glm::mix(210.0f, 60.0f,  ratio));
                dl->AddLine(ImVec2(x0, y0g), ImVec2(x1, y1g), IM_COL32(cr, cg, 60, 220), 1.5f);

                ImVec2 quad[4] = { {x0,y0g},{x1,y1g},{x1,p1.y},{x0,p1.y} };
                dl->AddConvexPolyFilled(quad, 4, IM_COL32(cr, cg, 60, 28));
            }
            {
                int ic = (m_fpsHistoryIdx + n - 1) % n;
                float vc = m_fpsHistory[ic] / yMax;
                float xc = p1.x - 1.0f;
                float yc = p1.y - fpsGraphSize.y * glm::clamp(vc, 0.0f, 1.0f);
                dl->AddCircleFilled(ImVec2(xc, yc), 3.0f, IM_COL32(255, 240, 100, 220));
            }
            dl->AddText(ImVec2(p0.x + 3, p0.y + 2), IM_COL32(120, 160, 200, 180), "FPS");
            ImGui::Dummy(fpsGraphSize);
        }

        ImGui::Spacing();
        ImGui::SeparatorText("性能剖析");
        ImGui::Text("帧耗时: %.2fms  Update: %.2fms  Render: %.2fms",
            m_frameProfiler.frameDeltaMs,
            m_frameProfiler.updateTotal.lastMs,
            m_frameProfiler.renderTotal.lastMs);
        ImGui::Text("Chunk: 已加载 %zu  待加载 %zu",
            m_frameProfiler.loadedChunks,
            m_frameProfiler.pendingChunkLoads);

        if (ImGui::BeginTable("##perf_breakdown", 5,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp))
        {
            const float frameMs = std::max(m_frameProfiler.frameDeltaMs, 0.0001f);
            const float updateMs = std::max(m_frameProfiler.updateTotal.lastMs, 0.0001f);
            const float renderMs = std::max(m_frameProfiler.renderTotal.lastMs, 0.0001f);

            ImGui::TableSetupColumn("阶段");
            ImGui::TableSetupColumn("当前(ms)");
            ImGui::TableSetupColumn("均值(ms)");
            ImGui::TableSetupColumn("峰值(ms)");
            ImGui::TableSetupColumn("占比");
            ImGui::TableHeadersRow();

            auto perfRow = [&](const char* label, const PerfMetric& metric, float basisMs) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", metric.lastMs);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.2f", metric.avgMs);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.2f", metric.peakMs);
                ImGui::TableSetColumnIndex(4); ImGui::Text("%.1f%%", perfPercent(metric.lastMs, basisMs));
            };

            perfRow("Update 总计", m_frameProfiler.updateTotal, frameMs);
            perfRow("Update 核心逻辑", m_frameProfiler.coreLogic, updateMs);
            perfRow("Update 怪物", m_frameProfiler.monsterUpdate, updateMs);
            perfRow("Update 相机", m_frameProfiler.cameraUpdate, updateMs);
            perfRow("Update 物理", m_frameProfiler.physicsUpdate, updateMs);
            perfRow("Update Actor", m_frameProfiler.actorUpdate, updateMs);
            perfRow("Update 状态机", m_frameProfiler.stateMachineUpdate, updateMs);
            perfRow("Update 掉落", m_frameProfiler.dropUpdate, updateMs);
            perfRow("Update 天气", m_frameProfiler.weatherUpdate, updateMs);
            perfRow("Update 任务", m_frameProfiler.missionUpdate, updateMs);
            perfRow("Update Chunk流送", m_frameProfiler.chunkStreamUpdate, updateMs);
            perfRow("Render 总计", m_frameProfiler.renderTotal, frameMs);
            perfRow("Render 背景", m_frameProfiler.backgroundRender, renderMs);
            perfRow("Render 场景对象", m_frameProfiler.sceneRender, renderMs);
            perfRow("Render 天空背景", m_frameProfiler.skyBackgroundRender, renderMs);
            perfRow("Render Chunk", m_frameProfiler.chunkRender, renderMs);
            perfRow("Render 视差", m_frameProfiler.parallaxRender, renderMs);
            perfRow("Render Sprite", m_frameProfiler.spriteRender, renderMs);
            perfRow("Render Tile", m_frameProfiler.tileRender, renderMs);
            perfRow("Render 阴影", m_frameProfiler.shadowRender, renderMs);
            perfRow("Render Actor", m_frameProfiler.actorRender, renderMs);
            perfRow("Render 光照", m_frameProfiler.lightingRender, renderMs);
            perfRow("Render ImGui", m_frameProfiler.imguiRender, renderMs);
            ImGui::EndTable();
        }

        // 最大帧率设置
        {
            int curTarget = _context.getTime().getTargetFPS();
            if (m_maxFpsSlider == 60 && curTarget != 60)
                m_maxFpsSlider = (curTarget > 0) ? curTarget : 0;

            const int kPresets[] = {30, 60, 120, 144, 240};
            ImGui::Text("最大帧率：");
            ImGui::SameLine();
            for (int p : kPresets)
            {
                bool sel = (m_maxFpsSlider == p);
                if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.55f, 1.0f, 1.0f));
                char buf[8]; snprintf(buf, sizeof(buf), "%d", p);
                if (ImGui::Button(buf))
                {
                    m_maxFpsSlider = p;
                    saveConfigValue("performance", "target_fps", p);
                    applyRuntimeGraphicsSettings();
                }
                if (sel) ImGui::PopStyleColor();
                ImGui::SameLine();
            }
            {
                bool sel = (m_maxFpsSlider == 0);
                if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.55f, 1.0f, 1.0f));
                if (ImGui::Button("不限"))
                {
                    m_maxFpsSlider = 0;
                    saveConfigValue("performance", "target_fps", 0);
                    applyRuntimeGraphicsSettings();
                }
                if (sel) ImGui::PopStyleColor();
            }
        }

        if (ImGui::Checkbox("垂直同步 VSync", &m_vsyncEnabled))
        {
            applyRuntimeGraphicsSettings();
        }
        ImGui::TextDisabled("开启 VSync 时，OpenGL 会被显示器刷新率限制；想超过 60/120/144，必须先关闭它。");

        ImGui::Spacing();

        // ── 图形设置 ───────────────────────────────────────────────────────
        ImGui::SeparatorText("图形设置");

        if (ImGui::SliderFloat("相机缩放", &m_zoomSliderValue, 0.5f, 8.0f))
            _context.getCamera().setZoom(m_zoomSliderValue);
        ImGui::SameLine();
        if (ImGui::Button("重置"))
        {
            m_zoomSliderValue = 2.5f;
            _context.getCamera().setZoom(2.5f);
        }

        // 粒子效果档位
        {
            const char* levelLabels[] = { "无粒子", "少量", "中等", "大量" };
            int lvl = static_cast<int>(m_uiParticleLevel);
            ImGui::Text("界面粒子效果：");
            ImGui::SameLine();
            for (int i = 0; i < 4; ++i)
            {
                bool sel = (lvl == i);
                if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.55f, 1.0f, 1.0f));
                if (ImGui::Button(levelLabels[i]))
                    m_uiParticleLevel = static_cast<UiParticleLevel>(i);
                if (sel) ImGui::PopStyleColor();
                if (i < 3) ImGui::SameLine();
            }
        }

        if (ImGui::Checkbox("显示帧率", &m_showFpsOverlay)) {}
        if (ImGui::Checkbox("角色朝向反向", &m_invertPlayerFacing))
            saveBoolSetting("invert_player_facing", m_invertPlayerFacing);

        // ── 运行模式切换 ───────────────────────────────────────────────────
        ImGui::SeparatorText("运行模式");
        ImGui::PushStyleColor(ImGuiCol_Button,
            m_devMode ? ImVec4(0.22f,0.22f,0.28f,1.f) : ImVec4(0.12f,0.45f,0.18f,1.f));
        if (ImGui::Button("游戏模式##gm", ImVec2(90, 0))) m_devMode = false;
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,
            m_devMode ? ImVec4(0.65f,0.35f,0.05f,1.f) : ImVec4(0.22f,0.22f,0.28f,1.f));
        if (ImGui::Button("开发模式##dm", ImVec2(90, 0))) m_devMode = true;
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled(m_devMode ? "  调试覆盖层已启用" : "  干净游戏画面");

        // 仅开发模式下显示的调试选项
        if (m_devMode)
        {
            ImGui::Separator();
            if (ImGui::Checkbox("显示技能调试", &m_showSkillDebugOverlay))
                saveBoolSetting("show_skill_debug_overlay", m_showSkillDebugOverlay);
            if (ImGui::Checkbox("地形块边框", &m_showActiveChunkHighlights))
                saveBoolSetting("show_active_chunk_highlights", m_showActiveChunkHighlights);
            if (ImGui::Checkbox("物理调试", &m_showPhysicsDebug)) {}

            ImGui::Spacing();
            ImGui::SeparatorText("开发属导工具");
            if (ImGui::Button("打开帧编辑器", ImVec2(160, 0)))
                m_frameEditor.open();
            ImGui::SameLine();
            if (ImGui::Button("打开状态机编辑器", ImVec2(160, 0)))
                m_smEditor.open();
        }

        // ── 天气控制 ───────────────────────────────────────────────────────
        ImGui::SeparatorText("天气控制");
        ImGui::Text("星球: %s  %02d:%02d %s  日照: %.0f%%",
            game::route::RouteData::planetName(m_routeData.selectedPlanet),
            m_timeOfDaySystem.getHour24(), m_timeOfDaySystem.getMinute(),
            m_timeOfDaySystem.getPhaseName(),
            m_timeOfDaySystem.getDaylightFactor() * 100.0f);
        ImGui::Text("当前天气: %s  天体可见度: %.0f%%",
            m_weatherSystem.getCurrentWeatherName(),
            m_weatherSystem.getSkyVisibility() * 100.0f);
        if (ImGui::Checkbox("贴屏雨滴（移动时掠过）", &m_screenRainOverlay))
        {
            saveBoolSetting("screen_rain_overlay", m_screenRainOverlay);
            m_weatherSystem.setScreenRainOverlayEnabled(m_screenRainOverlay);
        }
        if (m_screenRainOverlay)
        {
            if (ImGui::SliderFloat("贴屏雨强", &m_screenRainOverlayStrength, 0.2f, 2.0f, "%.2f"))
            {
                m_weatherSystem.setScreenRainOverlayStrength(m_screenRainOverlayStrength);
                saveFloatSetting("screen_rain_overlay_strength", m_screenRainOverlayStrength);
            }
            if (ImGui::SliderFloat("掠过强度", &m_screenRainMotionStrength, 0.2f, 3.0f, "%.2f"))
            {
                m_weatherSystem.setScreenRainMotionScale(m_screenRainMotionStrength);
                saveFloatSetting("screen_rain_motion_strength", m_screenRainMotionStrength);
            }
        }
        ImGui::TextDisabled("会额外绘制一层屏幕空间雨幕，并根据角色移动速度增强横向掠过感。");
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
    }

    void GameScene::applyRuntimeGraphicsSettings()
    {
        _context.getTime().setTargetFPS(m_maxFpsSlider);
        _context.getTime().setFrameLimitEnabled(!m_vsyncEnabled);

        applyRendererVSync(_context.getRenderer(), m_vsyncEnabled);
        saveConfigValue("graphics", "vsync", m_vsyncEnabled);
    }

    // 编辑器 UI 与回滚逻辑已拆分到 game_scene_editor.cpp。

    // ─────────────────────────────────────────────────────────────────────────
    //  加载玩家状态机
    // ─────────────────────────────────────────────────────────────────────────
    void GameScene::loadPlayerSM(const std::string& smJsonPath)
    {
        using namespace game::statemachine;
        if (!SmLoader::load(smJsonPath, m_playerSMData))
        {
            spdlog::warn("[GameScene] 状态机加载失败: {}", SmLoader::lastError());
            m_playerSMPath.clear();
            m_playerSMLoaded = false;
            return;
        }
        m_playerSM.init(&m_playerSMData);
        m_playerSMPath   = smJsonPath;
        m_playerSMLoaded = true;
        spdlog::info("[GameScene] 玩家状态机加载完成: {} (初始: {})",
                     smJsonPath, m_playerSMData.initialState);

        // 立即播放初始状态对应的动画（init 内部用 dummy result，stateChanged 不会传出来）
        if (m_player && !m_playerSMData.initialState.empty())
        {
            auto it = m_playerSMData.states.find(m_playerSMData.initialState);
            if (it != m_playerSMData.states.end() && !it->second.animationId.empty())
            {
                if (auto* anim = m_player->getComponent<engine::component::AnimationComponent>())
                    anim->forcePlay(it->second.animationId);
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  每帧驱动玩家状态机
    //
    //  activeInputs 构成规则：
    //    - 持续型条件：每帧根据物理/输入状态判断后直接放入
    //    - 瞬间型按键：通过 m_playerSM.pushInput() 放入 0.2s 缓冲池
    //    - LAND 落地事件：仅在从 AIRBORNE→GROUNDED 的那一帧放入
    //
    //  如何自定义新触发器：
    //    1. 在 sm_types.h kTriggerPresets 中添加字符串（供编辑器显示）
    //    2. 在本函数中用 activeInputs.push_back("YOUR_TRIGGER") 添加判断
    //    3. 在 *.sm.json 的 transitions 里用相同字符串引用
    // ─────────────────────────────────────────────────────────────────────────
    void GameScene::tickPlayerSM(float dt)
    {
        if (!m_playerSMLoaded || !m_player || m_isPlayerInMech || m_possessedMonster) return;

        using namespace engine::component;
        using namespace game::statemachine;

        // ── 1. 物理状态查询 ────────────────────────────────────────────────
        auto* physics    = m_player->getComponent<PhysicsComponent>();
        auto* controller = m_player->getComponent<ControllerComponent>();
        if (!physics || !controller) return;

        const glm::vec2 vel      = physics->getVelocity();
        const auto      mvState  = controller->getMovementState();
        using MS = ControllerComponent::MovementState;
        const bool grounded = (mvState == MS::Idle || mvState == MS::Run);
        const bool land     = (!m_prevGrounded && grounded);   // 落地瞬间
        m_prevGrounded = grounded;

        // ── 2. 构建持续型 activeInputs ────────────────────────────────────
        //  规则：每帧只要条件成立就放入，状态机内部对持续型无缓冲要求
        std::vector<std::string> activeInputs;
        activeInputs.reserve(8);

        // 物理/重力
        if (grounded)      activeInputs.push_back("GROUNDED");
        else               activeInputs.push_back("AIRBORNE");
        if (vel.y < -0.5f) activeInputs.push_back("RISING");   // Box2D y-up → 上升时 vy < 0
        if (vel.y >  0.5f) activeInputs.push_back("FALLING");  // 下落时 vy > 0
        if (land)          activeInputs.push_back("LAND");

        // 移动方向键（按住 ≡ 持续型）
        const auto& inp = _context.getInputManager();
        const bool moveL = inp.isActionDown("move_left");
        const bool moveR = inp.isActionDown("move_right");
        if (moveL) activeInputs.push_back("KEY_MOVE_L");
        if (moveR) activeInputs.push_back("KEY_MOVE_R");
        if (!moveL && !moveR) activeInputs.push_back("NO_INPUT");
        if (moveL || moveR)   activeInputs.push_back("IS_MOVING");

        // 冲刺持续
        if (m_isDashing) activeInputs.push_back("IS_DASHING");

        // 当前是否处于攻击状态
        if (m_playerSM.getCurrentState().find("ATTACK") != std::string::npos)
            activeInputs.push_back("IS_ATTACKING");

        // ── 3. 瞬间型按键 → pushInput（仅在按下那帧调用）────────────────
        //  这些按键有 0.2s 缓冲，可以提前按下并在连招窗口期生效
        static float s_smTime = 0.0f;
        s_smTime += dt;

        if (inp.isActionPressed("attack"))    m_playerSM.pushInput("KEY_ATTACK",  s_smTime);
        if (inp.isActionPressed("jump"))      m_playerSM.pushInput("KEY_JUMP",    s_smTime);
        if (inp.isActionPressed("skill_use")) m_playerSM.pushInput("KEY_SKILL_1", s_smTime);
        // 如需 dash/block/skill_2/3，在 config.json 中绑定按键后在此处添加同样的逻辑

        // ── 4. 驱动状态机 ────────────────────────────────────────────────
        const UpdateResult result = m_playerSM.update(dt, activeInputs, s_smTime);

        // ── 5. 应用根位移（Root Motion）──────────────────────────────────
        if (result.rootMotionDx != 0.0f || result.rootMotionDy != 0.0f)
        {
            const float facing =
                (controller->getFacingDirection() == ControllerComponent::FacingDirection::Left)
                    ? -1.0f : 1.0f;
            physics->applyImpulse({ result.rootMotionDx * facing, result.rootMotionDy });
        }

        // ── 6. 帧事件回调 ─────────────────────────────────────────────────
        for (const auto& evt : result.firedEvents)
        {
            // 格式："动词:参数"，e.g. "play_sound:sword_swing"
            if      (evt.rfind("play_sound:", 0) == 0) { /* TODO: 音频系统 */ }
            else if (evt.rfind("spawn_vfx:",  0) == 0) { /* TODO: 特效生成 */ }
            else if (evt == "shake_screen")             { /* TODO: 屏幕震动 */ }
            spdlog::debug("[SM] 帧事件: {}", evt);
        }

        // ── 7. 同步动画组件 ────────────────────────────────────────────────
        if (result.stateChanged)
        {
            spdlog::debug("[SM] → {}", result.currentState);

            auto it = m_playerSMData.states.find(result.currentState);
            if (it != m_playerSMData.states.end())
            {
                const std::string& clipName = it->second.animationId;
                if (!clipName.empty())
                {
                    auto* anim = m_player->getComponent<engine::component::AnimationComponent>();
                    if (anim)
                        anim->play(clipName);
                }
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  扫描 assets/textures/Characters/*.sm.json → m_characters
    // ─────────────────────────────────────────────────────────────────────────
    void GameScene::scanCharacters()
    {
        m_characters.clear();
        const std::filesystem::path dir = "assets/textures/Characters";
        if (!std::filesystem::exists(dir)) return;

        for (const auto& entry : std::filesystem::directory_iterator(dir))
        {
            const auto& p = entry.path();
            if (p.extension() == ".json" && p.stem().extension() == ".sm")
            {
                CharacterEntry ce;
                ce.id          = p.stem().stem().string(); // "gundom"
                ce.displayName = ce.id;
                ce.smPath      = p.string();
                m_characters.push_back(ce);
            }
        }
        std::sort(m_characters.begin(), m_characters.end(),
                  [](const CharacterEntry& a, const CharacterEntry& b){ return a.id < b.id; });
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  开发模式右上角：角色选择器覆盖层
    // ─────────────────────────────────────────────────────────────────────────
    void GameScene::renderDevModeOverlay()
    {
        const float w = 280.0f;
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - w - 8.0f, 8.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(w, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.80f);

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration  | ImGuiWindowFlags_NoMove        |
            ImGuiWindowFlags_NoNav         | ImGuiWindowFlags_NoSavedSettings|
            ImGuiWindowFlags_AlwaysAutoResize;

        if (!ImGui::Begin("##dev_overlay", nullptr, flags))
        { ImGui::End(); return; }

        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "[开发模式]");
        ImGui::Separator();

        // ── 角色下拉 ────────────────────────────────────────────────────────
        if (m_characters.empty()) scanCharacters();

        const char* curName = m_characters.empty() ? "（无）"
                            : m_characters[m_selectedCharacter].displayName.c_str();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##char_sel", curName))
        {
            for (int i = 0; i < (int)m_characters.size(); i++)
            {
                bool sel = (i == m_selectedCharacter);
                if (ImGui::Selectable(m_characters[i].displayName.c_str(), sel))
                {
                    m_selectedCharacter = i;
                    loadPlayerSM(m_characters[i].smPath);
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // ── 当前 SM 文件提示 ────────────────────────────────────────────────
        if (!m_characters.empty())
        {
            const std::string& selectedPath = m_characters[m_selectedCharacter].smPath;
            ImGui::TextDisabled("选择的 SM: %s", selectedPath.c_str());

            const bool sameAsPlayer = (!m_playerSMPath.empty() && m_playerSMPath == selectedPath);
            if (sameAsPlayer)
                ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.45f, 1.0f), "玩家当前 SM: %s", m_playerSMPath.c_str());
            else if (!m_playerSMPath.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f), "玩家当前 SM: %s", m_playerSMPath.c_str());
        }

        if (ImGui::Checkbox("角色朝向反向##dev", &m_invertPlayerFacing))
            saveBoolSetting("invert_player_facing", m_invertPlayerFacing);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f), "[状态机调试]");
        ImGui::Text("状态机已加载: %s", m_playerSMLoaded ? "true" : "false");
        ImGui::Text("当前控制对象: %s", getControlledActor() == m_player ? "player" : "other");
        ImGui::Text("当前状态: %s", m_playerSM.getCurrentState().empty()
            ? "<empty>" : m_playerSM.getCurrentState().c_str());
        if (!m_playerSM.getCurrentState().empty())
        {
            auto it = m_playerSMData.states.find(m_playerSM.getCurrentState());
            ImGui::Text("状态绑定动画ID: %s", (it != m_playerSMData.states.end() && !it->second.animationId.empty())
                ? it->second.animationId.c_str() : "<empty>");
        }
        if (auto* anim = m_player
                ? m_player->getComponent<engine::component::AnimationComponent>()
                : nullptr)
        {
            ImGui::Text("当前动画 Clip: %s", anim->currentClip().empty()
                ? "<empty>" : anim->currentClip().c_str());
            ImGui::Text("当前动画帧: %d  计时: %.3f", anim->currentFrame(), anim->currentTimer());
        }
        ImGui::Text("初始状态: %s", m_playerSMData.initialState.empty()
            ? "<empty>" : m_playerSMData.initialState.c_str());
        ImGui::Text("状态数: %d", static_cast<int>(m_playerSMData.states.size()));

        if (auto* sprite = m_player
                ? m_player->getComponent<engine::component::SpriteComponent>()
                : nullptr)
        {
            const auto& spriteData = sprite->getSprite();
            const auto& srcRectOpt = spriteData.getSourceRect();
            const std::string& texId = spriteData.getTextureId();
            const unsigned int glTex = _context.getResourceManager().getGLTexture(texId);
            const glm::vec2 texSize = _context.getResourceManager().getTextureSize(texId);
            const glm::vec2 spriteSize = sprite->getSpriteSize();
            const glm::vec2 spriteOffset = sprite->getOffset();
            const glm::vec4 cachedUv = sprite->getCachedUV();

            if (auto* transform = m_player->getComponent<engine::component::TransformComponent>())
            {
                const glm::vec2 pos = transform->getPosition();
                const glm::vec2 scale = transform->getScale();
                ImGui::Text("玩家可见: %s  机甲中: %s", sprite->isHidden() ? "false" : "true", m_isPlayerInMech ? "true" : "false");
                ImGui::Text("玩家位置: x=%.1f y=%.1f", pos.x, pos.y);
                ImGui::Text("玩家缩放: x=%.3f y=%.3f  旋转: %.1f", scale.x, scale.y, transform->getRotation());
            }
            ImGui::Text("玩家纹理: %s", texId.empty() ? "<empty>" : texId.c_str());
            ImGui::Text("GL纹理句柄: %u  纹理尺寸: %.0f x %.0f", glTex, texSize.x, texSize.y);
            ImGui::Text("Sprite尺寸: %.1f x %.1f  偏移: %.1f, %.1f", spriteSize.x, spriteSize.y, spriteOffset.x, spriteOffset.y);
            ImGui::Text("缓存UV: %.4f %.4f %.4f %.4f", cachedUv.x, cachedUv.y, cachedUv.z, cachedUv.w);

            if (glTex != 0 && srcRectOpt.has_value() && texSize.x > 0.0f && texSize.y > 0.0f)
            {
                const auto& src = srcRectOpt.value();
                ImGui::Text("源矩形: x=%.0f y=%.0f w=%.0f h=%.0f",
                    src.position.x, src.position.y, src.size.x, src.size.y);
                const float u0 = src.position.x / texSize.x;
                const float v0 = src.position.y / texSize.y;
                const float u1 = (src.position.x + src.size.x) / texSize.x;
                const float v1 = (src.position.y + src.size.y) / texSize.y;

                const float previewScale = 2.0f;
                const ImVec2 previewSize(src.size.x * previewScale, src.size.y * previewScale);

                ImGui::Spacing();
                ImGui::TextUnformatted("当前动画预览:");
                ImGui::Image((ImTextureID)(intptr_t)glTex, previewSize, ImVec2(u0, v0), ImVec2(u1, v1));
            }
        }

        ImGui::Spacing();

        // ── 快捷打开按钮 ────────────────────────────────────────────────────
        if (ImGui::Button("帧编辑器", ImVec2(-1, 0)))
            m_frameEditor.open();
        if (ImGui::Button("状态机编辑器", ImVec2(-1, 0)))
        {
            if (!m_characters.empty())
            {
                // 如果编辑器当前未打开某文件，尝试用所选角色的 SM 打开
                m_smEditor.open();
            }
            else
            {
                m_smEditor.open();
            }
        }

        ImGui::End();
    }

    void GameScene::renderPerformanceOverlay()    {
        ImGui::SetNextWindowPos({10.0f, 10.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.45f);
        ImGui::Begin("##fps_game", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
        const float frameMs = std::max(m_frameProfiler.frameDeltaMs, 0.0001f);
        ImGui::Text("FPS: %.1f  Δ: %.2fms", ImGui::GetIO().Framerate, m_frameProfiler.frameDeltaMs);
        ImGui::Text("Update %.2f (%.0f%%) | Render %.2f (%.0f%%)",
            m_frameProfiler.updateTotal.lastMs,
            perfPercent(m_frameProfiler.updateTotal.lastMs, frameMs),
            m_frameProfiler.renderTotal.lastMs,
            perfPercent(m_frameProfiler.renderTotal.lastMs, frameMs));
        ImGui::Text("Chunk 已加载 %zu | 待加载 %zu", m_frameProfiler.loadedChunks, m_frameProfiler.pendingChunkLoads);

        struct Hotspot { const char* label; float ms; };
        std::array<Hotspot, 16> hotspots{{
            {"U 核心逻辑", m_frameProfiler.coreLogic.lastMs},
            {"U 怪物", m_frameProfiler.monsterUpdate.lastMs},
            {"U 相机", m_frameProfiler.cameraUpdate.lastMs},
            {"U 物理", m_frameProfiler.physicsUpdate.lastMs},
            {"U Actor", m_frameProfiler.actorUpdate.lastMs},
            {"U 状态机", m_frameProfiler.stateMachineUpdate.lastMs},
            {"U 掉落", m_frameProfiler.dropUpdate.lastMs},
            {"U 天气", m_frameProfiler.weatherUpdate.lastMs},
            {"U 任务", m_frameProfiler.missionUpdate.lastMs},
            {"U Chunk流送", m_frameProfiler.chunkStreamUpdate.lastMs},
            {"R Chunk", m_frameProfiler.chunkRender.lastMs},
            {"R 视差", m_frameProfiler.parallaxRender.lastMs},
            {"R Sprite", m_frameProfiler.spriteRender.lastMs},
            {"R Tile", m_frameProfiler.tileRender.lastMs},
            {"R 阴影", m_frameProfiler.shadowRender.lastMs},
            {"R 光照", m_frameProfiler.lightingRender.lastMs}
        }};
        std::sort(hotspots.begin(), hotspots.end(), [](const Hotspot& a, const Hotspot& b) {
            return a.ms > b.ms;
        });

        ImGui::Separator();
        ImGui::TextUnformatted("热点 Top3（占整帧）");
        for (int i = 0; i < 3; ++i)
        {
            const auto& h = hotspots[static_cast<size_t>(i)];
            ImGui::Text("%d) %s %.2fms  %.1f%%", i + 1, h.label, h.ms, perfPercent(h.ms, frameMs));
        }

        if (m_devMode)
        {
            ImGui::Separator();
            ImGui::Text("U 逻辑 %.2f  怪物 %.2f  相机 %.2f", m_frameProfiler.coreLogic.lastMs, m_frameProfiler.monsterUpdate.lastMs, m_frameProfiler.cameraUpdate.lastMs);
            ImGui::Text("U 物理 %.2f  Actor %.2f  状态机 %.2f", m_frameProfiler.physicsUpdate.lastMs, m_frameProfiler.actorUpdate.lastMs, m_frameProfiler.stateMachineUpdate.lastMs);
            ImGui::Text("U 掉落 %.2f  天气 %.2f  任务 %.2f", m_frameProfiler.dropUpdate.lastMs, m_frameProfiler.weatherUpdate.lastMs, m_frameProfiler.missionUpdate.lastMs);
            ImGui::Text("U Chunk流送 %.2f", m_frameProfiler.chunkStreamUpdate.lastMs);
            ImGui::Separator();
            ImGui::Text("R 背景 %.2f  场景 %.2f  天空 %.2f", m_frameProfiler.backgroundRender.lastMs, m_frameProfiler.sceneRender.lastMs, m_frameProfiler.skyBackgroundRender.lastMs);
            ImGui::Text("R Chunk %.2f  视差 %.2f  Sprite %.2f", m_frameProfiler.chunkRender.lastMs, m_frameProfiler.parallaxRender.lastMs, m_frameProfiler.spriteRender.lastMs);
            ImGui::Text("R Sprite %.2f  Tile %.2f  阴影 %.2f", m_frameProfiler.spriteRender.lastMs, m_frameProfiler.tileRender.lastMs, m_frameProfiler.shadowRender.lastMs);
            ImGui::Text("R Actor %.2f  光照 %.2f  ImGui %.2f", m_frameProfiler.actorRender.lastMs, m_frameProfiler.lightingRender.lastMs, m_frameProfiler.imguiRender.lastMs);
        }
        ImGui::End();
    }

    void GameScene::renderMechPrompt()
    {
        if (m_possessedMonster)
        {
            ImGuiIO &io = ImGui::GetIO();
            const float width = 320.0f;
            ImGui::SetNextWindowPos({(io.DisplaySize.x - width) * 0.5f, io.DisplaySize.y - 112.0f}, ImGuiCond_Always);
            ImGui::SetNextWindowSize({width, 84.0f}, ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.84f);
            ImGui::Begin("##possession_hud", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                ImGuiWindowFlags_NoSavedSettings);
            ImGui::Text("已接管: %s", m_possessedMonster->getName().c_str());
            ImGui::Text("C 解除接管   WASD 直接控制   攻击/技能 可用");
            if (m_possessedAttackCooldown > 0.0f)
                ImGui::Text("普攻冷却 %.2fs", m_possessedAttackCooldown);
            else
                ImGui::TextColored({0.55f, 1.0f, 0.7f, 1.0f}, "普攻就绪");
            if (m_possessedSkillCooldown > 0.0f)
                ImGui::Text("技能冷却 %.2fs", m_possessedSkillCooldown);
            else
                ImGui::TextColored({0.60f, 0.85f, 1.0f, 1.0f}, "技能就绪");
            if (m_possessedLastAttackHits > 0)
                ImGui::Text("上次攻击清除: %d", m_possessedLastAttackHits);
            else
                ImGui::TextDisabled("上次攻击未命中");
            ImGui::ProgressBar(std::clamp(m_possessionEnergy / 12.0f, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), "控制能量");
            ImGui::End();
            return;
        }

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
            ImGui::Text("飞行引擎: %s", m_mechFlightEngineInstalled ? "已安装（空中二段空格可飞行）" : "未安装");
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
        std::string stateLabel;
        if (actor == m_player && m_playerSMLoaded && !m_playerSM.getCurrentState().empty())
        {
            if (auto* anim = m_player->getComponent<engine::component::AnimationComponent>())
                stateLabel = anim->currentClip();

            if (stateLabel.empty())
            {
                auto it = m_playerSMData.states.find(m_playerSM.getCurrentState());
                if (it != m_playerSMData.states.end())
                    stateLabel = it->second.animationId;
            }

            if (stateLabel.empty())
                stateLabel = m_playerSM.getCurrentState();
        }
        else
        {
            stateLabel = controller->getMovementStateName();
            if (m_isPlayerInMech)
                stateLabel = m_statePrefixMech + stateLabel;
            else if (actor == m_possessedMonster)
                stateLabel = m_statePrefixPossessed + stateLabel;
        }
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

    void GameScene::renderModeSwitchHint()
    {
        if (m_modeSwitchHintTimer <= 0.0f || m_modeSwitchHintText.empty())
            return;

        // 淡入淡出：前 0.25s 渐显，后 0.35s 渐隐
        static constexpr float kFadeIn  = 0.25f;
        static constexpr float kFadeOut = 0.35f;
        static constexpr float kTotal   = 1.8f;
        const float elapsed = kTotal - m_modeSwitchHintTimer;
        float alpha = 1.0f;
        if (elapsed < kFadeIn)
            alpha = elapsed / kFadeIn;
        else if (m_modeSwitchHintTimer < kFadeOut)
            alpha = m_modeSwitchHintTimer / kFadeOut;
        alpha = std::clamp(alpha, 0.0f, 1.0f);

        const ImVec2 dispSize = ImGui::GetIO().DisplaySize;
        ImDrawList *dl = ImGui::GetForegroundDrawList();

        // 大字提示
        ImFont *font = ImGui::GetFont();
        const float fontSize  = 22.0f;
        const ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, m_modeSwitchHintText.c_str());
        const ImVec2 textPos  = {(dispSize.x - textSize.x) * 0.5f,
                                  dispSize.y * 0.30f - textSize.y * 0.5f};

        const bool isFly = (m_modeSwitchHintText == "飞行模式");
        // 背景胶囊
        const float padX = 18.0f, padY = 9.0f;
        ImVec2 bMin{textPos.x - padX, textPos.y - padY};
        ImVec2 bMax{textPos.x + textSize.x + padX, textPos.y + textSize.y + padY};
        ImU32 bgCol  = isFly ? IM_COL32(10, 30, 60,  static_cast<int>(190 * alpha))
                             : IM_COL32(20, 20, 20,  static_cast<int>(190 * alpha));
        ImU32 bdCol  = isFly ? IM_COL32(80, 160, 255, static_cast<int>(230 * alpha))
                             : IM_COL32(160, 170, 180, static_cast<int>(200 * alpha));
        ImU32 txCol  = isFly ? IM_COL32(140, 210, 255, static_cast<int>(255 * alpha))
                             : IM_COL32(220, 230, 240, static_cast<int>(255 * alpha));
        const float rounding = (bMax.y - bMin.y) * 0.5f;
        dl->AddRectFilled(bMin, bMax, bgCol, rounding);
        dl->AddRect(bMin, bMax, bdCol, rounding, 0, 1.5f);
        dl->AddText(font, fontSize, textPos, txCol, m_modeSwitchHintText.c_str());
    }

    void GameScene::renderFlightThrusterFX()
    {
        auto *actor = getControlledActor();
        if (!actor)
            return;

        auto *transform = actor->getComponent<engine::component::TransformComponent>();
        auto *controller = actor->getComponent<engine::component::ControllerComponent>();
        auto *sprite = actor->getComponent<engine::component::SpriteComponent>();
        if (!transform || !controller)
            return;

        if (controller->getMovementState() != engine::component::ControllerComponent::MovementState::Jetpack)
            return;

        glm::vec2 worldPos = transform->getPosition();
        glm::vec2 screenLogical = _context.getCamera().worldToScreen(worldPos);
        ImVec2 center = logicalToImGuiScreen(_context, screenLogical);

        const float t = static_cast<float>(ImGui::GetTime());
        const float pulse = 0.78f + 0.22f * std::sin(t * 18.0f);
        const float jitter = std::sin(t * 33.0f) * 1.6f;
        const bool facingLeft = controller->getFacingDirection() == engine::component::ControllerComponent::FacingDirection::Left;
        const float altitudeMeters = std::max(0.0f, controller->getPosZ() / 32.0f);
        const float altitudeT = std::clamp((altitudeMeters - 120.0f) / 780.0f, 0.0f, 1.0f);

        float spriteW = 42.0f;
        float spriteH = 58.0f;
        if (sprite)
        {
            glm::vec2 sz = sprite->getSpriteSize();
            if (sz.x > 1.0f) spriteW = sz.x * 0.22f;
            if (sz.y > 1.0f) spriteH = sz.y * 0.45f;
        }

        ImDrawList *dl = ImGui::GetForegroundDrawList();
        const float baseY = center.y + spriteH;
        const float side = facingLeft ? -1.0f : 1.0f;

        for (int i = 0; i < 2; ++i)
        {
            const float offsetX = side * (8.0f + i * 10.0f);
            const float len = (18.0f + i * 5.0f) * pulse * (1.0f + altitudeT * 1.4f);
            const float width = 7.0f - i * 1.5f;

            ImVec2 nozzle{center.x + offsetX, baseY};
            ImVec2 tipA{nozzle.x - width, nozzle.y + 4.0f};
            ImVec2 tipB{nozzle.x + width, nozzle.y + 4.0f};
            ImVec2 tail{nozzle.x + jitter * 0.25f, nozzle.y + len + jitter};

            dl->AddTriangleFilled(tipA, tipB, tail, IM_COL32(255, 120, 20, 210));
            dl->AddTriangleFilled(
                {nozzle.x - width * 0.42f, nozzle.y + 4.0f},
                {nozzle.x + width * 0.42f, nozzle.y + 4.0f},
                {tail.x, nozzle.y + len * 0.62f},
                IM_COL32(255, 225, 90, 220));

            dl->AddCircleFilled(nozzle, 3.0f + 0.5f * pulse, IM_COL32(255, 230, 120, 190));
            dl->AddCircleFilled(nozzle, 7.0f + i * 2.0f, IM_COL32(255, 120, 20, 55));

            // 高空高速尾迹：让玩家感知“稀薄空气中的高速滑行”
            if (altitudeT > 0.05f)
            {
                const float trailLen = len * (1.5f + altitudeT * 1.8f);
                const int trailA = static_cast<int>(70.0f + 120.0f * altitudeT);
                ImVec2 trailTail{nozzle.x + jitter * 0.15f, nozzle.y + trailLen};
                dl->AddLine(nozzle, trailTail, IM_COL32(255, 210, 120, trailA), 2.6f - i * 0.6f);
                dl->AddLine(
                    {nozzle.x - width * 0.25f, nozzle.y + 2.0f},
                    {trailTail.x - width * 0.18f, trailTail.y + 4.0f},
                    IM_COL32(180, 220, 255, trailA * 2 / 3),
                    1.5f - i * 0.25f);
            }
        }

        if (altitudeT > 0.20f && controller->isFlyModeActive())
        {
            const int streakCount = 5 + static_cast<int>(altitudeT * 5.0f);
            for (int s = 0; s < streakCount; ++s)
            {
                float ph = t * (28.0f + s * 2.0f) + s * 1.7f;
                float rx = std::sin(ph * 0.63f) * (22.0f + s * 5.0f);
                float ry = std::cos(ph * 0.42f) * (10.0f + s * 2.5f);
                float sl = 12.0f + 10.0f * altitudeT;
                int sa = static_cast<int>(55.0f + 90.0f * altitudeT);
                ImVec2 a{center.x + rx, center.y + ry};
                ImVec2 b{center.x + rx - side * sl, center.y + ry + 1.5f};
                dl->AddLine(a, b, IM_COL32(185, 220, 255, sa), 1.2f);
            }
        }
    }

    void GameScene::renderMonsterIFFMarkers()
    {
        if (!m_monsterManager)
            return;

        const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(SDL_GetTicks()) * 0.008f);
        m_monsterManager->renderIFFMarkers(getControlledActor(), pulse);
    }

    // ------------------------------------------------------------------
    //  路线 HUD（左下角，显示 20×20 路线进度）
    // ------------------------------------------------------------------
    void GameScene::renderRouteHUD()
    {
        if (!m_routeData.isValid()) return;

        using namespace game::route;
        using R = RouteData;

        // DNF 风格小地图：每格放大为 8px，加 1px 间距
        const float CELL_PX  = 8.0f;
        const float CELL_TOT = CELL_PX + 1.0f;
        const float MAP_W    = R::MAP_SIZE * CELL_TOT;
        const float MAP_H    = R::MAP_SIZE * CELL_TOT;
        const float WIN_W    = MAP_W + 18.0f;
        const float WIN_H    = MAP_H + 48.0f;

        float dh = ImGui::GetIO().DisplaySize.y;
        ImGui::SetNextWindowPos({8.0f, dh - WIN_H - 8.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({WIN_W, WIN_H}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin("##dnf_routehud", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoNav         | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

        auto* dl     = ImGui::GetWindowDrawList();
        ImVec2 wp    = ImGui::GetWindowPos();

        // ── 面板背景（深色仿旧皮革）────────────────────────────────────────
        dl->AddRectFilled({wp.x, wp.y}, {wp.x + WIN_W, wp.y + WIN_H},
            IM_COL32(14, 10, 6, 215), 5.0f);
        dl->AddRect({wp.x, wp.y}, {wp.x + WIN_W, wp.y + WIN_H},
            IM_COL32(150, 118, 45, 180), 5.0f, 0, 1.2f);
        dl->AddRect({wp.x + 2, wp.y + 2}, {wp.x + WIN_W - 2, wp.y + WIN_H - 2},
            IM_COL32(80, 62, 22, 120), 4.0f, 0, 0.8f);

        // 四角铆钉
        for (int cx = 0; cx < 2; ++cx) for (int cy = 0; cy < 2; ++cy)
        {
            float rx = wp.x + (cx ? WIN_W - 6 : 6);
            float ry = wp.y + (cy ? WIN_H - 6 : 6);
            dl->AddCircleFilled({rx, ry}, 3.5f, IM_COL32(150, 118, 45, 200));
            dl->AddCircleFilled({rx, ry}, 1.8f, IM_COL32(220, 185, 85, 240));
        }

        // ── 标题 ────────────────────────────────────────────────────────
        const char* titleStr = "地下城地图";
        ImVec2 titleSz = ImGui::CalcTextSize(titleStr);
        float  titleX  = wp.x + (WIN_W - titleSz.x) * 0.5f;
        dl->AddText({titleX + 1, wp.y + 5 + 1}, IM_COL32(0, 0, 0, 180), titleStr);
        dl->AddText({titleX, wp.y + 5},  IM_COL32(220, 185, 85, 255), titleStr);
        // 标题下分割线
        dl->AddLine({wp.x + 8, wp.y + 19}, {wp.x + WIN_W - 8, wp.y + 19},
            IM_COL32(150, 118, 45, 140), 0.8f);

        ImVec2 origin = {wp.x + 8.0f, wp.y + 24.0f};
        int psize = static_cast<int>(m_routeData.path.size());
        const float T = static_cast<float>(ImGui::GetTime());

        // ── 绘制格子 ─────────────────────────────────────────────────────
        for (int ry2 = 0; ry2 < R::MAP_SIZE; ++ry2)
        {
            for (int cx2 = 0; cx2 < R::MAP_SIZE; ++cx2)
            {
                float px = origin.x + cx2 * CELL_TOT;
                float py = origin.y + ry2 * CELL_TOT;
                ImVec2 p0{px, py}, p1{px + CELL_PX, py + CELL_PX};

                glm::ivec2 cell{cx2, ry2};
                int pidx = -1;
                for (int k = 0; k < psize; ++k)
                    if (m_routeData.path[k] == cell) { pidx = k; break; }

                bool isObjective = (cell == m_routeData.objectiveCell);

                ImU32 fill;
                if (pidx == 0)
                    fill = IM_COL32(50, 200, 90, 255);     // 出发（绿）
                else if (pidx == psize - 1)
                    fill = IM_COL32(220, 65, 65, 255);     // 撤离（红）
                else if (pidx == m_currentZone)
                {
                    // 当前房间：金色脉冲
                    float pulse = 0.5f + 0.5f * std::sin(T * 4.0f);
                    fill = IM_COL32(
                        static_cast<int>(200 + 55 * pulse),
                        static_cast<int>(170 + 40 * pulse),
                        30, 255);
                }
                else if (pidx >= 0 && pidx < m_currentZone)
                    fill = IM_COL32(60, 80, 55, 220);      // 已过（暗绿）
                else if (pidx > m_currentZone)
                    fill = IM_COL32(45, 40, 28, 200);      // 未到（深褐）
                else
                    fill = IM_COL32(20, 16, 10, 160);      // 非路径（极暗）

                // 格子填充
                dl->AddRectFilled(p0, p1, fill, 1.0f);

                // 路径格子加微弱内边框
                if (pidx >= 0)
                    dl->AddRect(p0, p1, IM_COL32(120, 95, 35, 100), 1.0f, 0, 0.7f);

                // 目标格：金色边框
                if (isObjective)
                    dl->AddRect(p0, p1, IM_COL32(255, 210, 50, 220), 0.0f, 0, 1.5f);
                // 当前区域：白色边框
                else if (pidx == m_currentZone)
                    dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 200), 0.0f, 0, 1.5f);
            }
        }

        // ── 路径连线（黄色虚线风格）──────────────────────────────────────
        for (int k = 0; k + 1 < psize; ++k)
        {
            const auto &a = m_routeData.path[k];
            const auto &b = m_routeData.path[k + 1];
            ImVec2 pa{origin.x + a.x * CELL_TOT + CELL_PX * 0.5f,
                      origin.y + a.y * CELL_TOT + CELL_PX * 0.5f};
            ImVec2 pb{origin.x + b.x * CELL_TOT + CELL_PX * 0.5f,
                      origin.y + b.y * CELL_TOT + CELL_PX * 0.5f};
            ImU32 lc = (k < m_currentZone)
                ? IM_COL32(80, 120, 70, 150)         // 已过：暗绿
                : IM_COL32(220, 180, 55, 190);       // 未过：金色
            dl->AddLine(pa, pb, lc, 1.5f);
        }

        // ── 当前玩家位置标记（比白边框更明显）─────────────────────────────
        if (m_currentZone >= 0 && m_currentZone < psize)
        {
            const auto &curCell = m_routeData.path[m_currentZone];
            ImVec2 center{
                origin.x + curCell.x * CELL_TOT + CELL_PX * 0.5f,
                origin.y + curCell.y * CELL_TOT + CELL_PX * 0.5f
            };
            const float pulse = 0.5f + 0.5f * std::sin(T * 5.5f);
            const float outerR = 5.0f + pulse * 3.0f;

            if (auto *controlled = getControlledActor())
            {
                const glm::vec2 worldPos = getActorWorldPosition(controlled);
                const int tileX = static_cast<int>(std::floor(worldPos.x / 16.0f));
                const int zoneStartTileX = m_currentZone * RouteData::TILES_PER_CELL;
                const float localTileX = static_cast<float>(tileX - zoneStartTileX);
                const float localRatio = std::clamp(localTileX / static_cast<float>(RouteData::TILES_PER_CELL), 0.0f, 1.0f);

                const float railMargin = 1.2f;
                const float railY = center.y + CELL_PX * 0.20f;
                const float railX0 = origin.x + curCell.x * CELL_TOT + railMargin;
                const float railX1 = origin.x + curCell.x * CELL_TOT + CELL_PX - railMargin;
                const float markerX = railX0 + (railX1 - railX0) * localRatio;

                dl->AddLine({railX0, railY}, {railX1, railY}, IM_COL32(35, 55, 80, 220), 1.2f);
                dl->AddLine({railX0, railY}, {markerX, railY}, IM_COL32(120, 220, 255, 235), 1.4f);
                dl->AddCircleFilled({markerX, railY}, 1.8f + pulse * 0.8f, IM_COL32(255, 250, 200, 255), 12);
                dl->AddCircle({markerX, railY}, 3.2f + pulse * 1.1f, IM_COL32(110, 220, 255, 180), 12, 1.0f);
            }

            dl->AddCircleFilled(center, 3.2f, IM_COL32(255, 245, 180, 255), 16);
            dl->AddCircle(center, outerR, IM_COL32(110, 220, 255, static_cast<int>(180 + 60 * pulse)), 20, 1.8f);
            dl->AddCircle(center, outerR + 3.0f, IM_COL32(110, 220, 255, static_cast<int>(80 + 40 * pulse)), 24, 1.0f);

            ImVec2 arrowA{center.x, center.y - 9.0f};
            ImVec2 arrowB{center.x - 4.0f, center.y - 14.0f};
            ImVec2 arrowC{center.x + 4.0f, center.y - 14.0f};
            dl->AddTriangleFilled(arrowA, arrowB, arrowC, IM_COL32(255, 240, 150, 240));
        }

        // ── 底部状态文字 ──────────────────────────────────────────────────
        float textY = wp.y + WIN_H - 22.0f;
        dl->AddLine({wp.x + 8, textY - 3}, {wp.x + WIN_W - 8, textY - 3},
            IM_COL32(100, 78, 28, 120), 0.8f);

        if (m_currentZone < psize)
        {
            const auto &curCell = m_routeData.path[m_currentZone];
            bool isLast = (m_currentZone == psize - 1);
            const char* label = RouteData::cellLabel(curCell).c_str();
            char stepBuf[64];
            snprintf(stepBuf, sizeof(stepBuf), "%d/%d  %s%s",
                m_currentZone + 1, psize, label, isLast ? " [撤]" : "");
            ImVec2 stSz = ImGui::CalcTextSize(stepBuf);
            ImU32  stCol = isLast
                ? IM_COL32(80, 220, 100, 255)
                : IM_COL32(220, 185, 85, 240);
            dl->AddText({wp.x + (WIN_W - stSz.x) * 0.5f, textY},
                IM_COL32(0, 0, 0, 160), stepBuf);
            dl->AddText({wp.x + (WIN_W - stSz.x) * 0.5f + 0, textY + 0}, stCol, stepBuf);
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
        m_mechInventory.addItem({"mech_flight_engine", "机甲飞行引擎", 1, Cat::Equipment,
                     game::inventory::EquipmentSlotType::MechEngine}, 1);
        m_mechInventory.addItem({"exo_armor", "外骨骼装甲", 1, Cat::Equipment,
                     game::inventory::EquipmentSlotType::Armor}, 1);
        m_mechInventory.addItem({"kinetic_actuator", "动能执行器", 1, Cat::Equipment,
                     game::inventory::EquipmentSlotType::AccessoryA}, 1);
        m_mechInventory.addItem({"gyro_stabilizer", "陀螺稳定器", 1, Cat::Equipment,
                     game::inventory::EquipmentSlotType::AccessoryB}, 1);

        // 星技珠子（StarSkill 类型，可放入星技圆形槽）
        m_inventory.addItem({"star_fire",  "炎焰星技", 1, Cat::StarSkill}, 2);
        m_inventory.addItem({"star_ice",   "寒冰星技", 1, Cat::StarSkill}, 1);
        m_inventory.addItem({"star_wind",  "疾风星技", 1, Cat::StarSkill}, 1);
        m_inventory.addItem({"star_light", "闪光星技", 1, Cat::StarSkill}, 1);
    }

    void GameScene::loadActorRoleConfig()
    {
        m_playerActorKey = loadConfigString("actor_roles", "player_actor_key", m_playerActorKey);
        m_mechActorKey = loadConfigString("actor_roles", "mech_actor_key", m_mechActorKey);
        m_controlLabelPlayer = loadConfigString("actor_roles", "control_label_player", m_controlLabelPlayer);
        m_controlLabelMech = loadConfigString("actor_roles", "control_label_mech", m_controlLabelMech);
        m_controlLabelPossessed = loadConfigString("actor_roles", "control_label_possessed", m_controlLabelPossessed);
        m_statePrefixMech = loadConfigString("actor_roles", "state_prefix_mech", m_statePrefixMech);
        m_statePrefixPossessed = loadConfigString("actor_roles", "state_prefix_possessed", m_statePrefixPossessed);
        m_hudPlayerText = loadConfigString("actor_roles", "hud_player_text", m_hudPlayerText);
        m_hudMechPrefix = loadConfigString("actor_roles", "hud_mech_prefix", m_hudMechPrefix);
        m_hudMechName = loadConfigString("actor_roles", "hud_mech_name", m_hudMechName);

        if (m_playerActorKey.empty()) m_playerActorKey = "player";
        if (m_mechActorKey.empty()) m_mechActorKey = "mech_drop";
        if (m_controlLabelPlayer.empty()) m_controlLabelPlayer = "人物";
        if (m_controlLabelMech.empty()) m_controlLabelMech = "机甲";
        if (m_controlLabelPossessed.empty()) m_controlLabelPossessed = "接管体";
        if (m_statePrefixMech.empty()) m_statePrefixMech = "机甲·";
        if (m_statePrefixPossessed.empty()) m_statePrefixPossessed = "接管·";
        if (m_hudPlayerText.empty()) m_hudPlayerText = "player";
        if (m_hudMechPrefix.empty()) m_hudMechPrefix = "机甲：";
        if (m_hudMechName.empty()) m_hudMechName = "gundom";
    }

    // ------------------------------------------------------------------
    //  左侧武器栏（常驻显示）
    // ------------------------------------------------------------------
    void GameScene::renderWeaponBar()
    {
        constexpr float SLOT_H  = 60.0f;
        constexpr float SLOT_W  = 60.0f;
        constexpr float GAP     = 4.0f;
        constexpr int   SLOTS   = game::weapon::WeaponBar::SLOTS;

        const float WIN_H = SLOTS * SLOT_H + (SLOTS - 1) * GAP + 20.0f;
        const float WIN_W = SLOT_W + 18.0f;

        ImGui::SetNextWindowPos({8.0f, 178.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({WIN_W, WIN_H}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);   // 全透明，完全用 DrawList 绘制

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {GAP, GAP});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8.0f, 8.0f});
        ImGui::Begin("##dnf_weapon_bar", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoSavedSettings);

        auto* dl  = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();

        // 面板底层：暗钢色背景
        dl->AddRectFilled({wp.x, wp.y}, {wp.x + WIN_W, wp.y + WIN_H},
            IM_COL32(15, 12, 8, 200), 5.0f);
        dl->AddRect({wp.x, wp.y}, {wp.x + WIN_W, wp.y + WIN_H},
            IM_COL32(160, 130, 50, 160), 5.0f, 0, 1.2f);

        for (int i = 0; i < SLOTS; ++i)
        {
            const auto &slot     = m_weaponBar.getSlot(i);
            const bool is_active = (i == m_weaponBar.getActiveIndex());

            float sy = wp.y + 8.0f + i * (SLOT_H + GAP);
            float sx = wp.x + 9.0f;

            // ── 槽背景色 ──────────────────────────────────────────────
            ImU32 slotBg  = is_active
                ? IM_COL32(60, 48, 10, 240)
                : IM_COL32(22, 18, 12, 220);
            dl->AddRectFilled({sx, sy}, {sx + SLOT_W, sy + SLOT_H}, slotBg, 4.0f);

            // ── 金属内纹（斜纹增加质感）──
            for (int li = 0; li < static_cast<int>(SLOT_H); li += 3)
                dl->AddLine({sx + 1, sy + li}, {sx + SLOT_W - 1, sy + li},
                    IM_COL32(40, 32, 15, 40), 0.7f);

            // ── 槽内容 ──────────────────────────────────────────────
            float cx = sx + SLOT_W * 0.5f;
            float cy = sy + SLOT_H * 0.5f;

            if (slot.isEmpty())
            {
                // 空槽：中央编号 + 细十字引导线
                dl->AddLine({cx - 8, cy}, {cx + 8, cy}, IM_COL32(80, 65, 30, 120), 0.8f);
                dl->AddLine({cx, cy - 8}, {cx, cy + 8}, IM_COL32(80, 65, 30, 120), 0.8f);
                char n[4]; snprintf(n, sizeof(n), "%d", i + 1);
                ImVec2 ns = ImGui::CalcTextSize(n);
                dl->AddText({cx - ns.x * 0.5f, sy + 4}, IM_COL32(100, 80, 30, 160), n);
            }
            else
            {
                const auto* def = game::weapon::getWeaponDef(slot.item->id);
                // 武器图标：用几何图形表示（巨剑/斧/矛等）
                float iR = SLOT_W * 0.30f;
                // 剑身
                dl->AddLine({cx, sy + 6}, {cx, sy + SLOT_H - 8},
                    IM_COL32(210, 185, 100, 240), 3.5f);
                // 护手
                dl->AddLine({cx - iR * 0.7f, cy - 2}, {cx + iR * 0.7f, cy - 2},
                    IM_COL32(190, 160, 70, 220), 2.5f);
                // 剑尖高光
                dl->AddTriangleFilled(
                    {cx - 3, sy + 7}, {cx + 3, sy + 7}, {cx, sy + 2},
                    IM_COL32(255, 235, 140, 200));
                // 剑柄
                dl->AddRectFilled({cx - 3, sy + SLOT_H - 14},
                    {cx + 3, sy + SLOT_H - 7}, IM_COL32(140, 100, 40, 200), 1.5f);

                // 右下角武器名（缩写）
                if (def)
                {
                    const char* lbl = def->icon_label.empty() ? "?" : def->icon_label.c_str();
                    ImVec2 ls = ImGui::CalcTextSize(lbl);
                    dl->AddText({sx + SLOT_W - ls.x - 3, sy + SLOT_H - ls.y - 3},
                        IM_COL32(220, 195, 100, 200), lbl);
                }
            }

            // ── 主边框（双层 DNF 金属感）──────────────────────────────
            if (is_active)
            {
                // 激活槽：金色发光边框
                dl->AddRect({sx - 2, sy - 2}, {sx + SLOT_W + 2, sy + SLOT_H + 2},
                    IM_COL32(255, 200, 50, 100), 5.0f, 0, 3.5f);
                dl->AddRect({sx, sy}, {sx + SLOT_W, sy + SLOT_H},
                    IM_COL32(255, 220, 80, 230), 4.0f, 0, 1.8f);
                // 左上三角激活标记
                dl->AddTriangleFilled(
                    {sx, sy}, {sx + 10, sy}, {sx, sy + 10},
                    IM_COL32(255, 220, 80, 220));
            }
            else
            {
                dl->AddRect({sx, sy}, {sx + SLOT_W, sy + SLOT_H},
                    IM_COL32(140, 110, 40, 160), 4.0f, 0, 1.2f);
            }

            // 四角铆钉
            for (int cx2 = 0; cx2 < 2; ++cx2) for (int cy2 = 0; cy2 < 2; ++cy2)
            {
                float rx = sx + (cx2 ? SLOT_W - 5 : 5);
                float ry = sy + (cy2 ? SLOT_H - 5 : 5);
                dl->AddCircleFilled({rx, ry}, 2.8f, IM_COL32(160, 130, 50, 200));
                dl->AddCircleFilled({rx, ry}, 1.5f, IM_COL32(220, 190, 90, 240));
            }

            // ── InvisibleButton 用于拖放交互 ──────────────────────────
            ImGui::SetCursorScreenPos({sx, sy});
            char btnId[16]; snprintf(btnId, sizeof(btnId), "##wslot%d", i);
            ImGui::InvisibleButton(btnId, {SLOT_W, SLOT_H});

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("BAG_SLOT"))
                {
                    const auto drag = *static_cast<const InventoryDragSlot *>(payload->Data);
                    if (drag.owner == static_cast<int>(InventoryOwner::Player))
                        m_weaponBar.equipFromInventory(i, drag.index, m_inventory);
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
                m_weaponBar.unequipToInventory(i, m_inventory);

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
                }
                ImGui::TextDisabled("%s", locale::T("weapon_bar.dblclick_unequip").c_str());
                ImGui::EndTooltip();
            }
        }

        // 底部切换提示（小字）
        ImVec2 hintSz = ImGui::CalcTextSize("↕");
        dl->AddText({wp.x + (WIN_W - hintSz.x) * 0.5f, wp.y + WIN_H - hintSz.y - 3},
            IM_COL32(140, 115, 50, 160), "↕");

        ImGui::End();
        ImGui::PopStyleVar(2);
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
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("BAG_SLOT"))
                {
                    const auto drag = *static_cast<const InventoryDragSlot*>(p->Data);
                    if (drag.owner == static_cast<int>(InventoryOwner::Player))
                    {
                        auto& src  = m_inventory.getSlot(drag.index);
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

    void GameScene::renderEquipmentPage()
    {
        constexpr float SLOT = 72.0f;
        constexpr int COLS = 2;

        ImGui::TextDisabled("拖拽背包装备到槽位，右键可卸下到背包");
        ImGui::Separator();
        ImGui::Spacing();

        auto *dl = ImGui::GetWindowDrawList();
        for (int i = 0; i < game::inventory::EquipmentLoadout::SLOT_COUNT; ++i)
        {
            if (i % COLS != 0)
                ImGui::SameLine();

            auto &slot = m_equipmentLoadout.getSlot(i);
            const auto slotType = m_equipmentLoadout.getSlotType(i);
            const bool occupied = !slot.isEmpty();

            ImGui::PushStyleColor(ImGuiCol_Button,
                occupied ? ImVec4(0.28f, 0.22f, 0.14f, 1.0f)
                         : ImVec4(0.12f, 0.12f, 0.16f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.36f, 0.30f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.44f, 0.34f, 0.20f, 1.0f));

            char btnId[24];
            snprintf(btnId, sizeof(btnId), "##eq%d", i);
            ImGui::Button(btnId, {SLOT, SLOT});

            ImVec2 bmin = ImGui::GetItemRectMin();
            ImVec2 bmax = ImGui::GetItemRectMax();
            dl->AddRect(bmin, bmax, IM_COL32(210, 176, 96, 200), 4.0f, 0, 1.3f);

            const char *slotLabel = game::inventory::EquipmentLoadout::slotTypeLabel(slotType);
            ImVec2 lsz = ImGui::CalcTextSize(slotLabel);
            dl->AddText({bmin.x + (SLOT - lsz.x) * 0.5f, bmax.y + 3.0f}, IM_COL32(170, 155, 120, 220), slotLabel);

            if (occupied)
            {
                drawItemIcon(dl, bmin, bmax, slot.item->category, slot.count);
            }
            else
            {
                const char* plus = "+";
                ImVec2 psz = ImGui::CalcTextSize(plus);
                dl->AddText({bmin.x + (SLOT - psz.x) * 0.5f, bmin.y + (SLOT - psz.y) * 0.5f},
                            IM_COL32(140, 130, 98, 160), plus);
            }

            if (occupied && ImGui::IsItemClicked(ImGuiMouseButton_Right))
                m_equipmentLoadout.unequipToInventory(i, m_mechInventory);

            if (occupied && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                ImGui::SetDragDropPayload("EQUIP_SLOT", &i, sizeof(int));
                ImGui::TextUnformatted(slot.item->name.c_str());
                ImGui::TextDisabled("拖到背包取下 | 拖到装备槽换位");
                ImGui::EndDragDropSource();
            }

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("BAG_SLOT"))
                {
                    const auto drag = *static_cast<const InventoryDragSlot *>(p->Data);
                    if (drag.owner == static_cast<int>(InventoryOwner::Mech))
                        m_equipmentLoadout.equipFromInventory(i, drag.index, m_mechInventory);
                }
                if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("EQUIP_SLOT"))
                {
                    int src = *static_cast<const int *>(p->Data);
                    if (src != i)
                    {
                        auto &srcSlot = m_equipmentLoadout.getSlot(src);
                        bool srcToDstOk = srcSlot.isEmpty() || m_equipmentLoadout.canEquipInSlot(*srcSlot.item, i);
                        bool dstToSrcOk = slot.isEmpty() || m_equipmentLoadout.canEquipInSlot(*slot.item, src);
                        if (srcToDstOk && dstToSrcOk)
                            std::swap(srcSlot, slot);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("槽位: %s", slotLabel);
                ImGui::Separator();
                if (occupied)
                {
                    ImGui::TextUnformatted(slot.item->name.c_str());
                    ImGui::TextDisabled("右键卸下到背包");
                }
                else
                {
                    ImGui::TextDisabled("仅可放置对应类型装备");
                }
                ImGui::EndTooltip();
            }

            ImGui::PopStyleColor(3);
        }
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

        const bool showMechStorage = canAccessMechInventory();
        const float INV_W   = COLS * SLOT + (COLS - 1) * GAP;
        const float SKILL_W = SK_COLS * SLOT + (SK_COLS - 1) * GAP;
        const float WIN_W   = INV_W + PANEL_GAP + SKILL_W + 20.0f;

        ImVec2 disp = ImGui::GetIO().DisplaySize;
        const float desiredH = showMechStorage ? 760.0f : (ROWS * SLOT + (ROWS - 1) * GAP + 120.0f);
        const float WIN_H = std::min(desiredH, std::max(520.0f, disp.y - 60.0f));
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
                const std::string controlLabel = m_isPlayerInMech ? m_controlLabelMech
                    : (m_possessedMonster ? m_controlLabelPossessed : m_controlLabelPlayer);
                ImGui::Text("当前控制: %s", controlLabel.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("仓储视图: %s", showMechStorage ? "人物 + 机甲" : "仅人物");
                ImGui::Separator();

                auto renderInventoryPanel = [&](const char* childId,
                                                const char* title,
                                                game::inventory::Inventory& inventory,
                                                InventoryOwner owner,
                                                float height)
                {
                    const bool isMechPanel = owner == InventoryOwner::Mech;
                    const ImVec4 panelBg    = isMechPanel ? ImVec4(0.15f, 0.11f, 0.07f, 0.96f)
                                                          : ImVec4(0.08f, 0.12f, 0.20f, 0.96f);
                    const ImVec4 panelTitle = isMechPanel ? ImVec4(0.96f, 0.84f, 0.62f, 1.0f)
                                                          : ImVec4(0.75f, 0.90f, 1.0f, 1.0f);
                    const ImU32 borderCol   = isMechPanel ? IM_COL32(210, 172, 90, 220)
                                                          : IM_COL32(100, 170, 255, 220);
                    const ImU32 glowCol     = isMechPanel ? IM_COL32(255, 180, 70, 45)
                                                          : IM_COL32(90, 170, 255, 38);

                    ImGui::PushStyleColor(ImGuiCol_ChildBg, panelBg);
                    ImGui::BeginChild(childId, {INV_W, height}, true);

                    ImDrawList* panelDl = ImGui::GetWindowDrawList();
                    ImVec2 panelMin = ImGui::GetWindowPos();
                    ImVec2 panelMax = {panelMin.x + ImGui::GetWindowSize().x, panelMin.y + ImGui::GetWindowSize().y};
                    if (isMechPanel)
                    {
                        const float bevel = 12.0f;
                        const ImVec2 mechOuter[] = {
                            {panelMin.x + bevel, panelMin.y},
                            {panelMax.x - bevel, panelMin.y},
                            {panelMax.x, panelMin.y + bevel},
                            {panelMax.x, panelMax.y - bevel},
                            {panelMax.x - bevel, panelMax.y},
                            {panelMin.x + bevel, panelMax.y},
                            {panelMin.x, panelMax.y - bevel},
                            {panelMin.x, panelMin.y + bevel},
                        };
                        const ImVec2 mechInner[] = {
                            {panelMin.x + bevel + 6.0f, panelMin.y + 6.0f},
                            {panelMax.x - bevel - 6.0f, panelMin.y + 6.0f},
                            {panelMax.x - 6.0f, panelMin.y + bevel + 6.0f},
                            {panelMax.x - 6.0f, panelMax.y - bevel - 6.0f},
                            {panelMax.x - bevel - 6.0f, panelMax.y - 6.0f},
                            {panelMin.x + bevel + 6.0f, panelMax.y - 6.0f},
                            {panelMin.x + 6.0f, panelMax.y - bevel - 6.0f},
                            {panelMin.x + 6.0f, panelMin.y + bevel + 6.0f},
                        };
                        panelDl->AddPolyline(mechOuter, 8, borderCol, ImDrawFlags_Closed, 2.2f);
                        panelDl->AddPolyline(mechInner, 8, IM_COL32(120, 88, 42, 210), ImDrawFlags_Closed, 1.1f);
                        panelDl->AddRectFilled({panelMin.x + 10.0f, panelMin.y + 10.0f}, {panelMax.x - 10.0f, panelMin.y + 38.0f}, glowCol, 0.0f);
                        panelDl->AddRectFilled({panelMin.x + 14.0f, panelMin.y + 14.0f}, {panelMax.x - 14.0f, panelMin.y + 18.0f}, IM_COL32(255, 214, 120, 30), 0.0f);

                        const ImVec2 rivets[] = {
                            {panelMin.x + 16.0f, panelMin.y + 16.0f},
                            {panelMax.x - 16.0f, panelMin.y + 16.0f},
                            {panelMin.x + 16.0f, panelMax.y - 16.0f},
                            {panelMax.x - 16.0f, panelMax.y - 16.0f},
                        };
                        for (const ImVec2& rivet : rivets)
                        {
                            panelDl->AddCircleFilled(rivet, 4.0f, IM_COL32(118, 92, 50, 230));
                            panelDl->AddCircleFilled({rivet.x - 1.0f, rivet.y - 1.0f}, 1.8f, IM_COL32(220, 188, 110, 220));
                        }
                    }
                    else
                    {
                        panelDl->AddRect(panelMin, panelMax, borderCol, 8.0f, 0, 1.5f);
                        panelDl->AddRectFilled({panelMin.x + 8.0f, panelMin.y + 8.0f}, {panelMax.x - 8.0f, panelMin.y + 34.0f}, glowCol, 6.0f);
                    }

                    ImGui::PushStyleColor(ImGuiCol_Text, panelTitle);
                    ImGui::TextUnformatted(title);
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    ImGui::TextDisabled("[%s]", isMechPanel ? "MECH" : "PLAYER");
                    ImGui::TextDisabled("独立存放，拖拽可在人物与机甲仓之间转移");
                    ImGui::Separator();

                    for (int row = 0; row < ROWS; ++row)
                    {
                        for (int col = 0; col < COLS; ++col)
                        {
                            if (col > 0) ImGui::SameLine();

                            int idx = row * COLS + col;
                            auto &slot = inventory.getSlot(idx);

                            bool is_weapon = !slot.isEmpty() &&
                                slot.item->category == game::inventory::ItemCategory::Weapon;
                            bool is_star = !slot.isEmpty() &&
                                slot.item->category == game::inventory::ItemCategory::StarSkill;
                            bool is_equipment = !slot.isEmpty() &&
                                slot.item->category == game::inventory::ItemCategory::Equipment;

                            if (is_weapon)
                                ImGui::PushStyleColor(ImGuiCol_Button, {0.30f, 0.18f, 0.10f, 1.0f});
                            else if (is_star)
                                ImGui::PushStyleColor(ImGuiCol_Button,
                                    isMechPanel ? ImVec4(0.22f, 0.18f, 0.10f, 1.0f)
                                                : ImVec4(0.12f, 0.22f, 0.38f, 1.0f));
                            else if (is_equipment)
                                ImGui::PushStyleColor(ImGuiCol_Button,
                                    isMechPanel ? ImVec4(0.34f, 0.24f, 0.10f, 1.0f)
                                                : ImVec4(0.19f, 0.23f, 0.28f, 1.0f));
                            else
                                ImGui::PushStyleColor(ImGuiCol_Button,
                                    isMechPanel ? ImVec4(0.22f, 0.16f, 0.10f, 1.0f)
                                                : ImVec4(0.16f, 0.19f, 0.30f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                isMechPanel ? ImVec4(0.48f, 0.34f, 0.16f, 1.0f)
                                            : ImVec4(0.26f, 0.38f, 0.58f, 1.0f));

                            char label[64];
                            const char* prefix = owner == InventoryOwner::Mech ? "m" : "p";
                            if (slot.isEmpty())
                                snprintf(label, sizeof(label), "##%s_s%d", prefix, idx);
                            else if (is_weapon)
                            {
                                const auto *def = game::weapon::getWeaponDef(slot.item->id);
                                snprintf(label, sizeof(label), "%s##%s_s%d",
                                    def ? def->icon_label.c_str() : "[W]", prefix, idx);
                            }
                            else
                                snprintf(label, sizeof(label), "##%s_s%d", prefix, idx);

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

                            // 人物背包右键星技珠 → 放入第一个空星技槽
                            if (owner == InventoryOwner::Player && is_star && ImGui::IsItemClicked(ImGuiMouseButton_Right))
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
                                InventoryDragSlot drag{static_cast<int>(owner), idx};
                                ImGui::SetDragDropPayload("BAG_SLOT", &drag, sizeof(drag));
                                ImGui::Text("%s背包", inventoryOwnerLabel(owner));
                                ImGui::TextUnformatted(slot.item->name.c_str());
                                if (owner == InventoryOwner::Player && is_weapon)
                                    ImGui::TextDisabled("%s", locale::T("weapon_bar.drag_hint").c_str());
                                if (owner == InventoryOwner::Player && is_star)
                                    ImGui::TextDisabled("右键快速装备 | 拖入技能格子");
                                if (owner == InventoryOwner::Mech && is_equipment)
                                    ImGui::TextDisabled("拖入右侧机甲装备格子进行装配");
                                ImGui::EndDragDropSource();
                            }

                            // 拖放目标：接受背包互转 / 星技槽 / 装备槽
                            if (ImGui::BeginDragDropTarget())
                            {
                                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("BAG_SLOT"))
                                {
                                    const auto drag = *static_cast<const InventoryDragSlot*>(p->Data);
                                    InventoryOwner srcOwner = static_cast<InventoryOwner>(drag.owner);
                                    if (!(srcOwner == owner && drag.index == idx))
                                    {
                                        auto& sourceInventory = srcOwner == InventoryOwner::Mech ? m_mechInventory : m_inventory;
                                        std::swap(slot, sourceInventory.getSlot(drag.index));
                                    }
                                }
                                if (slot.isEmpty() && owner == InventoryOwner::Player)
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
                                }
                                if (slot.isEmpty() && owner == InventoryOwner::Mech)
                                {
                                    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("EQUIP_SLOT"))
                                    {
                                        int srcEquipIdx = *static_cast<const int*>(p->Data);
                                        auto& srcEquipSlot = m_equipmentLoadout.getSlot(srcEquipIdx);
                                        if (!srcEquipSlot.isEmpty())
                                        {
                                            slot.item = srcEquipSlot.item;
                                            slot.count = srcEquipSlot.count;
                                            srcEquipSlot.item.reset();
                                            srcEquipSlot.count = 0;
                                        }
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }

                            // Tooltip
                            if (!slot.isEmpty() && ImGui::IsItemHovered())
                            {
                                ImGui::BeginTooltip();
                                ImGui::Text("%s背包", inventoryOwnerLabel(owner));
                                ImGui::Separator();
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
                                    if (owner == InventoryOwner::Player)
                                    {
                                        ImGui::TextDisabled("右键 → 装入技能格子");
                                        ImGui::TextDisabled("拖拽 → 放到技能格子指定槽");
                                    }
                                    else
                                    {
                                        ImGui::TextDisabled("可拖回人物背包后再装备");
                                    }
                                }
                                else if (is_equipment)
                                {
                                    ImGui::TextDisabled("机甲装备");
                                    ImGui::Separator();
                                    if (owner == InventoryOwner::Mech)
                                        ImGui::TextDisabled("拖拽到右侧机甲装备格子进行装配");
                                    else if (showMechStorage)
                                        ImGui::TextDisabled("拖拽到机甲背包后再装配");
                                    else
                                        ImGui::TextDisabled("靠近机甲后可转移到机甲背包");
                                }
                                else
                                {
                                    ImGui::TextDisabled("%s: %d / %d",
                                        locale::T("inventory.quantity").c_str(), slot.count, slot.item->max_stack);
                                }
                                ImGui::EndTooltip();
                            }

                            ImGui::PopStyleColor(2);
                        }
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                };

                // ── 左侧：人物/机甲双层背包 ────────────────────────────
                ImVec2 columnsStart = ImGui::GetCursorPos();
                const float leftAvailH = ImGui::GetContentRegionAvail().y;
                const float playerPanelH = showMechStorage ? (leftAvailH - PANEL_GAP) * 0.5f : leftAvailH;
                const float mechPanelH = showMechStorage ? (leftAvailH - PANEL_GAP) - playerPanelH : 0.0f;

                ImGui::BeginGroup();
                renderInventoryPanel("##player_inv_grid", "人物背包", m_inventory, InventoryOwner::Player, playerPanelH);
                if (showMechStorage)
                {
                    ImGui::Spacing();
                    renderInventoryPanel("##mech_inv_grid", "机甲背包", m_mechInventory, InventoryOwner::Mech, mechPanelH);
                }
                ImGui::EndGroup();

                // ── 右侧：技能格子 ────────────────────────────────────
                ImGui::SetCursorPos({columnsStart.x + INV_W + PANEL_GAP, columnsStart.y});
                ImGui::BeginChild("##skill_panel", {SKILL_W, 0.0f}, false, 0);

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
                        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("BAG_SLOT"))
                        {
                            const auto drag = *static_cast<const InventoryDragSlot*>(p->Data);
                            if (drag.owner == static_cast<int>(InventoryOwner::Player))
                            {
                                auto& src  = m_inventory.getSlot(drag.index);
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

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92f, 0.84f, 0.62f, 1.0f));
                ImGui::TextUnformatted("◆ 机甲装备格子");
                ImGui::PopStyleColor();
                ImGui::Separator();
                ImGui::Spacing();

                if (!showMechStorage)
                {
                    ImGui::TextDisabled("进入机甲或靠近机甲后显示机甲背包与装备槽");
                }
                else
                {
                    constexpr int EQ_COLS = 2;
                    constexpr int EQ_COUNT = game::inventory::EquipmentLoadout::SLOT_COUNT;
                    for (int eq = 0; eq < EQ_COUNT; ++eq)
                    {
                        if (eq % EQ_COLS != 0) ImGui::SameLine();

                        auto &eqSlot = m_equipmentLoadout.getSlot(eq);
                        const auto eqType = m_equipmentLoadout.getSlotType(eq);
                        const bool occupied = !eqSlot.isEmpty();

                        ImGui::PushStyleColor(ImGuiCol_Button,
                            occupied ? ImVec4(0.30f, 0.24f, 0.14f, 1.0f)
                                     : ImVec4(0.12f, 0.10f, 0.08f, 0.95f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImVec4(0.42f, 0.32f, 0.20f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                            ImVec4(0.48f, 0.36f, 0.22f, 1.0f));

                        char eqId[16];
                        snprintf(eqId, sizeof(eqId), "##eqp%d", eq);
                        ImGui::Button(eqId, {SLOT, SLOT});

                        ImVec2 bmin = ImGui::GetItemRectMin();
                        ImVec2 bmax = ImGui::GetItemRectMax();
                        skillDrawList->AddRect(bmin, bmax, IM_COL32(205, 175, 94, 200), 4.0f, 0, 1.2f);

                        if (occupied)
                            drawItemIcon(skillDrawList, bmin, bmax, eqSlot.item->category, eqSlot.count);
                        else
                        {
                            const char* plus = "+";
                            ImVec2 ps = ImGui::CalcTextSize(plus);
                            skillDrawList->AddText({(bmin.x + bmax.x - ps.x) * 0.5f, (bmin.y + bmax.y - ps.y) * 0.5f},
                                IM_COL32(150, 130, 90, 180), plus);
                        }

                        const char* typeLabel = game::inventory::EquipmentLoadout::slotTypeLabel(eqType);
                        ImVec2 ts = ImGui::CalcTextSize(typeLabel);
                        skillDrawList->AddText({(bmin.x + bmax.x - ts.x) * 0.5f, bmax.y + 2.0f},
                            IM_COL32(182, 164, 130, 210), typeLabel);

                        if (occupied && ImGui::IsItemClicked(ImGuiMouseButton_Right))
                            m_equipmentLoadout.unequipToInventory(eq, m_mechInventory);

                        if (occupied && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                        {
                            ImGui::SetDragDropPayload("EQUIP_SLOT", &eq, sizeof(int));
                            ImGui::TextUnformatted(eqSlot.item->name.c_str());
                            ImGui::TextDisabled("右键卸下 | 拖到背包取下 | 拖到装备槽换位");
                            ImGui::EndDragDropSource();
                        }

                        if (ImGui::BeginDragDropTarget())
                        {
                            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("BAG_SLOT"))
                            {
                                const auto drag = *static_cast<const InventoryDragSlot*>(p->Data);
                                if (drag.owner == static_cast<int>(InventoryOwner::Mech))
                                    m_equipmentLoadout.equipFromInventory(eq, drag.index, m_mechInventory);
                            }
                            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("EQUIP_SLOT"))
                            {
                                int srcEq = *static_cast<const int*>(p->Data);
                                if (srcEq != eq)
                                {
                                    auto &srcSlot = m_equipmentLoadout.getSlot(srcEq);
                                    bool srcToDstOk = srcSlot.isEmpty() || m_equipmentLoadout.canEquipInSlot(*srcSlot.item, eq);
                                    bool dstToSrcOk = eqSlot.isEmpty() || m_equipmentLoadout.canEquipInSlot(*eqSlot.item, srcEq);
                                    if (srcToDstOk && dstToSrcOk)
                                        std::swap(srcSlot, eqSlot);
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        if (ImGui::IsItemHovered())
                        {
                            ImGui::BeginTooltip();
                            ImGui::Text("槽位: %s", typeLabel);
                            ImGui::Separator();
                            if (occupied)
                            {
                                ImGui::TextUnformatted(eqSlot.item->name.c_str());
                                ImGui::TextDisabled("右键卸下到背包");
                            }
                            else
                            {
                                ImGui::TextDisabled("仅可放置对应类型装备");
                            }
                            ImGui::EndTooltip();
                        }

                        ImGui::PopStyleColor(3);
                    }
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
        m_player = actor_manager->createActor(m_playerActorKey);

        glm::vec2 startPos = {0.0f, 56.0f};

        auto* transform = m_player->addComponent<engine::component::TransformComponent>(startPos);

        const std::string frameJsonPath = "assets/textures/Characters/gundom.json";
        game::animation::FrameAnimationSet animationSet;
        if (!game::animation::loadFrameAnimationSet(frameJsonPath, animationSet))
            animationSet = game::animation::makeDefaultGundomAnimationSet();

        if (animationSet.texturePath.empty())
            animationSet.texturePath = "assets/textures/Characters/gundom.png";

        const auto initialRect = animationSet.initialSourceRect().value_or(
            engine::utils::FRect{{241.0f, 1625.0f}, {241.0f, 125.0f}});

        // ── Gundam 精灵表：优先读取帧编辑器 JSON 中声明的实际贴图路径 ──
        m_player->addComponent<engine::component::SpriteComponent>(
            animationSet.texturePath,
            engine::utils::Alignment::CENTER,
            initialRect);

        auto* ctrl = m_player->addComponent<engine::component::ControllerComponent>(20.0f, 28.0f);
        ctrl->setGroundAcceleration(80.0f);
        ctrl->setAirAcceleration(12.0f);
        ctrl->setGroundBand(16.0f, 96.0f);
        ctrl->setJetpackEnabled(false);

        auto* anim = m_player->addComponent<engine::component::AnimationComponent>(241.0f, 125.0f);

        for (const auto& [clipName, clip] : animationSet.clips)
            anim->addClip(clipName, clip);

        anim->play("idle");

        // 物理体（shadow footprint）
        constexpr float PPM = engine::world::WorldConfig::PIXELS_PER_METER;
        constexpr float kShadowHalfW = 24.0f * 0.5f / PPM;
        constexpr float kShadowHalfD = 7.0f  * 0.5f / PPM;
        b2BodyId bodyId = physics_manager->createDynamicBody(
            {startPos.x / PPM, startPos.y / PPM}, {kShadowHalfW, kShadowHalfD}, m_player);
        m_player->addComponent<engine::component::PhysicsComponent>(bodyId, physics_manager.get());

        m_player->addComponent<game::component::AttributeComponent>();

        _context.getCamera().setFollowTarget(&transform->getPosition(), 5.0f);
        m_zoomSliderValue = 2.5f;
        _context.getCamera().setZoom(2.5f);
        _context.getCamera().setPseudo3DVerticalScale(1.0f);
        _context.getCamera().setLockY(true, 40.0f);
        {
            const glm::vec2& vp = _context.getCamera().getViewportSize();
            _context.getCamera().setPosition({
                transform->getPosition().x - vp.x * 0.5f,
                40.0f - vp.y * 0.5f
            });
        }
    }

    void GameScene::executeCommand()
    {
        std::string command = m_commandBuffer.data();
        command.erase(std::remove_if(command.begin(), command.end(), [](unsigned char c) {
            return std::isspace(c) != 0;
        }), command.end());

        std::transform(command.begin(), command.end(), command.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (!command.empty() && command.front() == 'r')
            command.erase(command.begin());

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
        // 直接生成在玩家右侧，避免高空空投导致“看不到召唤物”。
        glm::vec2 spawnPos = {playerPos.x + 140.0f, playerPos.y};

        if (!m_mech)
        {
            m_mech = actor_manager->createActor(m_mechActorKey);
            auto* mechTransform = m_mech->addComponent<engine::component::TransformComponent>(spawnPos);
            mechTransform->setScale({1.0f, 1.0f});

            // 机甲展示改为使用 gundom.json 动画资源（与人物同源，动作可配置）。
            const std::string frameJsonPath = "assets/textures/Characters/gundom.json";
            game::animation::FrameAnimationSet animationSet;
            if (!game::animation::loadFrameAnimationSet(frameJsonPath, animationSet))
                animationSet = game::animation::makeDefaultGundomAnimationSet();
            if (animationSet.texturePath.empty())
                animationSet.texturePath = "assets/textures/Characters/gundom.png";

            const auto initialRect = animationSet.initialSourceRect().value_or(
                engine::utils::FRect{{241.0f, 1625.0f}, {241.0f, 125.0f}});
            m_mech->addComponent<engine::component::SpriteComponent>(
                animationSet.texturePath,
                engine::utils::Alignment::CENTER,
                initialRect);
            auto* mechAnim = m_mech->addComponent<engine::component::AnimationComponent>(241.0f, 125.0f);
            for (const auto& [clipName, clip] : animationSet.clips)
                mechAnim->addClip(clipName, clip);
            mechAnim->play("idle");

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
            if (!m_mech->getComponent<engine::component::AnimationComponent>())
            {
                const std::string frameJsonPath = "assets/textures/Characters/gundom.json";
                game::animation::FrameAnimationSet animationSet;
                if (!game::animation::loadFrameAnimationSet(frameJsonPath, animationSet))
                    animationSet = game::animation::makeDefaultGundomAnimationSet();
                auto* mechAnim = m_mech->addComponent<engine::component::AnimationComponent>(241.0f, 125.0f);
                for (const auto& [clipName, clip] : animationSet.clips)
                    mechAnim->addClip(clipName, clip);
                mechAnim->play("idle");
            }
            if (auto* physics = m_mech->getComponent<engine::component::PhysicsComponent>())
            {
                physics->setWorldPosition(spawnPos);
                physics->setVelocity({0.0f, 0.0f});
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
        updateMechFlightCapability();

        spdlog::info("指令 001 已执行：机甲已生成在玩家旁边，坐标 ({:.1f}, {:.1f})", spawnPos.x, spawnPos.y);
    }

    void GameScene::updateMechFlightCapability()
    {
        if (!m_mech)
            return;

        auto *mechController = m_mech->getComponent<engine::component::ControllerComponent>();
        if (!mechController)
            return;

        m_mechFlightEngineInstalled = m_equipmentLoadout.hasItemId("mech_flight_engine");

        // 仅在“驾驶机甲 + 已安装飞行引擎”时启用飞行能力
        mechController->setJetpackEnabled(m_isPlayerInMech && m_mechFlightEngineInstalled);
    }

    void GameScene::updateEquipmentAttributeBonuses()
    {
        if (!m_player)
            return;

        auto *attr = m_player->getComponent<game::component::AttributeComponent>();
        if (!attr)
            return;

        // 先清理旧装备词条，再按当前已装备物品重建，避免换装残留。
        attr->removeAllModifiers("equip_mech_flight_engine");
        attr->removeAllModifiers("equip_exo_armor");
        attr->removeAllModifiers("equip_kinetic_actuator");
        attr->removeAllModifiers("equip_gyro_stabilizer");

        if (m_equipmentLoadout.hasItemId("mech_flight_engine"))
        {
            attr->addModifier({"equip_mech_flight_engine", game::component::StatType::MaxStarEnergy, 20.0f, 0.0f, -1.0f});
        }
        if (m_equipmentLoadout.hasItemId("exo_armor"))
        {
            attr->addModifier({"equip_exo_armor", game::component::StatType::MaxHp, 35.0f, 0.0f, -1.0f});
            attr->addModifier({"equip_exo_armor", game::component::StatType::Defense, 8.0f, 0.0f, -1.0f});
        }
        if (m_equipmentLoadout.hasItemId("kinetic_actuator"))
        {
            attr->addModifier({"equip_kinetic_actuator", game::component::StatType::Attack, 12.0f, 0.0f, -1.0f});
            attr->addModifier({"equip_kinetic_actuator", game::component::StatType::Speed, 0.0f, 0.10f, -1.0f});
        }
        if (m_equipmentLoadout.hasItemId("gyro_stabilizer"))
        {
            attr->addModifier({"equip_gyro_stabilizer", game::component::StatType::CritRate, 0.08f, 0.0f, -1.0f});
            attr->addModifier({"equip_gyro_stabilizer", game::component::StatType::JumpPower, 0.0f, 0.08f, -1.0f});
        }
    }

    void GameScene::tryEnterMech()
    {
        if (!m_mech || m_isPlayerInMech || !m_player || m_possessedMonster)
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
        updateMechFlightCapability();
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
        updateMechFlightCapability();
        spdlog::info("驾驶员已离开机甲");
    }

    void GameScene::tryPossessNearestMonster()
    {
        if (!m_monsterManager || !m_player || m_isPlayerInMech || m_possessedMonster)
            return;

        const glm::vec2 origin = getActorWorldPosition(m_player);
        auto *candidate = m_monsterManager->findNearestMonster(origin, 180.0f);
        if (!candidate)
            return;

        auto *playerController = m_player->getComponent<engine::component::ControllerComponent>();
        auto *playerPhysics = m_player->getComponent<engine::component::PhysicsComponent>();
        auto *playerTransform = m_player->getComponent<engine::component::TransformComponent>();
        auto *playerSprite = m_player->getComponent<engine::component::SpriteComponent>();
        auto *targetTransform = candidate->getComponent<engine::component::TransformComponent>();
        if (!playerController || !playerPhysics || !playerTransform || !playerSprite || !targetTransform)
            return;

        if (!m_monsterManager->possessMonster(candidate))
            return;

        playerController->setEnabled(false);
        playerPhysics->setVelocity({0.0f, 0.0f});
        playerPhysics->setWorldPosition({-4096.0f, -4096.0f});
        playerTransform->setPosition({-4096.0f, -4096.0f});
        playerSprite->setHidden(true);

        _context.getCamera().setFollowTarget(&targetTransform->getPosition(), 4.0f);
        m_possessedMonster = candidate;
        m_possessionEnergy = 12.0f;
        m_possessionFxTimer = 0.5f;
        m_possessedAttackCooldown = 0.0f;
        m_possessedSkillCooldown = 0.0f;
        m_possessedLastAttackHits = 0;
        spdlog::info("控制权切换：已接管怪物 {}", candidate->getName());
    }

    void GameScene::releasePossessedMonster(bool forced)
    {
        if (!m_possessedMonster || !m_player || !m_monsterManager)
            return;

        auto *playerController = m_player->getComponent<engine::component::ControllerComponent>();
        auto *playerPhysics = m_player->getComponent<engine::component::PhysicsComponent>();
        auto *playerTransform = m_player->getComponent<engine::component::TransformComponent>();
        auto *playerSprite = m_player->getComponent<engine::component::SpriteComponent>();
        if (!playerController || !playerPhysics || !playerTransform || !playerSprite)
            return;

        glm::vec2 restorePos = getActorWorldPosition(m_possessedMonster) + glm::vec2(-28.0f, 0.0f);
        playerSprite->setHidden(false);
        playerTransform->setPosition(restorePos);
        playerPhysics->setWorldPosition(restorePos);
        playerPhysics->setVelocity({0.0f, 0.0f});
        playerController->setEnabled(true);

        m_monsterManager->releasePossessedMonster();
        _context.getCamera().setFollowTarget(&playerTransform->getPosition(), 5.0f);
        m_possessedMonster = nullptr;
        m_possessionEnergy = 0.0f;
        m_possessionFxTimer = forced ? 0.0f : 0.35f;
        m_possessedAttackCooldown = 0.0f;
        m_possessedSkillCooldown = 0.0f;
        m_possessedLastAttackHits = 0;
        spdlog::info("控制权切换：已返回玩家本体{}", forced ? "（强制中断）" : "");
    }

    void GameScene::updatePossession(float dt)
    {
        if (m_possessionFxTimer > 0.0f)
            m_possessionFxTimer = std::max(0.0f, m_possessionFxTimer - dt);

        if (!m_possessedMonster)
            return;

        if (m_possessedMonster->isNeedRemove())
        {
            releasePossessedMonster(true);
            return;
        }

        m_possessionEnergy = std::max(0.0f, m_possessionEnergy - dt);
        if (m_possessionEnergy <= 0.0f)
        {
            releasePossessedMonster(true);
        }
    }

    void GameScene::performPossessedMonsterAttack()
    {
        if (!m_possessedMonster || !m_monsterManager || m_possessedAttackCooldown > 0.0f)
            return;

        auto *controller = m_possessedMonster->getComponent<engine::component::ControllerComponent>();
        auto *physics = m_possessedMonster->getComponent<engine::component::PhysicsComponent>();
        auto *transform = m_possessedMonster->getComponent<engine::component::TransformComponent>();
        auto *ai = m_possessedMonster->getComponent<game::monster::MonsterAIComponent>();
        if (!controller || !physics || !transform || !ai)
            return;

        const float facing = controller->getFacingDirection() == engine::component::ControllerComponent::FacingDirection::Left ? -1.0f : 1.0f;
        const glm::vec2 origin = transform->getPosition();

        float range = 84.0f;
        float halfHeight = 56.0f;
        float cooldown = 0.48f;
        float dashImpulse = 4.2f;
        float upwardImpulse = 0.0f;
        float vfxAge = 0.18f;
        switch (ai->getMonsterType())
        {
        case game::monster::MonsterType::Slime:
            range = 82.0f;
            halfHeight = 48.0f;
            cooldown = 0.50f;
            dashImpulse = 4.0f;
            upwardImpulse = -2.2f;
            break;
        case game::monster::MonsterType::Wolf:
            range = 118.0f;
            halfHeight = 58.0f;
            cooldown = 0.34f;
            dashImpulse = 6.8f;
            break;
        case game::monster::MonsterType::WhiteApe:
            range = 138.0f;
            halfHeight = 86.0f;
            cooldown = 0.72f;
            dashImpulse = 5.4f;
            upwardImpulse = -1.1f;
            vfxAge = 0.24f;
            break;
        }

        std::vector<glm::vec2> defeatPositions;
        const int slain = m_monsterManager->strikeMonstersFrom(
            m_possessedMonster, facing, range, halfHeight, &defeatPositions);

        const glm::vec2 slashCenter = origin + glm::vec2{facing * range * 0.56f, -10.0f};
        emitSlashVFX(slashCenter, facing, vfxAge, range * 0.9f);
        for (const glm::vec2 &pos : defeatPositions)
        {
            for (int i = 0; i < 10; ++i)
            {
                const float t = static_cast<float>(i) / 9.0f;
                glm::vec2 dir = glm::normalize(glm::vec2(facing * (1.1f + t), -0.8f + 1.6f * t));
                emitCombatFragment(
                    pos,
                    dir * (210.0f + 90.0f * t),
                    0.38f + 0.08f * t,
                    3.0f + 2.0f * t
                );
            }
        }

        glm::vec2 vel = physics->getVelocity();
        vel.x = facing * dashImpulse;
        vel.y = std::min(vel.y, upwardImpulse);
        physics->setVelocity(vel);

        m_possessedAttackCooldown = cooldown;
        m_possessedLastAttackHits = slain;
        m_possessionEnergy = std::min(12.0f, m_possessionEnergy + 0.20f + 0.35f * static_cast<float>(slain));
    }

    void GameScene::performPossessedMonsterSkill()
    {
        if (!m_possessedMonster || !m_monsterManager || m_possessedSkillCooldown > 0.0f)
            return;

        auto *controller = m_possessedMonster->getComponent<engine::component::ControllerComponent>();
        auto *physics = m_possessedMonster->getComponent<engine::component::PhysicsComponent>();
        auto *transform = m_possessedMonster->getComponent<engine::component::TransformComponent>();
        auto *ai = m_possessedMonster->getComponent<game::monster::MonsterAIComponent>();
        if (!controller || !physics || !transform || !ai)
            return;

        const float facing = controller->getFacingDirection() == engine::component::ControllerComponent::FacingDirection::Left ? -1.0f : 1.0f;
        const glm::vec2 origin = transform->getPosition();

        float energyCost = 2.6f;
        float cooldown = 3.0f;
        float vfxAge = 0.28f;
        float vfxRadius = 108.0f;
        float strikeRange = 0.0f;
        float strikeHalfHeight = 0.0f;
        float blastRadius = 0.0f;
        bool useBlast = false;
        glm::vec2 effectCenter = origin;
        glm::vec2 newVelocity = physics->getVelocity();

        switch (ai->getMonsterType())
        {
        case game::monster::MonsterType::Slime:
            energyCost = 2.2f;
            cooldown = 2.8f;
            effectCenter = origin + glm::vec2{0.0f, 8.0f};
            useBlast = true;
            blastRadius = 92.0f;
            vfxRadius = 96.0f;
            newVelocity.y = -5.8f;
            break;
        case game::monster::MonsterType::Wolf:
            energyCost = 2.8f;
            cooldown = 3.1f;
            vfxAge = 0.24f;
            vfxRadius = 150.0f;
            effectCenter = origin + glm::vec2{facing * 88.0f, -6.0f};
            strikeRange = 178.0f;
            strikeHalfHeight = 74.0f;
            newVelocity.x = facing * 10.0f;
            newVelocity.y = -3.4f;
            break;
        case game::monster::MonsterType::WhiteApe:
            energyCost = 3.5f;
            cooldown = 4.2f;
            vfxAge = 0.34f;
            vfxRadius = 138.0f;
            effectCenter = origin + glm::vec2{facing * 54.0f, 22.0f};
            useBlast = true;
            blastRadius = 126.0f;
            newVelocity.x = facing * 2.5f;
            break;
        }

        if (m_possessionEnergy < energyCost)
            return;

        int slain = 0;
        std::vector<glm::vec2> defeatPositions;
        if (useBlast)
            slain = m_monsterManager->blastMonstersFrom(m_possessedMonster, effectCenter, blastRadius, &defeatPositions);
        else
            slain = m_monsterManager->strikeMonstersFrom(m_possessedMonster, facing, strikeRange, strikeHalfHeight, &defeatPositions);

        physics->setVelocity(newVelocity);
        m_possessionEnergy = std::max(0.0f, m_possessionEnergy - energyCost);
        m_possessedSkillCooldown = cooldown;
        m_possessedLastAttackHits = slain;
        emitSlashVFX(effectCenter, facing, vfxAge, vfxRadius);
        for (const glm::vec2 &pos : defeatPositions)
        {
            for (int i = 0; i < 14; ++i)
            {
                const float t = static_cast<float>(i) / 13.0f;
                glm::vec2 dir = glm::normalize(glm::vec2(-1.0f + 2.0f * t, -0.65f + 1.3f * t));
                emitCombatFragment(
                    pos,
                    dir * (180.0f + 120.0f * t),
                    0.46f + 0.10f * t,
                    3.2f + 2.3f * t
                );
            }
        }
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
            chunk_manager->rebuildDirtyChunks(2);

        std::vector<glm::vec2> defeatPositions;
        int slain = m_monsterManager
            ? m_monsterManager->slashMonsters(playerPos, facing, weaponDef->range + 20.0f, 72.0f, &defeatPositions)
            : 0;

        emitSlashVFX(slashCenter, facing, 0.20f, weaponDef->range);
        for (const glm::vec2 &pos : defeatPositions)
        {
            for (int i = 0; i < 14; ++i)
            {
                float spread = (-0.9f + 1.8f * (static_cast<float>(i) / 13.0f));
                glm::vec2 dir = glm::normalize(glm::vec2(facing * (1.5f + std::abs(spread) * 1.8f), spread * 1.2f - 0.35f));
                float speed = 240.0f + 26.0f * static_cast<float>(i);
                emitCombatFragment(
                    pos,
                    dir * speed,
                    0.55f + 0.02f * static_cast<float>(i % 4),
                    4.0f + static_cast<float>(i % 3)
                );
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
            chunk_manager->rebuildDirtyChunks(2);

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
                            emitSkillVFX(game::skill::SkillEffect::IceAura, ppos, 0.75f, 0.0f);
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
            emitSkillProjectile(
                game::skill::SkillEffect::FireBlast,
                origin,
                origin,
                origin,
                attackPos,
                delta * kFireProjectileSpeed,
                std::min(flightTime + 0.18f, 1.4f),
                def->range
            );
            emitSkillVFX(game::skill::SkillEffect::FireBlast, origin, 0.18f, -1.0f);

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
            chunk_manager->rebuildDirtyChunks(2);

        int crushed = m_monsterManager
            ? m_monsterManager->crushMonstersInRadius(attackPos, radius + 18.0f)
            : 0;

        m_lastAttackSkillTarget = attackPos;
        m_hasLastAttackSkillTarget = true;
        emitSkillVFX(game::skill::SkillEffect::FireBlast, attackPos, 0.72f, radius);
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
                emitSkillVFX(game::skill::SkillEffect::LightDash, ppos, 0.45f, facing);
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
            case game::skill::SkillEffect::StarJump:   keyHint = "[Q]";    break;
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
    //  角色状态 HUD（DNF 风格）：华丽金属血条 + 羊皮纸属性面板
    // ──────────────────────────────────────────────────────────────────────────
    void GameScene::renderPlayerStatusHUD()
    {
        auto* attr = m_player
            ? m_player->getComponent<game::component::AttributeComponent>()
            : nullptr;
        if (!attr) return;

        const ImVec2 disp   = ImGui::GetIO().DisplaySize;
        const float  T      = static_cast<float>(ImGui::GetTime());
        const float  hpRatio = std::clamp(attr->getHpRatio(), 0.0f, 1.0f);
        const float  seRatio = std::clamp(attr->getStarEnergyRatio(), 0.0f, 1.0f);
        const float  maxHp   = attr->get(game::component::StatType::MaxHp);
        const float  maxSe   = attr->get(game::component::StatType::MaxStarEnergy);

        // ── 血条窗口（屏幕底部居中）────────────────────────────────────────
        constexpr float BAR_W  = 340.0f;
        constexpr float BAR_H  = 20.0f;
        constexpr float WIN_W  = BAR_W + 24.0f;   // 含左右金属端帽
        constexpr float WIN_H  = 56.0f;
        ImGui::SetNextWindowPos({(disp.x - WIN_W) * 0.5f, disp.y - WIN_H - 8.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({WIN_W, WIN_H}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);   // 全透明，完全用 DrawList 绘制
        ImGui::Begin("##dnf_hp_hud", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoInputs     | ImGuiWindowFlags_NoNav  |
            ImGuiWindowFlags_NoSavedSettings);

        auto* dl  = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();

        // ── 辅助 lambda: 绘制单条 DNF 风格进度条 ──────────────────────────
        auto drawDnfBar = [&](float px, float py, float bw, float bh,
                              ImU32 fillA, ImU32 fillB, float ratio,
                              const char* label, float cur, float max,
                              bool pulseLow)
        {
            // 1. 外层阴影
            dl->AddRectFilled({px - 3, py - 3}, {px + bw + 3, py + bh + 3},
                IM_COL32(0, 0, 0, 160), 4.0f);

            // 2. 槽底（暗钢色）
            dl->AddRectFilled({px, py}, {px + bw, py + bh},
                IM_COL32(18, 20, 28, 245), 3.5f);

            // 3. 槽底内纹（细横纹增加金属质感）
            for (int i = 0; i < static_cast<int>(bh); i += 2)
                dl->AddLine({px + 1, py + i + 0.5f}, {px + bw - 1, py + i + 0.5f},
                    IM_COL32(30, 32, 42, 80), 0.8f);

            // 4. 填充渐变（左亮右暗模拟液体感）
            float fw = bw * ratio;
            if (fw > 0.5f)
            {
                dl->AddRectFilled({px, py}, {px + fw, py + bh}, fillA, 3.5f);
                // 高光中线
                dl->AddLine({px + 2, py + bh * 0.32f}, {px + fw - 2, py + bh * 0.32f},
                    IM_COL32(255, 255, 255, 35), bh * 0.28f);
                // 末端渐暗（遮罩）
                if (fw > 20.0f)
                    dl->AddRectFilled({px + fw - 12, py}, {px + fw, py + bh},
                        IM_COL32(0, 0, 0, 80), 3.5f);
            }

            // 5. 危机状态红色脉冲外发光
            if (pulseLow && ratio < 0.25f)
            {
                float glowA = 0.35f + 0.35f * std::sin(T * 6.5f);
                dl->AddRect({px - 2, py - 2}, {px + bw + 2, py + bh + 2},
                    IM_COL32(255, 40, 40, static_cast<int>(glowA * 200)), 4.0f, 0, 2.0f);
            }

            // 6. 金属外框（双层：外深内亮）
            dl->AddRect({px - 1, py - 1}, {px + bw + 1, py + bh + 1},
                IM_COL32(60, 50, 30, 200), 4.0f, 0, 1.0f);
            dl->AddRect({px, py}, {px + bw, py + bh},
                IM_COL32(200, 170, 80, 180), 3.5f, 0, 1.3f);

            // 7. 左端金属铆钉装饰
            dl->AddCircleFilled({px - 6, py + bh * 0.5f}, 4.5f, IM_COL32(180, 150, 60, 220));
            dl->AddCircleFilled({px - 6, py + bh * 0.5f}, 2.8f, IM_COL32(240, 210, 100, 240));
            dl->AddCircle({px - 6, py + bh * 0.5f}, 4.5f, IM_COL32(100, 80, 20, 180), 12, 1.0f);
            // 右端铆钉
            dl->AddCircleFilled({px + bw + 6, py + bh * 0.5f}, 4.5f, IM_COL32(180, 150, 60, 220));
            dl->AddCircleFilled({px + bw + 6, py + bh * 0.5f}, 2.8f, IM_COL32(240, 210, 100, 240));
            dl->AddCircle({px + bw + 6, py + bh * 0.5f}, 4.5f, IM_COL32(100, 80, 20, 180), 12, 1.0f);

            // 8. 标签 + 数值（居中）
            char buf[40];
            snprintf(buf, sizeof(buf), "%s  %.0f / %.0f", label, cur, max);
            ImVec2 ts = ImGui::CalcTextSize(buf);
            // 文字阴影
            dl->AddText({px + (bw - ts.x) * 0.5f + 1, py + (bh - ts.y) * 0.5f + 1},
                IM_COL32(0, 0, 0, 200), buf);
            dl->AddText({px + (bw - ts.x) * 0.5f, py + (bh - ts.y) * 0.5f},
                IM_COL32(255, 240, 210, 240), buf);
        };

        const float bx = wp.x + 12.0f;
        const float byHP = wp.y + 6.0f;
        const float bySE = wp.y + 32.0f;

        // ── HP 条（深红 → 亮红）
        drawDnfBar(bx, byHP, BAR_W, BAR_H,
            IM_COL32(200, 45, 45, 240), IM_COL32(120, 20, 20, 240),
            hpRatio, "HP", attr->getHp(), maxHp, true);

        // ── 星能条（深蓝 → 亮蓝紫）
        drawDnfBar(bx, bySE, BAR_W, 12.0f,
            IM_COL32(55, 100, 220, 220), IM_COL32(30, 55, 140, 220),
            seRatio, "SP", attr->getStarEnergy(), maxSe, false);

        ImGui::End();

        // ── 属性面板（右下角，羊皮纸卷轴风格）──────────────────────────────
        constexpr float PANEL_W = 186.0f;
        constexpr float PANEL_H = 182.0f;
        ImGui::SetNextWindowPos({disp.x - PANEL_W - 12.0f, disp.y - PANEL_H - 8.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({PANEL_W, PANEL_H}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin("##dnf_attr_hud", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoInputs     | ImGuiWindowFlags_NoNav  |
            ImGuiWindowFlags_NoSavedSettings);

        auto* adl = ImGui::GetWindowDrawList();
        ImVec2 ap  = ImGui::GetWindowPos();

        // 羊皮纸背景
        adl->AddRectFilled({ap.x, ap.y}, {ap.x + PANEL_W, ap.y + PANEL_H},
            IM_COL32(28, 22, 14, 220), 6.0f);
        adl->AddRectFilled({ap.x + 2, ap.y + 2}, {ap.x + PANEL_W - 2, ap.y + PANEL_H - 2},
            IM_COL32(42, 32, 18, 180), 5.0f);

        // 金属外框（双层）
        adl->AddRect({ap.x, ap.y}, {ap.x + PANEL_W, ap.y + PANEL_H},
            IM_COL32(60, 48, 22, 200), 6.0f, 0, 1.2f);
        adl->AddRect({ap.x + 2, ap.y + 2}, {ap.x + PANEL_W - 2, ap.y + PANEL_H - 2},
            IM_COL32(180, 148, 60, 160), 5.0f, 0, 1.0f);

        // 四角装饰铆钉
        for (int cx2 = 0; cx2 < 2; ++cx2) for (int cy2 = 0; cy2 < 2; ++cy2)
        {
            float rx = ap.x + (cx2 ? PANEL_W - 6 : 6);
            float ry = ap.y + (cy2 ? PANEL_H - 6 : 6);
            adl->AddCircleFilled({rx, ry}, 4.0f, IM_COL32(160, 130, 50, 220));
            adl->AddCircleFilled({rx, ry}, 2.2f, IM_COL32(230, 195, 90, 255));
        }

        // 标题：卷轴风格
        const char* title = "人物属性";
        ImVec2 titleSz = ImGui::CalcTextSize(title);
        float  tx      = ap.x + (PANEL_W - titleSz.x) * 0.5f;
        // 标题分割线（两侧各一段）
        adl->AddLine({ap.x + 8, ap.y + 18}, {tx - 5, ap.y + 18},
            IM_COL32(180, 148, 60, 140), 1.0f);
        adl->AddLine({tx + titleSz.x + 5, ap.y + 18}, {ap.x + PANEL_W - 8, ap.y + 18},
            IM_COL32(180, 148, 60, 140), 1.0f);
        adl->AddText({tx + 1, ap.y + 8 + 1}, IM_COL32(0, 0, 0, 180), title);
        adl->AddText({tx, ap.y + 8},  IM_COL32(230, 195, 100, 255), title);

        // 属性条目：图标符号 + 数值
        float atk  = attr->get(game::component::StatType::Attack);
        float def  = attr->get(game::component::StatType::Defense);
        float spd  = attr->get(game::component::StatType::Speed);
        float crit = attr->get(game::component::StatType::CritRate) * 100.0f;

        struct StatRow { const char* icon; const char* name; char val[24]; ImU32 col; };
        char atkBuf[24], defBuf[24], spdBuf[24], crtBuf[24], seBuf[24];
        snprintf(atkBuf, sizeof(atkBuf), "%.0f",   atk);
        snprintf(defBuf, sizeof(defBuf), "%.0f",   def);
        snprintf(spdBuf, sizeof(spdBuf), "%.2fx",  spd);
        snprintf(crtBuf, sizeof(crtBuf), "%.0f%%", crit);
        snprintf(seBuf,  sizeof(seBuf),  "%.0f/%.0f", attr->getStarEnergy(), maxSe);

        const StatRow rows[] = {
            {"⚔",  "攻击",  {}, IM_COL32(255, 120,  80, 240)},
            {"🛡",  "防御",  {}, IM_COL32(100, 180, 255, 240)},
            {"💨",  "速度",  {}, IM_COL32(120, 240, 160, 240)},
            {"💥",  "暴击",  {}, IM_COL32(255, 230,  60, 240)},
            {"✦",   "星能",  {}, IM_COL32(180, 130, 255, 240)},
        };
        const char* rowVals[] = {atkBuf, defBuf, spdBuf, crtBuf, seBuf};

        float lineH = 20.0f;
        float startY = ap.y + 26.0f;
        for (int ri = 0; ri < 5; ++ri)
        {
            float ry2 = startY + ri * lineH;
            // 交替行底色
            if (ri % 2 == 0)
                adl->AddRectFilled({ap.x + 6, ry2}, {ap.x + PANEL_W - 6, ry2 + lineH - 1},
                    IM_COL32(60, 46, 26, 80), 2.0f);
            // 图标
            adl->AddText({ap.x + 10, ry2 + 3}, rows[ri].col, rows[ri].icon);
            // 名称
            adl->AddText({ap.x + 30, ry2 + 3}, IM_COL32(200, 178, 130, 220), rows[ri].name);
            // 数值（右对齐）
            ImVec2 vs = ImGui::CalcTextSize(rowVals[ri]);
            adl->AddText({ap.x + PANEL_W - vs.x - 10, ry2 + 3},
                IM_COL32(240, 215, 140, 255), rowVals[ri]);
        }

        // 危机/星能不足提示
        if (hpRatio < 0.25f)
        {
            float blinkA = 0.4f + 0.4f * std::sin(T * 6.5f);
            adl->AddText({ap.x + 10, ap.y + PANEL_H - 17},
                IM_COL32(255, 60, 60, static_cast<int>(blinkA * 255)), "⚠ 生命值危急!");
        }
        else if (seRatio < 0.15f)
        {
            adl->AddText({ap.x + 10, ap.y + PANEL_H - 17},
                IM_COL32(150, 120, 255, 180), "✦ 星能不足");
        }

        float equipAtkBonus = 0.0f;
        float equipDefBonus = 0.0f;
        float equipSpdBonusPct = 0.0f;
        float equipJumpBonusPct = 0.0f;
        if (m_equipmentLoadout.hasItemId("kinetic_actuator"))
        {
            equipAtkBonus += 12.0f;
            equipSpdBonusPct += 10.0f;
        }
        if (m_equipmentLoadout.hasItemId("exo_armor"))
            equipDefBonus += 8.0f;
        if (m_equipmentLoadout.hasItemId("gyro_stabilizer"))
            equipJumpBonusPct += 8.0f;

        if (equipAtkBonus > 0.0f || equipDefBonus > 0.0f || equipSpdBonusPct > 0.0f || equipJumpBonusPct > 0.0f)
        {
            adl->AddLine({ap.x + 8, ap.y + PANEL_H - 36}, {ap.x + PANEL_W - 8, ap.y + PANEL_H - 36},
                IM_COL32(160, 132, 60, 110), 1.0f);
            char bonusLineA[80];
            char bonusLineB[80];
            snprintf(bonusLineA, sizeof(bonusLineA), "装备: 攻 +%.0f  防 +%.0f", equipAtkBonus, equipDefBonus);
            snprintf(bonusLineB, sizeof(bonusLineB), "      速 +%.0f%% 跳 +%.0f%%", equipSpdBonusPct, equipJumpBonusPct);
            adl->AddText({ap.x + 10, ap.y + PANEL_H - 32}, IM_COL32(230, 205, 130, 230), bonusLineA);
            adl->AddText({ap.x + 10, ap.y + PANEL_H - 18}, IM_COL32(205, 190, 145, 220), bonusLineB);
        }

        ImGui::End();
    }

    // ──────────────────────────────────────────────────────────────────────────
    //  技能特效 Tick：推进寿命，删除过期特效
    // ──────────────────────────────────────────────────────────────────────────
    void GameScene::tickSkillVFX(float dt)
    {
        for (auto& vfx : m_skillVfxList)
        {
            if (!vfx.active)
                continue;
            vfx.age += dt;
            if (vfx.age >= vfx.maxAge)
                vfx.active = false;
        }
    }

    void GameScene::tickSkillProjectiles(float dt)
    {
        if (m_skillProjectiles.empty() || !chunk_manager)
            return;

        for (auto &proj : m_skillProjectiles)
        {
            if (!proj.active)
                continue;

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
                proj.active = false;
            }

            if (proj.age >= proj.maxAge)
                proj.active = false;
        }
    }

    void GameScene::tickCombatEffects(float dt)
    {
        for (auto &slash : m_slashVfxList)
        {
            if (!slash.active)
                continue;
            slash.age += dt;
            if (slash.age >= slash.maxAge)
                slash.active = false;
        }

        for (auto &fragment : m_combatFragments)
        {
            if (!fragment.active)
                continue;
            fragment.age += dt;
            fragment.velocity *= std::max(0.0f, 1.0f - dt * 1.6f);
            fragment.velocity.y += 520.0f * dt;
            fragment.worldPos += fragment.velocity * dt;
            if (fragment.age >= fragment.maxAge)
                fragment.active = false;
        }
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
            if (!vfx.active)
                continue;
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
            if (!proj.active)
                continue;
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
            if (!slash.active)
                continue;
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
            if (!fragment.active)
                continue;
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
            if (!vfx.active)
                continue;
            ImVec2 vfxImGui = logicalToImGuiScreen(
                _context, _context.getCamera().worldToScreen(vfx.worldPos));
            drawDebugCross(dl, vfxImGui, IM_COL32(120, 255, 120, 220), 6.0f);
        }

        for (const auto& proj : m_skillProjectiles)
        {
            if (!proj.active)
                continue;
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
        const int activeVfx = static_cast<int>(std::count_if(m_skillVfxList.begin(), m_skillVfxList.end(),
            [](const SkillVFX& vfx) { return vfx.active; }));
        const int activeProj = static_cast<int>(std::count_if(m_skillProjectiles.begin(), m_skillProjectiles.end(),
            [](const SkillProjectile& proj) { return proj.active; }));
        ImGui::Text("活动特效: %d", activeVfx);
        ImGui::Text("飞行投射物: %d", activeProj);
        ImGui::TextColored({0.85f, 0.85f, 0.85f, 1.0f}, "白=鼠标屏幕 蓝=鼠标世界 黄=格中心 红=技能 绿=特效 橙=投射物");
        ImGui::End();
    }

    bool GameScene::canAccessMechInventory() const
    {
        if (!m_mech)
            return false;
        if (m_isPlayerInMech)
            return true;
        if (!m_player || m_possessedMonster)
            return false;

        glm::vec2 playerPos = getActorWorldPosition(m_player);
        glm::vec2 mechPos = getActorWorldPosition(m_mech);
        return glm::distance(playerPos, mechPos) <= 220.0f;
    }

    engine::object::GameObject* GameScene::getControlledActor() const
    {
        if (m_isPlayerInMech && m_mech)
            return m_mech;
        if (m_possessedMonster)
            return m_possessedMonster;
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