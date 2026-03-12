// 世界配置（种子、参数）
// world_config.h
#pragma once
#include <cstdint>
#include <string>

namespace engine::world {

struct WorldConfig {
    uint64_t seed = 0;                // 随机种子
    int seaLevel = 64;                 // 海平面高度（瓦片单位）
    float noiseScale = 0.01f;          // 噪声缩放（频率）
    float amplitude = 20.0f;           // 高度变化幅度
    // 地层定义
    int grassDepth = 2;                 // 草方块覆盖厚度
    int dirtDepth = 5;                  // 泥土层厚度
    int stoneStart = 50;                 // 石头开始出现的深度（低于此值石头为主）

    // 编译期常量
    static constexpr int CHUNK_SIZE = 16;   // 或者 32、64，根据你的需求
    static constexpr int TILE_SIZE = 16;
    // 可加载配置文件
    bool loadFromFile(const std::string& path);
};

} // namespace engine::world