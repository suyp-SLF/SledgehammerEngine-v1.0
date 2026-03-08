// 柏林噪声地形生成器
// perlin_noise_generator.h
#pragma once
#include "terrain_generator.h"
#include "tile_info.h"
#include <memory>

namespace FastNoiseLite
{
    class FastNoiseLite;
} // 前向声明

namespace engine::world
{
    class PerlinNoiseGenerator : public TerrainGenerator
    {
    public:
        explicit PerlinNoiseGenerator(const WorldConfig &config);
        ~PerlinNoiseGenerator() override;

        void generateChunk(int chunkX, int chunkY, std::vector<TileData> &outTiles) const override;
        float getHeightAt(int worldX, int worldY) const override;

    private:
        std::unique_ptr<FastNoiseLite::FastNoiseLite> m_noise;
    };

} // namespace engine::world