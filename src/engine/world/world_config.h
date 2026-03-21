// 世界配置（种子、参数）
// world_config.h
#pragma once
#include <glm/vec2.hpp>
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
    int stoneStart = 50;               // 石头开始出现的深度（低于此值石头为主）

    // ── 树木星球参数 ──
    int treeMinTrunkHeight = 8;        // 树干最小高度（格）
    int treeMaxTrunkHeight = 18;       // 树干最大高度（格）
    int treeSpacing        = 5;        // 两棵树之间最小间距（格）
    int treeCrownRadius    = 3;        // 树冠半径（格，椭圆）

    // 编译期常量
    static constexpr int CHUNK_SIZE = 16;   // 或者 32、64，根据你的需求
    static constexpr glm::vec2 TILE_SIZE = {16, 16};
    static constexpr float PIXELS_PER_METER = 32.0f; // 32像素 = 1米
    // 可加载配置文件
    bool loadFromFile(const std::string& path);
};

} // namespace engine::world