#include "physics_manager.h"
#include "../render/renderer.h"
#include "../render/camera.h"
#include <algorithm>
#include <glm/glm.hpp>

namespace engine::physics
{
    PhysicsManager::PhysicsManager()
    {
    }

    PhysicsManager::~PhysicsManager()
    {
        clearBodies();
        if (b2World_IsValid(m_worldId))
        {
            b2DestroyWorld(m_worldId);
            m_worldId = b2_nullWorldId;
        }
    }

    void PhysicsManager::init(b2Vec2 gravity)
    {
        b2WorldDef worldDef = b2DefaultWorldDef();
        worldDef.gravity = gravity;
        m_worldId = b2CreateWorld(&worldDef);
    }

    void PhysicsManager::update(float timeStep, int subStepCount)
    {
        if (b2World_IsValid(m_worldId))
        {
            b2World_Step(m_worldId, timeStep, subStepCount);
        }
    }

    b2BodyId PhysicsManager::createStaticBody(b2Vec2 position, b2Vec2 halfSize, void *userData)
    {
        if (!b2World_IsValid(m_worldId))
        {
            return b2_nullBodyId;
        }

        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_staticBody;
        bodyDef.position = position;
        bodyDef.userData = userData;
        b2BodyId bodyId = b2CreateBody(m_worldId, &bodyDef);

        if (!b2Body_IsValid(bodyId))
        {
            return b2_nullBodyId;
        }

        b2Polygon box = b2MakeBox(halfSize.x, halfSize.y);
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        b2CreatePolygonShape(bodyId, &shapeDef, &box);

        m_bodies.push_back(bodyId);
        if (userData)
        {
            m_userDataToBody[userData] = bodyId;
        }
        return bodyId;
    }

    b2BodyId PhysicsManager::createDynamicBody(b2Vec2 position, b2Vec2 halfSize, void *userData)
    {
        if (!b2World_IsValid(m_worldId))
        {
            return b2_nullBodyId;
        }

        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position = position;
        bodyDef.userData = userData;
        bodyDef.fixedRotation = true;
        bodyDef.linearDamping = 0.1f;
        b2BodyId bodyId = b2CreateBody(m_worldId, &bodyDef);

        if (!b2Body_IsValid(bodyId))
        {
            return b2_nullBodyId;
        }

        b2Polygon box = b2MakeBox(halfSize.x, halfSize.y);
        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.material.friction = 0.0f;
        shapeDef.material.restitution = 0.0f;
        shapeDef.density = 1.0f;
        b2CreatePolygonShape(bodyId, &shapeDef, &box);

        m_bodies.push_back(bodyId);
        if (userData)
        {
            m_userDataToBody[userData] = bodyId;
        }

        return bodyId;
    }

    void PhysicsManager::destroyBody(b2BodyId bodyId)
    {
        if (!b2Body_IsValid(bodyId))
        {
            return;
        }

        void *userData = b2Body_GetUserData(bodyId);
        if (userData)
        {
            m_userDataToBody.erase(userData);
        }

        m_bodies.erase(std::remove_if(m_bodies.begin(),
                                      m_bodies.end(),
                                      [bodyId](const b2BodyId &trackedBody)
                                      {
                                          return B2_ID_EQUALS(trackedBody, bodyId);
                                      }),
                       m_bodies.end());
        b2DestroyBody(bodyId);
    }

    b2BodyId PhysicsManager::findBodyByUserData(void *userData) const
    {
        auto it = m_userDataToBody.find(userData);
        return it != m_userDataToBody.end() ? it->second : b2_nullBodyId;
    }

    void PhysicsManager::clearBodies()
    {
        if (b2World_IsValid(m_worldId))
        {
            for (const auto &bodyId : m_bodies)
            {
                if (b2Body_IsValid(bodyId))
                {
                    b2DestroyBody(bodyId);
                }
            }
        }
        m_bodies.clear();
        m_userDataToBody.clear();
    }

    void PhysicsManager::debugDraw(engine::render::Renderer &renderer, const engine::render::Camera &camera) const
    {
        if (!b2World_IsValid(m_worldId))
            return;

        constexpr float PIXELS_PER_METER = 32.0f;

        for (const auto &bodyId : m_bodies)
        {
            if (!b2Body_IsValid(bodyId))
                continue;

            b2Vec2 pos = b2Body_GetPosition(bodyId);
            int shapeCount = b2Body_GetShapeCount(bodyId);

            if (shapeCount > 0)
            {
                b2ShapeId shapes[16];
                int actualCount = b2Body_GetShapes(bodyId, shapes, 16);

                for (int i = 0; i < actualCount; i++)
                {
                    if (b2Shape_IsValid(shapes[i]))
                    {
                        b2Polygon poly = b2Shape_GetPolygon(shapes[i]);
                        float width = (poly.vertices[1].x - poly.vertices[0].x) * 2.0f * PIXELS_PER_METER;
                        float height = (poly.vertices[2].y - poly.vertices[1].y) * 2.0f * PIXELS_PER_METER;

                        renderer.drawRect(camera, pos.x * PIXELS_PER_METER - width / 2, pos.y * PIXELS_PER_METER - height / 2, width, height, glm::vec4(0.0f, 0.5f, 1.0f, 0.3f));
                    }
                }
            }
        }
    }
}