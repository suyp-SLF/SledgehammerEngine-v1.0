#include "chunk.h"
#include "../render/renderer.h"
#include "../world/world_config.h"
#include "../core/context.h"
#include <glm/glm.hpp>

namespace engine::world
{

    Chunk::Chunk(int chunkX, int chunkY)
        : m_chunkX(chunkX), m_chunkY(chunkY)
    {
        // 可以初始化瓦片数据，例如全部设为空气
        for (auto &tile : m_tiles)
        {
            tile = TileData(TileType::Air);
        }
    }

    Chunk::~Chunk()
    {
        // 析构函数（智能指针会自动释放）
    }

    bool Chunk::buildMesh(const std::string &atlasTextureId, const glm::ivec2 &tileSize)
    {
        m_textureId = atlasTextureId;
        m_dirty = false;
        // TODO: 实际生成顶点数据，目前仅占位
        return true;
    }

    void Chunk::render(engine::core::Context &ctx)
    {
        // 判断是否需要渲染
        draw(ctx);
    }

    void Chunk::draw(engine::core::Context &ctx)
    {
        // 将 std::array<TileData, TILE_COUNT> 转换为 std::vector<TileData>
        // 注意：这里会复制数据，如果性能敏感，可以考虑修改 drawTileMap 接受数组指针
        std::vector<engine::world::TileData> tileVec(m_tiles.begin(), m_tiles.end());

        // 计算该区块的世界偏移（单位：像素）
        glm::vec2 worldOffset = glm::vec2(m_chunkX * SIZE * WorldConfig::TILE_SIZE,
                                          m_chunkY * SIZE * WorldConfig::TILE_SIZE);
        // 假设 TILE_SIZE 是一个全局常量，表示每个瓦片的像素尺寸（例如 16）

        ctx.getRenderer().drawTileMap(ctx.getCamera(),
                                      glm::ivec2(SIZE, SIZE),            // 区块尺寸（瓦片个数）
                                      glm::vec2(WorldConfig::TILE_SIZE), // 每个瓦片的大小
                                      tileVec,                           // 瓦片数据
                                      worldOffset);                      // 区块世界偏移
    }

} // namespace engine::world