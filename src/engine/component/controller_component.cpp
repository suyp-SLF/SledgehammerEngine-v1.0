#include "controller_component.h"
#include "transform_component.h"
#include "physics_component.h"
#include "../object/game_object.h"
#include "../core/context.h"
#include "../input/input_manager.h"
#include <algorithm>
#include <cmath>

namespace engine::component
{
    ControllerComponent::ControllerComponent(float speed, float jetpackForce)
        : m_speed(speed), m_jetpackForce(jetpackForce)
    {
    }

    void ControllerComponent::handleInput()
    {
        if (!_context)
            return;

        auto &input = _context->getInputManager();
        m_inputDir = {0.0f, 0.0f};

        if (input.isActionDown("move_left"))
            m_inputDir.x -= 1.0f;
        if (input.isActionDown("move_right"))
            m_inputDir.x += 1.0f;

        if (m_inputDir.x != 0.0f)
        {
            m_inputDir.x = m_inputDir.x > 0 ? 1.0f : -1.0f;
        }
    }

    float ControllerComponent::approach(float current, float target, float delta) const
    {
        if (current < target)
            return std::min(current + delta, target);
        return std::max(current - delta, target);
    }

    bool ControllerComponent::isGrounded(const PhysicsComponent& physics) const
    {
        return std::abs(physics.getVelocity().y) <= m_groundedThreshold;
    }

    const char* ControllerComponent::getMovementStateName() const
    {
        switch (m_state)
        {
        case MovementState::Idle: return "待机";
        case MovementState::Run: return "奔跑";
        case MovementState::Jump: return "起跳";
        case MovementState::Fall: return "下落";
        case MovementState::Jetpack: return "喷气";
        }
        return "未知";
    }

    const char* ControllerComponent::getAnimationStateKey() const
    {
        switch (m_state)
        {
        case MovementState::Idle: return "idle";
        case MovementState::Run: return "run";
        case MovementState::Jump: return "jump";
        case MovementState::Fall: return "fall";
        case MovementState::Jetpack: return "jetpack";
        }
        return "idle";
    }

    float ControllerComponent::getJetpackFuelRatio() const
    {
        if (m_jetpackFuelMax <= 0.0f)
            return 0.0f;
        return std::clamp(m_jetpackFuel / m_jetpackFuelMax, 0.0f, 1.0f);
    }

    void ControllerComponent::updateMovementState(const glm::vec2& velocity, bool grounded, bool jetpacking)
    {
        if (grounded)
        {
            m_state = std::abs(velocity.x) > 0.6f ? MovementState::Run : MovementState::Idle;
            return;
        }

        if (jetpacking)
        {
            m_state = MovementState::Jetpack;
            return;
        }

        m_state = velocity.y < 0.0f ? MovementState::Jump : MovementState::Fall;
    }

    void ControllerComponent::update(float delta_time)
    {
        if (!_owner)
            return;

        auto* physics = _owner->getComponent<PhysicsComponent>();
        if (!physics)
            return;

        auto& input = _context->getInputManager();
        glm::vec2 vel = physics->getVelocity();

        bool groundedNow = isGrounded(*physics);
        if (groundedNow)
        {
            m_coyoteTimer = m_coyoteTime;
            m_jetpackFuel = m_jetpackFuelMax;
            m_hasReleasedJumpSinceTakeoff = false;
            m_jetpackUnlockedThisAir = false;
        }
        else
        {
            m_coyoteTimer = std::max(0.0f, m_coyoteTimer - delta_time);
            if (input.isActionReleased("jump"))
                m_hasReleasedJumpSinceTakeoff = true;
        }

        float targetSpeed = m_inputDir.x * m_speed;
        float accel = groundedNow ? m_groundAccel : m_airAccel;
        vel.x = approach(vel.x, targetSpeed, accel * delta_time);

        if (m_inputDir.x < 0.0f)
            m_facing = FacingDirection::Left;
        else if (m_inputDir.x > 0.0f)
            m_facing = FacingDirection::Right;

        bool jumpedThisFrame = false;
        if (input.isActionPressed("jump") && m_coyoteTimer > 0.0f)
        {
            vel.y = -m_jumpSpeed;
            m_coyoteTimer = 0.0f;
            groundedNow = false;
            jumpedThisFrame = true;
        }

        if (!groundedNow && input.isActionReleased("jump") && vel.y < 0.0f)
            vel.y *= m_jumpCutFactor;

        if (!groundedNow && !jumpedThisFrame && m_hasReleasedJumpSinceTakeoff &&
            input.isActionPressed("jump") && m_jetpackFuel > 0.0f)
        {
            m_jetpackUnlockedThisAir = true;
        }

        bool jetpacking = false;
        if (!groundedNow && m_jetpackUnlockedThisAir && input.isActionDown("jump") && m_jetpackFuel > 0.0f)
        {
            vel.y = std::max(vel.y - (m_jetpackAccel + m_jetpackForce * 0.2f) * delta_time, -m_jetpackRiseSpeed);
            m_jetpackFuel = std::max(0.0f, m_jetpackFuel - delta_time);
            jetpacking = true;
        }

        physics->setVelocity(vel);
        updateMovementState(vel, groundedNow, jetpacking);
    }
}
