#include "chunk_manager.h"
#include "../core/context.h"
#include "tile_info.h"
#include <algorithm>

namespace engine::world
{
    ChunkManager::ChunkManager(const std::string &atlasTextureId, const glm::ivec2 &tileSize)
        : m_atlasTextureId(atlasTextureId), m_tileSize(tileSize) {}

    ChunkManager::~ChunkManager() = default;

    TileData &ChunkManager::tileAt(int worldX, int worldY)
    {
        int chunkX = worldX / Chunk::SIZE;
        int chunkY = worldY / Chunk::SIZE;
        if (worldX < 0)
            chunkX--; // 处理负数坐标
        if (worldY < 0)
            chunkY--;

        auto it = m_chunks.find(encodeChunkKey(chunkX, chunkY));
        if (it == m_chunks.end())
        {
            // 如果块未加载，可以选择加载或返回空气（但这里为了简单，抛异常或返回默认）
            static TileData air(TileType::Air);
            return air; // 危险：返回局部静态的引用，但多个调用会共享？最好重新设计
            // 实际应确保块已加载，或使用 setTile 时自动加载
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
            // 自动加载块
            loadChunk(chunkX, chunkY);
            it = m_chunks.find(key);
            if (it == m_chunks.end())
                return; // 加载失败
        }

        int localX = worldX - chunkX * Chunk::SIZE;
        int localY = worldY - chunkY * Chunk::SIZE;
        it->second->tileAt(localX, localY) = tile;
        it->second->setDirty();
    }

    void ChunkManager::updateVisibleChunks(const glm::vec2 &cameraPos, int viewDistanceInChunks)
    {
        // 计算相机所在的块
        int camChunkX = static_cast<int>(std::floor(cameraPos.x / (Chunk::SIZE * m_tileSize.x)));
        int camChunkY = static_cast<int>(std::floor(cameraPos.y / (Chunk::SIZE * m_tileSize.y)));

        // 加载视距内的块，卸载之外的块
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
            m_chunks.erase(key);
        }

        // 加载新块
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
        // TODO: 从磁盘加载或生成地形数据
        // 这里简单填充一些测试数据
        for (int ly = 0; ly < Chunk::SIZE; ++ly)
        {
            for (int lx = 0; lx < Chunk::SIZE; ++lx)
            {
                // 生成简单地形：地面以下为石头，地面为草
                int worldY = chunkY * Chunk::SIZE + ly;
                if (worldY < -5)
                {
                    chunk->tileAt(lx, ly) = engine::world::TileData(engine::world::TileType::Stone);
                }
                else if (worldY < 0)
                {
                    chunk->tileAt(lx, ly) = engine::world::TileData(engine::world::TileType::Dirt);
                }
                else
                {
                    chunk->tileAt(lx, ly) = engine::world::TileData(engine::world::TileType::Air);
                }
            }
        }
        chunk->buildMesh(m_atlasTextureId, m_tileSize); // 初始生成
        m_chunks[encodeChunkKey(chunkX, chunkY)] = std::move(chunk);
    }
    void ChunkManager::setTerrainGenerator(std::unique_ptr<TerrainGenerator> generator)
    {
        // 例如：m_generator = std::move(generator);
    }
    void ChunkManager::unloadChunk(int chunkX, int chunkY)
    {
        m_chunks.erase(encodeChunkKey(chunkX, chunkY));
    }

    void ChunkManager::renderAll(engine::core::Context &ctx) const
    {
        for (const auto &[_, chunk] : m_chunks)
        {
            chunk->render(ctx);
        }
    }
} // namespace engine::world