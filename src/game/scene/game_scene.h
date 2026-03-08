#pragma once
#include "../../engine/scene/scene.h"
#include "../../engine/world/chunk_manager.h"
namespace engine::object
{
    class GameObject;
}
namespace game::scene
{
    class GameScene : public engine::scene::Scene
    {
    public:
        GameScene(const std::string &name, engine::core::Context &context, engine::scene::SceneManager &sceneManager);

        //覆盖场景基类的核心方法
        void init() override;
        void update(float delta_time) override;
        void render() override;
        void handleInput() override;
        void clean() override;

    private:
        std::unique_ptr<engine::world::ChunkManager> chunk_manager; // 管理区块
        // 测试函数
        void createTestObject();
        void testCamera();
    };
} // namespace game::scene  