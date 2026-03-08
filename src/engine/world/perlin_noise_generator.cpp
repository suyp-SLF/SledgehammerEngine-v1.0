// 柏林噪声地形生成器
#include "perlin_noise_generator.h"
#include "FastNoiseLite.h" // 假设噪声库头文件
#include <cmath>

namespace engine::world
{
    PerlinNoiseGenerator::PerlinNoiseGenerator(const WorldConfig &config)
        : TerrainGenerator(config)
    {
        m_noise = std::make_unique<FastNoiseLite::FastNoiseLite>();
        m_noise->SetSeed(static_cast<int>(config.seed));
        m_noise->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
        m_noise->SetFrequency(config.noiseScale);
    }

    PerlinNoiseGenerator::~PerlinNoiseGenerator() = default;

    float PerlinNoiseGenerator::getHeightAt(int worldX, int worldY) const
    {
        // 2D 噪声值范围 [-1, 1]，映射到 [0, amplitude]
        float noiseVal = m_noise->GetNoise(static_cast<float>(worldX), static_cast<float>(worldY));
        return m_config.amplitude * (noiseVal * 0.5f + 0.5f); // 0 到 amplitude
    }

    void PerlinNoiseGenerator::generateChunk(int chunkX, int chunkY, std::vector<TileData> &outTiles) const
    {
        // 确保输出缓冲区大小正确
        outTiles.resize(CHUNK_SIZE * CHUNK_SIZE);

        // 计算该区块的世界坐标原点（左下角，取决于你的坐标系）
        int baseX = chunkX * CHUNK_SIZE;
        int baseY = chunkY * CHUNK_SIZE;

        for (int ly = 0; ly < CHUNK_SIZE; ++ly)
        {
            for (int lx = 0; lx < CHUNK_SIZE; ++lx)
            {
                int worldX = baseX + lx;
                int worldY = baseY + ly;

                float height = getHeightAt(worldX, 0); // 我们使用 X 坐标，Y 为 0，生成二维地形（类似泰拉瑞亚，高度随 X 变化）
                // 对于真正的 2D 横向卷轴，你可能需要基于 (worldX, 0) 生成一个高度值，然后决定该列每个 y 的方块类型。
                // 这里简化为：高度表示地表的 Y 坐标。如果 worldY 小于高度，则为固体方块，否则为空气。
                // 更真实的做法：使用二维噪声同时生成地形起伏和地层。

                // 示例：简单地层逻辑
                TileType type = TileType::Air;
                if (worldY < height)
                {
                    // 根据地层深度决定类型
                    int depth = static_cast<int>(height) - worldY;
                    if (depth == 0)
                    {
                        type = TileType::Grass;
                    }
                    else if (depth < m_config.grassDepth)
                    {
                        type = TileType::Dirt;
                    }
                    else if (worldY < m_config.stoneStart)
                    {
                        type = TileType::Stone;
                    }
                    else
                    {
                        type = TileType::Dirt;
                    }
                }
                else if (worldY < m_config.seaLevel && worldY >= height)
                {
                    // 水下空气部分？实际可能是水方块，这里暂略。
                    // type = TileType::Water;
                }

                // 设置 TileData
                outTiles[ly * CHUNK_SIZE + lx] = TileData(type);
                // 如果你还需要设置光照、液体等，可在此处理
            }
        }
    }
} // namespace engine::world