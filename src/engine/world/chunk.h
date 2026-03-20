#pragma once
#include "tile_info.h"
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <memory>
#include <unordered_map>
#include <SDL3/SDL_gpu.h>
#include <box2d/id.h>

// 假设你有一个渲染接口（如 VertexBuffer, IndexBuffer），这里用伪代码
struct SDL_GPUTexture;
namespace engine::core{ class Context;}
namespace engine::resource{class ResourceManager;}
namespace engine::physics{class PhysicsManager;}
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
        static constexpr int SIZE = 16; // 每个块 16x16 瓦片
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

        // OpenGL 路径：构建 VAO/VBO（委托给 renderer）
        bool buildMeshGL(const std::string &textureId,
                         const glm::ivec2 &tileSize,
                         engine::resource::ResourceManager *resMgr,
                         engine::render::Renderer &renderer);

        // 渲染该块（绑定缓冲并绘制）
        void render(engine::core::Context &ctx);
        void draw(engine::core::Context &ctx);

        // 获取块的世界位置（左下角坐标，单位：瓦片）
        glm::ivec2 getPosition() const
        {
            return glm::ivec2(m_chunkX * SIZE, m_chunkY * SIZE);
        }

        glm::vec2 getWorldPosition(const glm::ivec2 &tileSize) const
        {
            return glm::vec2(m_chunkX * SIZE * tileSize.x, m_chunkY * SIZE * tileSize.y);
        }

        // 检查块是否包含该世界瓦片坐标
        bool contains(int worldX, int worldY) const
        {
            int cx = worldX / SIZE;
            int cy = worldY / SIZE;
            return cx == m_chunkX && cy == m_chunkY;
        }

        // 新增：创建/销毁物理体, 与物理管理器交互
        void createPhysicsBodies(engine::physics::PhysicsManager *physicsMgr, glm::vec2 m_tileSize, float pixelsPerMeter);
        void destroyPhysicsBodies(engine::physics::PhysicsManager *physicsMgr);
        void rebuildPhysicsBodies(engine::physics::PhysicsManager *physicsMgr, float pixelsPerMeter);

    private:
        int m_chunkX, m_chunkY;
        glm::vec2 m_tileSize;
        std::array<engine::world::TileData, TILE_COUNT> m_tiles;
        std::vector<b2BodyId> m_physicsBodies;

        bool m_dirty = true;     // 是否需要重新生成网格
        size_t m_indexCount = 0; // 索引数量

        // 纹理图集ID（每个块使用同一个图集，实际可以全局统一）
        std::unordered_map<SDL_GPUTexture *, TextureBatch> m_batches;
        std::string m_textureId;

        // OpenGL 路径
        unsigned int m_gl_vao = 0;
        unsigned int m_gl_vbo = 0;
        unsigned int m_gl_tex = 0;
        int m_gl_vertex_count = 0;
    };
}
