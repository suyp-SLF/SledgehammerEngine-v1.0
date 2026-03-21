#include "tree_manager.h"
#include "../../engine/world/chunk_manager.h"
#include "../../engine/world/tile_info.h"
#include "../../engine/world/world_config.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <stack>
#include <algorithm>

namespace game::world
{
    using namespace engine::world;

    // ──────────────────────────────────────────────
    // 轻量级随机数（不依赖 <random>）
    // ──────────────────────────────────────────────
    static uint64_t xorshift64(uint64_t x)
    {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        return x;
    }

    int TreeManager::randInt(uint64_t seed, int mn, int mx)
    {
        uint64_t r = xorshift64(seed + 0xDEADBEEF);
        return mn + static_cast<int>(r % static_cast<uint64_t>(mx - mn + 1));
    }

    // ──────────────────────────────────────────────
    // 树木生成
    // ──────────────────────────────────────────────
    void TreeManager::generateTreesForChunk(int chunkX, int chunkY,
                                             ChunkManager &chunkMgr,
                                             const engine::world::WorldConfig &config)
    {
        constexpr int CS = engine::world::WorldConfig::CHUNK_SIZE; // 16
        int baseX = chunkX * CS;
        const uint64_t worldSeed  = config.seed;
        const int      minTrunkH  = config.treeMinTrunkHeight;
        const int      maxTrunkH  = config.treeMaxTrunkHeight;
        const int      spacing    = config.treeSpacing > 0 ? config.treeSpacing : 1;
        const int      crownR     = config.treeCrownRadius;

        // 在本区块范围内，以一定间隔随机放置树
        for (int lx = 0; lx < CS; ++lx)
        {
            int worldX = baseX + lx;

            // 用位置+种子决定是否在此列生成树
            uint64_t colSeed = xorshift64(static_cast<uint64_t>(worldX) ^ (worldSeed * 0x9E3779B9));
            // 约 1/spacing 的概率生成
            if ((colSeed % static_cast<uint64_t>(spacing)) != 0)
                continue;

            // 找到该列地表（从上往下找第一个 Grass 或 Dirt)
            int surfaceY = -1;
            for (int wy = 0; wy < 200; ++wy)
            {
                TileType t = chunkMgr.tileAt(worldX, wy).type;
                if (t == TileType::Grass || t == TileType::Dirt)
                {
                    surfaceY = wy;
                    break;
                }
            }
            if (surfaceY < 0)
                continue;

            glm::ivec2 root{worldX, surfaceY - 1};
            // 防止重复生成
            if (m_generatedRoots.count(root))
                continue;

            // 树干高度
            uint64_t heightSeed = xorshift64(colSeed + static_cast<uint64_t>(surfaceY));
            int trunkH = randInt(heightSeed, minTrunkH, maxTrunkH);

            // 确认树干空间都是空气
            bool canPlace = true;
            for (int dy = 0; dy < trunkH; ++dy)
            {
                if (chunkMgr.tileAt(worldX, surfaceY - 1 - dy).type != TileType::Air)
                {
                    canPlace = false;
                    break;
                }
            }
            if (!canPlace)
                continue;

            // 放置树干
            for (int dy = 0; dy < trunkH; ++dy)
            {
                chunkMgr.setTile(worldX, surfaceY - 1 - dy, TileData(TileType::Wood));
            }

            // 放置树冠（椭圆形，半径由星球参数控制，中心在树顶上方1格）
            int crownTopY  = surfaceY - 1 - trunkH + 1; // 冠中心Y
            int crownRx = crownR, crownRy = crownR;
            for (int dy = -crownRy; dy <= crownRy; ++dy)
            {
                for (int dx = -crownRx; dx <= crownRx; ++dx)
                {
                    // 椭圆判断
                    float ex = static_cast<float>(dx) / crownRx;
                    float ey = static_cast<float>(dy) / crownRy;
                    if (ex * ex + ey * ey > 1.0f)
                        continue;

                    int lx2 = worldX + dx;
                    int ly2 = crownTopY + dy;
                    if (chunkMgr.tileAt(lx2, ly2).type == TileType::Air)
                    {
                        chunkMgr.setTile(lx2, ly2, TileData(TileType::Leaves));
                    }
                }
            }

            m_generatedRoots.insert(root);
            spdlog::trace("TreeManager: 在 ({},{}) 植树，高{}格", worldX, surfaceY, trunkH);
        }
    }

