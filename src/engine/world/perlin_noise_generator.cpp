// 柏林噪声地形生成器
#include "perlin_noise_generator.h"
#include "FastNoiseLite.h"
#include <memory>
#include <cmath>
#include <algorithm>

namespace engine::world
{
    PerlinNoiseGenerator::PerlinNoiseGenerator(const WorldConfig &config)
        : TerrainGenerator(config)
    {
        // 地表高度噪声
        m_noise = std::make_unique<FastNoiseLite>();
        m_noise->SetSeed(static_cast<int>(config.seed));
        m_noise->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
        m_noise->SetFrequency(config.noiseScale);

        // 洞穴雕刻噪声（不同种子 + 较低频率，产生更大的洞穴腔体）
        m_caveNoise = std::make_unique<FastNoiseLite>();
        m_caveNoise->SetSeed(static_cast<int>(config.seed) ^ 0x7A3F1B2C);
        m_caveNoise->SetNoiseType(FastNoiseLite::NoiseType_Perlin);
        m_caveNoise->SetFrequency(0.035f);
    }

    PerlinNoiseGenerator::~PerlinNoiseGenerator() = default;

    float PerlinNoiseGenerator::getHeightAt(int worldX, int worldY) const
    {
        float noiseVal = m_noise->GetNoise(static_cast<float>(worldX), static_cast<float>(worldY));
        return m_config.amplitude * (noiseVal * 0.5f + 0.5f);
    }

    void PerlinNoiseGenerator::generateChunk(int chunkX, int chunkY, std::vector<TileData> &outTiles) const
    {
        outTiles.resize(WorldConfig::CHUNK_SIZE * WorldConfig::CHUNK_SIZE);

        int baseX = chunkX * WorldConfig::CHUNK_SIZE;
        int baseY = chunkY * WorldConfig::CHUNK_SIZE;

        for (int ly = 0; ly < WorldConfig::CHUNK_SIZE; ++ly)
        {
            for (int lx = 0; lx < WorldConfig::CHUNK_SIZE; ++lx)
            {
                int worldX = baseX + lx;
                int worldY = baseY + ly;

                // ── 生物群系查询 ──
                // 0=草原 1=森林 2=岩地 3=矿山 4=洞穴
                int biome = m_biomeByZone ? m_biomeByZone(worldX) : 0;

                // ── 振幅倍率（影响地形崎岖程度）──
                float ampMult = 1.0f;
                switch (biome)
                {
                case 2: ampMult = 1.8f; break;  // 岩地：极崎岖
                case 3: ampMult = 1.5f; break;  // 矿山：很崎岖
                case 4: ampMult = 1.6f; break;  // 洞穴：崎岖
                case 1: ampMult = 0.7f; break;  // 森林：较平缓
                default: ampMult = 1.0f; break; // 草原：标准
                }

                // ── 地表高度 ──
                float noiseVal = m_noise->GetNoise(static_cast<float>(worldX), 0.0f);
                float heightOffset = m_config.amplitude * ampMult * (noiseVal * 0.5f + 0.5f);
                int surfaceY = m_config.seaLevel - static_cast<int>(heightOffset);

                // ── 初始瓦片类型 ──
                TileType type = TileType::Air;

                if (worldY < surfaceY)
                {
                    type = TileType::Air;
                }
                else
                {
                    switch (biome)
                    {
                    case 1: // 森林：超厚泥土层，草地表面
                        if (worldY == surfaceY)
                            type = TileType::Grass;
                        else if (worldY < surfaceY + m_config.grassDepth * 4)
                            type = TileType::Dirt;
                        else
                            type = TileType::Stone;
                        break;

                    case 2: // 岩地：砾石表面，极薄泥土，大量裸石
                        if (worldY == surfaceY)
                            type = TileType::Gravel;
                        else if (worldY <= surfaceY + 1)
                            type = TileType::Dirt;
                        else
                            type = TileType::Stone;
                        break;

                    case 3: // 矿山：纯石头（无草无土），矿脉另注入
                    case 4: // 洞穴：纯石头（靠洞穴雕刻产生视觉差异）
                        type = TileType::Stone;
                        break;

                    default: // 草原：标准分层
                        if (worldY == surfaceY)
                            type = TileType::Grass;
                        else if (worldY < surfaceY + m_config.grassDepth)
                            type = TileType::Dirt;
                        else
                            type = TileType::Stone;
                        break;
                    }
                }

                // ── 洞穴雕刻（地表下至少 3 格才开始）──
                if (type != TileType::Air && worldY > surfaceY + 2)
                {
                    // 洞穴密度阈值，每个生物群系不同
                    float thresh = 0.0f;
                    switch (biome)
                    {
                    case 4: thresh = 0.30f; break;  // 洞穴：大量洞穴（蜂巢状）
                    case 2: thresh = 0.14f; break;  // 岩地：中等洞穴
                    case 3: thresh = 0.11f; break;  // 矿山：少量洞穴
                    case 1: thresh = 0.05f; break;  // 森林：极少洞穴
                    default: thresh = 0.08f; break; // 草原：少量洞穴
                    }

                    // |noise| < thresh → 空气（形成蠕虫状隧道/腔体）
                    float cv = m_caveNoise->GetNoise(
                        static_cast<float>(worldX),
                        static_cast<float>(worldY));
                    if (std::abs(cv) < thresh)
                        type = TileType::Air;
                }

                outTiles[ly * WorldConfig::CHUNK_SIZE + lx] = TileData(type);
            }
        }
    }
} // namespace engine::world