#pragma once
#include "component.h"
#include <glm/vec2.hpp>

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
        float getSpeed() const { return m_speed; }
        MovementState getMovementState() const { return m_state; }
        FacingDirection getFacingDirection() const { return m_facing; }
        const char* getMovementStateName() const;
        const char* getAnimationStateKey() const;
        float getJetpackFuelRatio() const;

    private:
        float m_speed;
        float m_jetpackForce;
        glm::vec2 m_inputDir{0.0f, 0.0f};
        MovementState m_state = MovementState::Idle;
        FacingDirection m_facing = FacingDirection::Right;

        float m_groundAccel = 90.0f;
        float m_airAccel = 42.0f;
        float m_jumpSpeed = 8.0f;
        float m_jumpCutFactor = 0.45f;
        float m_coyoteTime = 0.1f;
        float m_coyoteTimer = 0.0f;
        float m_groundedThreshold = 0.12f;

        float m_jetpackFuelMax = 0.75f;
        float m_jetpackFuel = 0.75f;
        float m_jetpackAccel = 20.0f;
        float m_jetpackRiseSpeed = 5.5f;
        bool m_hasReleasedJumpSinceTakeoff = false;
        bool m_jetpackUnlockedThisAir = false;

        float approach(float current, float target, float delta) const;
        bool isGrounded(const PhysicsComponent& physics) const;
        void updateMovementState(const glm::vec2& velocity, bool grounded, bool jetpacking);

        void handleInput() override;
        void update(float delta_time) override;
        void render() override {}
    };
}
