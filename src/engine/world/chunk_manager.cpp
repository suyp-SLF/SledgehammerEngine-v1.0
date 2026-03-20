#include "chunk_manager.h"
#include "terrain_generator.h"
#include "../core/context.h"
#include "../render/camera.h"
#include "../resource/resource_manager.h"
#include "world_config.h"
#include "tile_info.h"
#include <algorithm>

namespace engine::world
{
    ChunkManager::ChunkManager(const std::string &atlasTextureId,
                               const glm::ivec2 &tileSize,
                               engine::resource::ResourceManager *resMgr,
                               engine::physics::PhysicsManager *physicsMgr)
        : m_atlasTextureId(atlasTextureId),
          m_tileSize(tileSize),
          m_resMgr(resMgr),
          m_physicsMgr(physicsMgr)
    {
    }

    ChunkManager::~ChunkManager() = default;

    void ChunkManager::rebuildChunkMesh(Chunk &chunk)
    {
        if (!m_resMgr)
            return;

        if (m_resMgr->getGPUDevice() == nullptr)
        {
            if (engine::core::Context::Current)
            {
                chunk.buildMeshGL(m_atlasTextureId, m_tileSize, m_resMgr, engine::core::Context::Current->getRenderer());
            }
            return;
        }

        chunk.buildMesh(m_atlasTextureId, m_tileSize, m_resMgr);
    }

    TileData &ChunkManager::tileAt(int worldX, int worldY)
    {
        int chunkX = worldX / Chunk::SIZE;
        int chunkY = worldY / Chunk::SIZE;
        if (worldX < 0)
            chunkX--;
        if (worldY < 0)
            chunkY--;

        auto it = m_chunks.find(encodeChunkKey(chunkX, chunkY));
        if (it == m_chunks.end())
        {
            static TileData air(TileType::Air);
            return air;
        }
        int localX = worldX - chunkX * Chunk::SIZE;
        int localY = worldY - chunkY * Chunk::SIZE;
        return it->second->tileAt(localX, localY);
    }

    void ChunkManager::setTile(int worldX, int worldY, TileData tile)
    {
        int chunkX = worldX / Chunk::SIZE;
        int chunkY = worldY / Chunk::SIZE;
        if (worldX < 0)
            chunkX--;
        if (worldY < 0)
            chunkY--;

        uint64_t key = encodeChunkKey(chunkX, chunkY);
        auto it = m_chunks.find(key);
        if (it == m_chunks.end())
        {
            loadChunk(chunkX, chunkY);
            it = m_chunks.find(key);
            if (it == m_chunks.end())
                return;
        }

        int localX = worldX - chunkX * Chunk::SIZE;
        int localY = worldY - chunkY * Chunk::SIZE;
        TileData &currentTile = it->second->tileAt(localX, localY);
        if (currentTile.type == tile.type)
            return;

        currentTile = std::move(tile);
        it->second->setDirty();
        it->second->rebuildPhysicsBodies(m_physicsMgr, WorldConfig::PIXELS_PER_METER);
        rebuildChunkMesh(*it->second);
    }

    glm::ivec2 ChunkManager::worldToTile(const glm::vec2 &worldPos) const
    {
        return {
            static_cast<int>(std::floor(worldPos.x / static_cast<float>(m_tileSize.x))),
            static_cast<int>(std::floor(worldPos.y / static_cast<float>(m_tileSize.y)))};
    }

    glm::vec2 ChunkManager::tileToWorld(const glm::ivec2 &tilePos) const
    {
        return {
            tilePos.x * static_cast<float>(m_tileSize.x),
            tilePos.y * static_cast<float>(m_tileSize.y)};
    }

    void ChunkManager::updateVisibleChunks(const glm::vec2 &cameraPos, int viewDistanceInChunks)
    {
        int camChunkX = static_cast<int>(std::floor(cameraPos.x / (Chunk::SIZE * m_tileSize.x)));
        int camChunkY = static_cast<int>(std::floor(cameraPos.y / (Chunk::SIZE * m_tileSize.y)));

        std::vector<uint64_t> toUnload;
        for (const auto &[key, chunk] : m_chunks)
        {
            int cx = static_cast<int>(key >> 32);
            int cy = static_cast<int>(key & 0xFFFFFFFF);
            int distX = std::abs(cx - camChunkX);
            int distY = std::abs(cy - camChunkY);
            if (distX > viewDistanceInChunks || distY > viewDistanceInChunks)
            {
                toUnload.push_back(key);
            }
        }
        for (auto key : toUnload)
        {
            int cx = static_cast<int>(key >> 32);
            int cy = static_cast<int>(key & 0xFFFFFFFFu);
            unloadChunk(cx, cy);
        }

        for (int dx = -viewDistanceInChunks; dx <= viewDistanceInChunks; ++dx)
        {
            for (int dy = -viewDistanceInChunks; dy <= viewDistanceInChunks; ++dy)
            {
                int cx = camChunkX + dx;
                int cy = camChunkY + dy;
                uint64_t key = encodeChunkKey(cx, cy);
                if (m_chunks.find(key) == m_chunks.end())
                {
                    loadChunk(cx, cy);
                }
            }
        }
    }

    void ChunkManager::loadChunk(int chunkX, int chunkY)
    {
        auto chunk = std::make_unique<Chunk>(chunkX, chunkY);

        // 使用地形生成器生成瓦片
        if (m_terrainGenerator)
        {
            std::vector<TileData> tiles;
            m_terrainGenerator->generateChunk(chunkX, chunkY, tiles);

            for (int ly = 0; ly < Chunk::SIZE; ++ly)
            {
                for (int lx = 0; lx < Chunk::SIZE; ++lx)
                {
                    chunk->tileAt(lx, ly) = tiles[ly * Chunk::SIZE + lx];
                }
            }
        }

        chunk->createPhysicsBodies(m_physicsMgr, glm::vec2(m_tileSize), WorldConfig::PIXELS_PER_METER);
        rebuildChunkMesh(*chunk);
        m_chunks[encodeChunkKey(chunkX, chunkY)] = std::move(chunk);
    }

    void ChunkManager::setTerrainGenerator(std::unique_ptr<TerrainGenerator> generator)
    {
        m_terrainGenerator = std::move(generator);
    }

    void ChunkManager::unloadChunk(int chunkX, int chunkY)
    {
        auto it = m_chunks.find(encodeChunkKey(chunkX, chunkY));
        if (it != m_chunks.end())
        {
            it->second->destroyPhysicsBodies(m_physicsMgr);
            m_chunks.erase(it);
        }
    }

    void ChunkManager::renderAll(engine::core::Context &ctx) const
    {
        const glm::vec2 chunkWorldSize = glm::vec2(Chunk::SIZE * m_tileSize.x,
                                                   Chunk::SIZE * m_tileSize.y);

        for (const auto &[_, chunk] : m_chunks)
        {
            if (!chunk)
                continue;

            if (!ctx.getCamera().isBoxInView(chunk->getWorldPosition(m_tileSize), chunkWorldSize))
                continue;

            chunk->draw(ctx);
        }
    }
} // namespace engine::world