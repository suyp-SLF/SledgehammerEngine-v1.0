#pragma once
#include "../../engine/scene/scene.h"
#include "../../engine/world/chunk_manager.h"
#include "../../engine/world/world_config.h"
#include "../../engine/physics/physics_manager.h"
#include "../../engine/actor/actor_manager.h"
#include "../../engine/render/text_renderer.h"
#include "../../engine/ecs/registry.h"
#include "../inventory/inventory.h"
#include "../weapon/weapon.h"
#include "../world/tree_manager.h"
#include "../weather/weather_system.h"
#include "../mission/planet_mission_ui.h"
#include "../route/route_data.h"

namespace game::scene
{
    class GameScene : public engine::scene::Scene
    {
    public:
        GameScene(const std::string &name,
                  engine::core::Context &context,
                  engine::scene::SceneManager &sceneManager,
                  game::route::RouteData routeData = {});

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
        float m_zoomSliderValue = 1.0f;
        glm::vec2 m_lastChunkUpdatePos = {-99999.0f, -99999.0f};
        glm::ivec2 m_hoveredTile = {0, 0};
        bool m_hasHoveredTile = false;
        bool m_showPhysicsDebug = false;

        // 背包系统
        game::inventory::Inventory m_inventory;
        bool m_showInventory = false;

        // 武器栏
        game::weapon::WeaponBar m_weaponBar;

        // 树木系统
        game::world::TreeManager m_treeManager;

        // 天气系统
        game::weather::WeatherSystem m_weatherSystem;

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
        void renderUI();
        void updateTextTexture();
        void renderInventoryUI();
        void renderWeaponBar();
        void renderDropItems();
        void renderPlayerStateTag();
        void syncPlayerPresentation();
        void renderRouteHUD();    // 左下角路线 HUD
        void renderSettlementUI(); // 撤离结算界面
        void injectOreVeins();    // 在目标格区域注入矿脉
        void initTestItems();

        // 撤离结算状态
        bool m_showSettlement = false;
        std::string m_playerAnimationTexture = "assets/textures/Characters/player.svg";
    };
} // namespace game::scene