#include "actor_manager.h"
#include "../component/transform_component.h"
#include "../object/game_object.h"
#include "../core/context.h"
#include <algorithm>

namespace engine::actor
{
    ActorManager::ActorManager(engine::core::Context &context)
        : m_context(context)
    {
    }

    ActorManager::~ActorManager() = default;

    engine::object::GameObject* ActorManager::createActor(const std::string &name)
    {
        auto actor = std::make_unique<engine::object::GameObject>(m_context, name);
        auto *ptr = actor.get();
        m_actors.push_back(std::move(actor));
        return ptr;
    }

    void ActorManager::update(float delta_time)
    {
        for (auto &actor : m_actors)
        {
            actor->update(delta_time);
        }

        for (auto &actor : m_actors)
        {
            if (actor->isNeedRemove())
                actor->clean();
        }

        m_actors.erase(
            std::remove_if(m_actors.begin(), m_actors.end(), [](const auto &actor)
            {
                return actor->isNeedRemove();
            }),
            m_actors.end());
    }

    void ActorManager::render()
    {
        std::vector<engine::object::GameObject*> renderQueue;
        renderQueue.reserve(m_actors.size());
        for (auto &actor : m_actors)
            renderQueue.push_back(actor.get());

        std::stable_sort(renderQueue.begin(), renderQueue.end(),
            [](const engine::object::GameObject *lhs, const engine::object::GameObject *rhs)
            {
                auto *lt = lhs ? lhs->getComponent<engine::component::TransformComponent>() : nullptr;
                auto *rt = rhs ? rhs->getComponent<engine::component::TransformComponent>() : nullptr;
                float ly = lt ? lt->getPosition().y : -99999.0f;
                float ry = rt ? rt->getPosition().y : -99999.0f;
                return ly < ry;
            });

        for (auto *actor : renderQueue)
        {
            actor->render();
        }
    }

    void ActorManager::handleInput()
    {
        for (auto &actor : m_actors)
        {
            actor->handleInput();
        }
    }

    void ActorManager::clear()
    {
        m_actors.clear();
    }
}
