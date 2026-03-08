#pragma once
#include "chunk.h"
#include <unordered_map>
#include <glm/glm.hpp>

namespace engine::world
{
    class TerrainGenerator;
    class ChunkManager
    {
    public:
        ChunkManager(const std::string &atlasTextureId, const glm::ivec2 &tileSize);
        ~ChunkManager();

        void setTerrainGenerator(std::unique_ptr<engine::world::TerrainGenerator> generator);

        // 获取指定世界坐标的瓦片（线程安全？暂不考虑）
        TileData &tileAt(int worldX, int worldY);
        const TileData &tileAt(int worldX, int worldY) const;

        // 设置瓦片并标记对应块为脏
        void setTile(int worldX, int worldY, TileData tile);

        // 更新可见块（根据相机位置和视距）
        void updateVisibleChunks(const glm::vec2 &cameraPos, int viewDistanceInChunks);

        // 渲染所有已加载的块
        void renderAll() const;

        // 加载/卸载块（内部调用）
        void loadChunk(int chunkX, int chunkY);
        void unloadChunk(int chunkX, int chunkY);

    private:
        std::unordered_map<uint64_t, std::unique_ptr<Chunk>> m_chunks;
        std::string m_atlasTextureId;
        glm::ivec2 m_tileSize;

        // 辅助函数：将 (chunkX, chunkY) 编码为 uint64_t 键
        static uint64_t encodeChunkKey(int x, int y)
        {
            return (static_cast<uint64_t>(x) << 32) | static_cast<uint32_t>(y);
        }
    };
} // namespace engine::world