#pragma once
#include "../../engine/scene/scene.h"
#include "../route/route_data.h"
#include <SDL3/SDL.h>

namespace game::scene
{
    /**
     * @brief 路线选择场景
     *
     * 在游戏开始前展示 20×20 格地图，玩家手动选择从出发点
     * 到撤离点的连续路线，确认后进入 GameScene。
     */
    class RouteSelectScene : public engine::scene::Scene
    {
    public:
        RouteSelectScene(const std::string &name,
                         engine::core::Context &context,
                         engine::scene::SceneManager &sceneManager);

        void init()        override;
        void update(float) override;
        void render()      override;
        void handleInput() override;
        void clean()       override;

    private:
        SDL_GLContext          m_glContext = nullptr;
        game::route::RouteData m_route;

        bool isAdjacent  (glm::ivec2 a, glm::ivec2 b) const;
        int  pathIndexOf (glm::ivec2 cell)             const;
        void handleCellClick(int cx, int cy, bool rightClick);
        void confirmAndStart();
    };

} // namespace game::scene
