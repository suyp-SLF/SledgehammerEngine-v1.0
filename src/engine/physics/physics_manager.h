// physics_manager.h
#pragma once
#include <box2d/box2d.h> // 引入Box2D库
#include <unordered_map>
#include <vector>

namespace engine::render
{
    class Renderer;
    class Camera;
}

namespace engine::physics
{
    class PhysicsManager
    {
    public:
        PhysicsManager();
        ~PhysicsManager();

        // 初始化物理世界（重力等）
        void init(b2Vec2 gravity = {0.0f, 10.0f});

        // 每帧更新（固定时间步）
        void update(float timeStep, int subStepCount = 4);

        // 创建静态物理体（用于瓦片）
        b2BodyId createStaticBody(b2Vec2 position, b2Vec2 halfSize, void *userData);
        b2BodyId createDynamicBody(b2Vec2 position, b2Vec2 halfSize, void *userData);

        // 销毁物理体
        void destroyBody(b2BodyId bodyId);
        
        // 通过用户数据查找物理体（可选）
        b2BodyId findBodyByUserData(void *userData) const;

        // 获取物理世界ID
        b2WorldId getWorldId() const { return m_worldId; }

        // 清理所有物理体
        void clearBodies();

        // 调试绘制所有碰撞箱
        void debugDraw(class engine::render::Renderer &renderer, const class engine::render::Camera &camera) const;

    private:
        b2WorldId m_worldId = b2_nullWorldId;
        std::vector<b2BodyId> m_bodies;
        std::unordered_map<void *, b2BodyId> m_userDataToBody; // 用于快速查找
    };

} // namespace engine::physics