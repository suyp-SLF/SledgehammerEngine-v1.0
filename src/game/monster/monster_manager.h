#pragma once

#include "monster_ai_component.h"
#include <vector>
#include <random>

namespace engine::core { class Context; }
namespace engine::actor { class ActorManager; }
namespace engine::physics { class PhysicsManager; }
namespace engine::world { class ChunkManager; }
namespace engine::object { class GameObject; }

namespace game::monster
{
    class MonsterManager
    {
    public:
        MonsterManager(engine::core::Context &context,
                       engine::actor::ActorManager &actorManager,
                       engine::physics::PhysicsManager &physicsManager,
                       engine::world::ChunkManager &chunkManager,
                       engine::object::GameObject *player);

        void update(float delta_time);
        void renderGroundShadows(engine::core::Context &context) const;
        int crushMonstersInRadius(const glm::vec2 &center, float radius);
        int slashMonsters(const glm::vec2 &origin,
                  float facing,
                  float range,
                  float halfHeight,
                  std::vector<glm::vec2> *defeatPositions = nullptr);
        size_t monsterCount() const { return m_monsters.size(); }

    private:
        struct MonsterEntry
        {
            engine::object::GameObject *actor = nullptr;
            MonsterType type = MonsterType::Slime;
        };

        engine::core::Context &m_context;
        engine::actor::ActorManager &m_actorManager;
        engine::physics::PhysicsManager &m_physicsManager;
        engine::world::ChunkManager &m_chunkManager;
        engine::object::GameObject *m_player;
        std::vector<MonsterEntry> m_monsters;
        std::mt19937 m_rng;
        float m_spawnTimer = 0.0f;

        void spawnMonster();
        void cleanupMonsters();
        bool findSpawnPosition(glm::vec2 &outWorldPos);
        MonsterType pickMonsterType() const;
    };
}