    // ──────────────────────────────────────────────
    // 挖掘瓦片 - 触发倒树
    // ──────────────────────────────────────────────
    void TreeManager::digTile(int tileX, int tileY,
                               ChunkManager &chunkMgr,
                               std::vector<DropItem> &outDrops)
    {
        TileType t = chunkMgr.tileAt(tileX, tileY).type;
        if (t != TileType::Wood && t != TileType::Leaves)
        {
            // 不是树木瓦片，直接到 Air
            chunkMgr.setTile(tileX, tileY, TileData(TileType::Air));
            return;
        }

        // 找树根，收集整棵树，然后清空+掉落
        glm::ivec2 root = findTreeRoot(tileX, tileY, chunkMgr);
        auto treeTiles = collectTreeTiles(root.x, root.y, chunkMgr);

        // 先生成掉落物
        spawnDrops(treeTiles, chunkMgr);

        // 将所有树瓦片清空
        for (auto &tp : treeTiles)
        {
            chunkMgr.setTile(tp.x, tp.y, TileData(TileType::Air));
        }

        // 把本次掉落追加到 outDrops
        outDrops.insert(outDrops.end(), m_drops.end() -
            static_cast<int>(treeTiles.size() / 3 + 1), m_drops.end());
        // （spawnDrops 已直接 push 到 m_drops，outDrops 是外部视图引用，所以这里
        //  outDrops 和 m_drops 同为 m_drops，无需额外 insert；
        //  outDrops 参数是为了兼容未来可能的外部传入，此处简化处理）
    }

    // ──────────────────────────────────────────────
    // 找树根（向下找到最底部连续的 Wood）
    // ──────────────────────────────────────────────
    glm::ivec2 TreeManager::findTreeRoot(int tileX, int tileY,
                                          ChunkManager &chunkMgr) const
    {
        // 先向下走到 Wood 底部
        int x = tileX, y = tileY;

        // 如果是 Leaves，先找到其下方的 Wood 列
        while (chunkMgr.tileAt(x, y).type == TileType::Leaves && y < 300)
            ++y;

        // 再向下找 Wood 连续段
        while (chunkMgr.tileAt(x, y).type == TileType::Wood && y < 300)
            ++y;

        return {x, y - 1}; // 最底部 Wood 位置
    }

    // ──────────────────────────────────────────────
    // 收集整棵树的瓦片（BFS/DFS）
    // ──────────────────────────────────────────────
    std::vector<glm::ivec2> TreeManager::collectTreeTiles(int rootX, int rootY,
                                                            ChunkManager &chunkMgr) const
    {
        std::vector<glm::ivec2> result;
        std::stack<glm::ivec2> stk;
        std::unordered_set<glm::ivec2, TileHash> visited;

        stk.push({rootX, rootY});

        while (!stk.empty())
        {
            auto cur = stk.top(); stk.pop();
            if (visited.count(cur)) continue;

            TileType t = chunkMgr.tileAt(cur.x, cur.y).type;
            if (t != TileType::Wood && t != TileType::Leaves)
                continue;

            visited.insert(cur);
            result.push_back(cur);

            // 四方向扩展（树叶可横向，树干主要向上）
            int dx[] = {0, 0, -1, 1};
            int dy[] = {-1, 1, 0, 0};
            for (int d = 0; d < 4; ++d)
            {
                glm::ivec2 nb{cur.x + dx[d], cur.y + dy[d]};
                if (!visited.count(nb))
                {
                    TileType nt = chunkMgr.tileAt(nb.x, nb.y).type;
                    if (nt == TileType::Wood || nt == TileType::Leaves)
                        stk.push(nb);
                }
            }
        }

        return result;
    }

