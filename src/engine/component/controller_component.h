#pragma once
#include "component.h"
#include <glm/vec2.hpp>
#include <algorithm>

namespace engine::component
{
    class PhysicsComponent;

    class ControllerComponent final : public Component
    {
    public:
        enum class MovementState
        {
            Idle,
            Run,
            Jump,
            Fall,
            Jetpack,
        };

        enum class FacingDirection
        {
            Left,
            Right,
        };

        ControllerComponent(float speed = 15.0f, float jetpackForce = 20.0f);
        ~ControllerComponent() = default;

        void setSpeed(float speed) { m_speed = speed; }
        void setEnabled(bool enabled) { m_enabled = enabled; }
        void setJumpSpeed(float jumpSpeed) { m_jumpSpeed = jumpSpeed; }
        void setGroundAcceleration(float accel) { m_groundAccel = accel; }
        void setAirAcceleration(float accel) { m_airAccel = accel; }
        void setJumpCutFactor(float factor) { m_jumpCutFactor = factor; }
        void setCoyoteTime(float coyoteTime) { m_coyoteTime = coyoteTime; }
        void setGroundedThreshold(float threshold) { m_groundedThreshold = threshold; }
        void setJetpackEnabled(bool enabled) { m_jetpackEnabled = enabled; }
        void setJetpackProfile(float fuelMax, float accel, float riseSpeed, float force)
        {
            m_jetpackFuelMax = fuelMax;
            m_jetpackFuel = fuelMax;
            m_jetpackAccel = accel;
            m_jetpackRiseSpeed = riseSpeed;
            m_jetpackForce = force;
        }
        bool isEnabled() const { return m_enabled; }
        float getSpeed()         const { return m_speed; }
        float getJumpSpeed()     const { return m_jumpSpeed; }
        float getGroundAccel()   const { return m_groundAccel; }
        float getAirAccel()      const { return m_airAccel; }
        float getCoyoteTime()    const { return m_coyoteTime; }
        float getJetpackForce()  const { return m_jetpackForce; }
        float getJetpackFuelMax() const { return m_jetpackFuelMax; }
        bool  isJetpackEnabled() const { return m_jetpackEnabled; }
        MovementState getMovementState() const { return m_state; }
        FacingDirection getFacingDirection() const { return m_facing; }
        /** SM 驱动角色使用：由状态机 tick 直接设置朝向，绕过 handleInput 路径 */
        void setFacingDirection(FacingDirection dir) { m_facing = dir; }
        const char* getMovementStateName() const;
        const char* getAnimationStateKey() const;
        float getJetpackFuelRatio() const;

        // 脚底方形碰撞（2.5D 影子碰撞）：
        // - halfSizePx: 方形半边长（像素）
        // - epsilonPx: 与瓦片高度比较的容差
        void setFootCollisionBox(float halfSizePx, float epsilonPx = 2.0f)
        {
            m_footHalfSizePx = std::max(1.0f, halfSizePx);
            m_groundContactEpsilonPx = std::max(0.1f, epsilonPx);
        }
        float getFootCollisionHalfSize() const { return m_footHalfSizePx; }
        void setFootTileContact(bool overlapped, float tileHeightPx)
        {
            m_footTileOverlapped = overlapped;
            m_footTileHeightPx = tileHeightPx;
        }

        // DNF Z轴：视觉跳跃高度（不影响 Box2D 物理）
        float getPosZ()     const { return m_posZ; }
        bool  isZGrounded() const { return m_posZ <= 0.0f && m_velZ <= 0.0f; }
        void setPosZ(float posZ, bool clearVel = true)
        {
            m_posZ = std::max(0.0f, posZ);
            if (clearVel)
                m_velZ = 0.0f;
        }

        // 跑步模式（双击方向键触发）：速度 ×1.5
        void setRunMode(bool run) { m_isRunMode = run; }
        bool isRunMode()   const { return m_isRunMode; }

        // DNF Y轴深度移动范围（世界像素单位）
        // yMin = 后方边界（近天花板），yMax = 前方边界（脚踩地面时角色中心）
        void setGroundBand(float yMin, float yMax) { m_groundYMin = yMin; m_groundYMax = yMax; }

    private:
        float m_speed;
        float m_jetpackForce;
        float m_depthSpeed  = 4.0f;    // W/S 深度移动速度（Box2D m/s）
        float m_groundYMin  = 16.0f;   // 深度上限（靠背景，像素）
        float m_groundYMax  = 64.0f;   // 深度下限（脚踩地面，像素）
        glm::vec2 m_inputDir{0.0f, 0.0f};
        MovementState m_state = MovementState::Idle;
        FacingDirection m_facing = FacingDirection::Right;
        bool m_enabled = true;

        float m_groundAccel = 90.0f;
        float m_airAccel = 20.0f;          // 空中横向加速（比地面慢，减少飘感）
        float m_jumpSpeed = 8.0f;
        float m_jumpCutFactor = 0.45f;
        float m_coyoteTime = 0.1f;
        float m_coyoteTimer = 0.0f;
        float m_groundedThreshold = 0.12f;

        // 下落重力倍率：当 velZ < 0（下落）时增大 Z 轴重力加速度
        float m_fallGravityMultiplier = 2.0f;

        // DNF Z轴跳跃（视觉高度，纯逻辑，不走 Box2D）
        float m_posZ = 0.0f;         // 当前视觉高度（像素，0 = 地面）
        float m_velZ = 0.0f;         // Z轴速度（正=上升）
        bool  m_isRunMode = false;   // 双击跑步模式
        static constexpr float kZGravity  = 800.0f;  // Z轴重力（像素/秒²）
        static constexpr float kZJumpSpeed = 240.0f; // 跳跃初速（像素/秒）

        float m_jetpackFuelMax = 0.75f;
        float m_jetpackFuel = 0.75f;
        float m_jetpackAccel = 20.0f;
        float m_jetpackRiseSpeed = 5.5f;
        bool m_jetpackEnabled = true;
        bool m_hasReleasedJumpSinceTakeoff = false;
        bool m_jetpackUnlockedThisAir = false;
        bool m_flyModeActive = false;

        float m_footHalfSizePx = 9.0f;
        float m_groundContactEpsilonPx = 2.0f;
        bool m_footTileOverlapped = false;
        float m_footTileHeightPx = 0.0f;

        float approach(float current, float target, float delta) const;
    public:
        bool isFlyModeActive() const { return m_flyModeActive; }
    private:
        bool isGrounded(const PhysicsComponent& physics) const;
        void updateMovementState(const glm::vec2& velocity, bool grounded, bool jetpacking, float velZ = 0.0f);

        void handleInput() override;
        void update(float delta_time) override;
        void render() override {}

        static constexpr float kFlyAscendSpeed = 560.0f; // px/s
        static constexpr float kFlyMaxHeightPx = 2000.0f * 32.0f; // 2000m
        static constexpr float kFlyDescendSpeed = 520.0f; // px/s
    };
}
