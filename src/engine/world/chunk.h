#pragma once
#include "tile_info.h"
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <memory>
#include <SDL3/SDL_gpu.h>

// 假设你有一个渲染接口（如 VertexBuffer, IndexBuffer），这里用伪代码
struct SDL_GPUTexture;
namespace engine::core
{
    class Context;
}
namespace engine::resource
{
    class ResourceManager;
}
namespace engine::render
{
    class Camera;
    class Renderer;
}
namespace engine::world
{
    struct TextureBatch
    {
        SDL_GPUBuffer *vertexBuffer = nullptr;
        Uint32 vertexCount = 0;
    };
    class Chunk
    {
    public:
        static constexpr int SIZE = 32; // 每个块 32x32 瓦片
        static constexpr int TILE_COUNT = SIZE * SIZE;

        Chunk(int chunkX, int chunkY);
        ~Chunk();

        // 瓦片访问
        engine::world::TileData &tileAt(int localX, int localY) { return m_tiles[localY * SIZE + localX]; }
        const engine::world::TileData &tileAt(int localX, int localY) const { return m_tiles[localY * SIZE + localX]; }

        // 标记块需要重新生成网格（例如瓦片变化时）
        void setDirty() { m_dirty = true; }
        bool isDirty() const { return m_dirty; }

        // 生成或更新顶点数据（基于当前瓦片状态）
        bool buildMesh(const std::string &textureId,
                       const glm::ivec2 &tileSize,
                       engine::resource::ResourceManager *resMgr);

        // 渲染该块（绑定缓冲并绘制）
        void render(engine::core::Context &ctx);
        void draw(engine::core::Context &ctx);

        // 获取块的世界位置（左下角坐标，单位：瓦片）
        glm::ivec2 getPosition() const
        {
            return glm::ivec2(m_chunkX * SIZE, m_chunkY * SIZE);
        }

        // 检查块是否包含该世界瓦片坐标
        bool contains(int worldX, int worldY) const
        {
            int cx = worldX / SIZE;
            int cy = worldY / SIZE;
            return cx == m_chunkX && cy == m_chunkY;
        }

    private:
        int m_chunkX, m_chunkY;
        std::array<engine::world::TileData, TILE_COUNT> m_tiles;

        bool m_dirty = true;     // 是否需要重新生成网格
        size_t m_indexCount = 0; // 索引数量

        // 纹理图集ID（每个块使用同一个图集，实际可以全局统一）
        std::unordered_map<SDL_GPUTexture *, TextureBatch> m_batches;
        std::string m_textureId;
    };
}