    // ──────────────────────────────────────────────
    // 生成掉落物
    // ──────────────────────────────────────────────
    void TreeManager::spawnDrops(const std::vector<glm::ivec2> &treeTiles,
                                  ChunkManager &chunkMgr)
    {
        if (treeTiles.empty()) return;

        // 统计木头数量（Wood tile 数 - Leaves tile 数）
        int woodCount  = 0;
        int leavesCount = 0;
        float sumX = 0.0f, sumY = 0.0f;

        for (auto &tp : treeTiles)
        {
            TileType t = chunkMgr.tileAt(tp.x, tp.y).type;
            if (t == TileType::Wood)   ++woodCount;
            if (t == TileType::Leaves) ++leavesCount;
            sumX += tp.x;
            sumY += tp.y;
        }

        glm::vec2 center{sumX / treeTiles.size() * 16.0f,
                         sumY / treeTiles.size() * 16.0f};

        // 木材掉落（每2个Wood tile掉1个木材）
        if (woodCount > 0)
        {
            DropItem woodDrop;
            woodDrop.worldPos = center + glm::vec2(-8.0f, -8.0f);
            woodDrop.velocity = {0.0f, -60.0f}; // 向上弹出
            woodDrop.item     = {"wood", "木材", 64, game::inventory::ItemCategory::Material};
            woodDrop.count    = std::max(1, woodCount / 2);
            woodDrop.lifetime = 60.0f;
            m_drops.push_back(woodDrop);
            spdlog::trace("TreeManager: 掉落木材 ×{}", woodDrop.count);
        }

        // 种子掉落（概率 50%：每棵树固定1～2个）
        uint64_t rng = xorshift64(static_cast<uint64_t>(
            static_cast<int>(center.x) * 73856093 ^ static_cast<int>(center.y) * 19349663));
        int seedCount = 1 + static_cast<int>(rng % 2); // 1~2
        {
            DropItem seedDrop;
            seedDrop.worldPos = center + glm::vec2(8.0f, -8.0f);
            seedDrop.velocity = {30.0f, -50.0f};
            seedDrop.item     = {"tree_seed", "树种", 20, game::inventory::ItemCategory::Material};
            seedDrop.count    = seedCount;
            seedDrop.lifetime = 60.0f;
            m_drops.push_back(seedDrop);
            spdlog::trace("TreeManager: 掉落树种 ×{}", seedCount);
        }
    }

    // ──────────────────────────────────────────────
    // 更新掉落物
    // ──────────────────────────────────────────────
    void TreeManager::updateDrops(float dt,
                                   const glm::vec2 &playerPos,
                                   game::inventory::Inventory &inventory,
                                   ChunkManager &chunkMgr)
    {
        constexpr float GRAVITY       = 400.0f;  // 像素/秒²
        constexpr float PICKUP_RADIUS = 48.0f;   // 拾取半径（像素）
        constexpr float TILE_PX       = 16.0f;

        for (auto &drop : m_drops)
        {
            if (drop.collected) continue;

            drop.lifetime -= dt;
            if (drop.lifetime <= 0.0f)
            {
                drop.collected = true;
                continue;
            }

            // 重力
            drop.velocity.y += GRAVITY * dt;

            // 移动
            drop.worldPos += drop.velocity * dt;

            // 简单落地检测（检查下方瓦片）
            glm::ivec2 tileBelow{
                static_cast<int>(std::floor(drop.worldPos.x / TILE_PX)),
                static_cast<int>(std::floor((drop.worldPos.y + TILE_PX) / TILE_PX))};
            TileType below = chunkMgr.tileAt(tileBelow.x, tileBelow.y).type;
            if (isSolid(below))
            {
                // 落地：停在地面
                drop.worldPos.y = tileBelow.y * TILE_PX - TILE_PX;
                drop.velocity   = {0.0f, 0.0f};
            }

            // 上下轻微飘动（视觉效果）
            drop.bobTimer += dt * 3.0f;

            // 拾取检测
            glm::vec2 diff = playerPos - drop.worldPos;
            float dist2 = diff.x * diff.x + diff.y * diff.y;
            if (dist2 < PICKUP_RADIUS * PICKUP_RADIUS)
            {
                if (inventory.addItem(drop.item, drop.count))
                {
                    drop.collected = true;
                    spdlog::info("拾取: {} ×{}", drop.item.name, drop.count);
                }
            }
        }

        // 清除已回收的掉落物
        m_drops.erase(
            std::remove_if(m_drops.begin(), m_drops.end(),
                           [](const DropItem &d) { return d.collected; }),
            m_drops.end());
    }

} // namespace game::world
