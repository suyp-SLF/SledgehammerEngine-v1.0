#pragma once
#include <vector>
#include <array>
#include <string>
#include <cstdio>
#include <cstdint>
#include <glm/glm.hpp>

namespace game::route
{
    /** @brief 格子地形类型 */
    enum class CellTerrain : uint8_t
    {
        Plains   = 0,  // 草原 - 普通地形
        Forest   = 1,  // 森林 - 树木繁茂
        Rocky    = 2,  // 岩地 - 石块裸露
        Mountain = 3,  // 矿山 - 含矿石（目标地形）
        Cave     = 4,  // 洞穴 - 地下大量洞穴
    };

    /**
     * @brief 任务路线数据
     *   - 20×20 逻辑格地图
     *   - 每个格子对应世界中 TILES_PER_CELL 宽的瓦片区域
     *   - path[0] = 出发格, path[N-1] = 撤离格
     */
    struct RouteData
    {
        static constexpr int MAP_SIZE       = 20;   // 20×20 格
        static constexpr int TILES_PER_CELL = 100;  // 每格横向对应 100 个瓦片

        std::vector<glm::ivec2> path;   // 有序路径（出发 → 撤离）

        // 每个格子的地形类型（行优先 [y][x]）
        std::array<std::array<CellTerrain, MAP_SIZE>, MAP_SIZE> terrain{};

        // 目标地图格（地形为Mountain，含矿脉）；{-1,-1} 表示无效
        glm::ivec2 objectiveCell{-1, -1};

        // 目标地图格在 path 中的索引（-1 = 路线未经过目标格）
        int objectiveZone = -1;

        bool       isValid()   const { return path.size() >= 2; }
        glm::ivec2 startCell() const { return path.empty() ? glm::ivec2{0, 0} : path.front(); }
        glm::ivec2 evacCell()  const { return path.empty() ? glm::ivec2{0, 0} : path.back(); }

        /** 格子标签：col='A'+'x', row=y+1  → e.g. {2,5} = "C6" */
        static std::string cellLabel(glm::ivec2 cell)
        {
            if (cell.x < 0 || cell.x >= MAP_SIZE ||
                cell.y < 0 || cell.y >= MAP_SIZE)
                return "??";
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%c%d",
                          static_cast<char>('A' + cell.x), cell.y + 1);
            return buf;
        }

        /** 根据种子生成全地图地形，并随机选定目标格 */
        void generateTerrain(uint64_t seed);

        /** 地形对应的 RGBA 底色（用于 UI 渲染）*/
        struct Color4 { uint8_t r, g, b, a; };
        static Color4 terrainColor(CellTerrain t)
        {
            switch (t)
            {
            case CellTerrain::Plains:   return {55,  140,  60, 255};
            case CellTerrain::Forest:   return {25,  100,  35, 255};
            case CellTerrain::Rocky:    return {100, 100, 110, 255};
            case CellTerrain::Mountain: return {120,  75, 155, 255};
            case CellTerrain::Cave:     return {40,  40,  70, 255};
            }
            return {50, 50, 60, 255};
        }

        static const char* terrainName(CellTerrain t)
        {
            switch (t)
            {
            case CellTerrain::Plains:   return "\u8349\u539f";
            case CellTerrain::Forest:   return "\u68ee\u6797";
            case CellTerrain::Rocky:    return "\u5ca9\u5730";
            case CellTerrain::Mountain: return "\u77ff\u5c71";
            case CellTerrain::Cave:     return "\u6d1e\u7a74";
            }
            return "\u672a\u77e5";
        }
    };

} // namespace game::route
