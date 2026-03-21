#pragma once
#include "../../engine/world/tile_info.h"
#include "../../engine/world/chunk_manager.h"
#include "../../engine/world/world_config.h"
#include "../inventory/inventory.h"
#include <glm/glm.hpp>
#include <vector>
#include <functional>
#include <unordered_set>

namespace game::world
{
    /**
     * @brief 世界掉落物 —— 漂浮在世界中的物品实体
     */
    struct DropItem
    {
        glm::vec2 worldPos;     // 世界像素坐标
        glm::vec2 velocity;     // 当前速度（像素/秒）
        game::inventory::Item item;
        int count = 1;
        float lifetime = 30.0f; // 剩余存活时间（秒）
        float bobTimer = 0.0f;  // 上下飘动计时器
        bool collected = false;
    };

    /**
     * @brief 树木管理器 —— 负责生成、挖掘、掉落逻辑
     *
     * 树木由瓦片组成（泰拉瑞亚风格）：
     *   - 树干：宽1格，高 trunkHeight 格，TileType::Wood
     *   - 树冠：在树顶的菱形区域，TileType::Leaves
     *
     * 砍伐规则：
     *   - 挖掉任意Wood瓦片 → 检测整棵树是否有Wood支撑（从根向上连通）
     *   - 不连通的部分全部清除，产生木头+种子掉落物
     */
    class TreeManager
    {
    public:
        TreeManager() = default;

        /**
         * @brief 在一个区块的地表生成树木
         * @param chunkX/chunkY  区块坐标
         * @param chunkMgr       ChunkManager 引用
         * @param config         星球参数（含种子、树高等）
         */
        void generateTreesForChunk(int chunkX, int chunkY,
                                   engine::world::ChunkManager &chunkMgr,
                                   const engine::world::WorldConfig &config);

        /**
         * @brief 挖掘一个瓦片（Wood 或 Leaves）
         *        如果是树木结构的一部分，触发倒树逻辑
         * @param tileX/Y    世界 tile 坐标
         * @param chunkMgr   ChunkManager 引用
         * @param outDrops   输出的掉落物列表（追加）
         */
        void digTile(int tileX, int tileY,
                     engine::world::ChunkManager &chunkMgr,
                     std::vector<DropItem> &outDrops);

        /**
         * @brief 更新掉落物（物理位移、拾取检测）
         * @param dt          帧时间（秒）
         * @param playerPos   玩家世界像素坐标
         * @param inventory   玩家背包
         * @param chunkMgr    用于落地检测
         */
        void updateDrops(float dt,
                         const glm::vec2 &playerPos,
                         game::inventory::Inventory &inventory,
                         engine::world::ChunkManager &chunkMgr);

        const std::vector<DropItem> &getDrops() const { return m_drops; }
        std::vector<DropItem> &getDrops()             { return m_drops; }

    private:
        std::vector<DropItem> m_drops;

        // 已生成树木的根部 tile 坐标（防止重复生成）
        struct TileHash {
            size_t operator()(const glm::ivec2 &v) const {
                return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 16);
            }
        };
        std::unordered_set<glm::ivec2, TileHash> m_generatedRoots;

        // 找到某个 Wood tile 所属树干的根（最底部连续 Wood）
        // 返回 {rootX, rootY}，找不到则返回 {tileX, tileY}
        glm::ivec2 findTreeRoot(int tileX, int tileY,
                                engine::world::ChunkManager &chunkMgr) const;

        // 收集整棵树（从root起的所有 Wood+Leaves 连通块）
        std::vector<glm::ivec2> collectTreeTiles(int rootX, int rootY,
                                                  engine::world::ChunkManager &chunkMgr) const;

        // 生成倒树掉落物
        void spawnDrops(const std::vector<glm::ivec2> &treeTiles,
                        engine::world::ChunkManager &chunkMgr);

        // 随机整数（轻量级 xorshift）
        static int randInt(uint64_t seed, int mn, int mx);
    };

} // namespace game::world
