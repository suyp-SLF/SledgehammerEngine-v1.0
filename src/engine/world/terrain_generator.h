// 地形生成器接口/基类
// terrain_generator.h
#pragma once
#include "world_config.h"
#include <glm/glm.hpp>
#include <cstdint>

namespace engine::world
{
    class TileData;
    // 地形生成器抽象基类
    class TerrainGenerator
    {
    public:
        explicit TerrainGenerator(const WorldConfig &config) : m_config(config) {}
        virtual ~TerrainGenerator() = default;

        // 根据区块坐标生成该区块的所有瓦片数据
        // 返回一个二维数组（或直接填充传入的缓冲区），大小 = CHUNK_SIZE * CHUNK_SIZE
        virtual void generateChunk(int chunkX, int chunkY, std::vector<TileData> &outTiles) const = 0;

        // 获取某个世界坐标的高度（用于辅助）
        virtual float getHeightAt(int worldX, int worldY) const = 0;

    protected:
        WorldConfig m_config;
    };

} // namespace engine::world