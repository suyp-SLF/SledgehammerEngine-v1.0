#include "route_data.h"

namespace game::route
{
    // 简单的 xorshift64 随机数生成器
    static uint64_t xorshift64(uint64_t x)
    {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        return x;
    }

    void RouteData::generateTerrain(uint64_t seed)
    {
        // 为每个格子生成地形类型
        constexpr int N = static_cast<int>(MAP_SIZE);
        for (int y = 0; y < N; ++y)
        {
            for (int x = 0; x < N; ++x)
            {
                uint64_t s = xorshift64(seed ^ (static_cast<uint64_t>(x) * 0x9E3779B97F4A7C15ULL
                                               + static_cast<uint64_t>(y) * 0x6C62272E07BB0142ULL));
                s = xorshift64(s);
        // Plains 35%, Forest 20%, Rocky 20%, Mountain 10%, Cave 15%
                int r = static_cast<int>(s % 100);
                if      (r < 35) terrain[y][x] = CellTerrain::Plains;
                else if (r < 55) terrain[y][x] = CellTerrain::Forest;
                else if (r < 75) terrain[y][x] = CellTerrain::Rocky;
                else if (r < 85) terrain[y][x] = CellTerrain::Mountain;
                else             terrain[y][x] = CellTerrain::Cave;
            }
        }

        // 随机选定一个目标格（不在边缘，强制为 Mountain）
        uint64_t os = xorshift64(seed * 0xDEADBEEFCAFEBABEULL);
        os = xorshift64(os);
        int ox = 2 + static_cast<int>(os % static_cast<uint64_t>(N - 4));
        os = xorshift64(os + 1);
        int oy = 2 + static_cast<int>(os % static_cast<uint64_t>(N - 4));
        objectiveCell = {ox, oy};
        terrain[oy][ox] = CellTerrain::Mountain;

        // 更新：检查目标格是否在已有路线上
        objectiveZone = -1;
        for (int i = 0; i < static_cast<int>(path.size()); ++i)
        {
            if (path[i] == objectiveCell)
            {
                objectiveZone = i;
                break;
            }
        }
    }

} // namespace game::route
