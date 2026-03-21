// 柏林噪声地形生成器
// perlin_noise_generator.h
#pragma once
#include "terrain_generator.h"
#include "tile_info.h"
#include <memory>
#include <functional>

class FastNoiseLite;

namespace engine::world
{
    class PerlinNoiseGenerator : public TerrainGenerator
    {
    public:
        explicit PerlinNoiseGenerator(const WorldConfig &config);
        ~PerlinNoiseGenerator() override;

        /**
         * @brief 设置生物群系查询回调
         *   fn(tileX) → int，返回值为 CellTerrain 的整数值
         *   0=草原 1=森林 2=岩地 3=矿山 4=洞穴
         */
        using BiomeLookup = std::function<int(int tileX)>;
        void setBiomeLookup(BiomeLookup fn) { m_biomeByZone = std::move(fn); }

        void generateChunk(int chunkX, int chunkY, std::vector<TileData> &outTiles) const override;
        float getHeightAt(int worldX, int worldY) const override;

    private:
        std::unique_ptr<FastNoiseLite> m_noise;
        std::unique_ptr<FastNoiseLite> m_caveNoise;  // 2D 噪声用于洞穴雕刻
        BiomeLookup m_biomeByZone;                   // 可选：生物群系查询
    };

} // namespace engine::world