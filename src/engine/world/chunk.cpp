#include "chunk.h"
#include "../render/renderer.h"
#include "../world/world_config.h"
#include "../core/context.h"
#include "../render/render_types.h" // 根据实际路径调整，确保包含 GPUVertex 的完整定义
#include "../resource/resource_manager.h"
#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>

namespace engine::world
{

    Chunk::Chunk(int chunkX, int chunkY)
        : m_chunkX(chunkX), m_chunkY(chunkY)
    {
        // 可以初始化瓦片数据，例如全部设为空气
        for (auto &tile : m_tiles)
        {
            tile = TileData(TileType::Stone);
        }
    }

    Chunk::~Chunk()
    {
        // 析构函数（智能指针会自动释放）
    }

    bool Chunk::buildMesh(const std::string &textureId,
                          const glm::ivec2 &tileSize,
                          engine::resource::ResourceManager *resMgr)
    {
        m_textureId = textureId;
        // 获取设备指针
        SDL_GPUDevice *device = resMgr->getGPUDevice();

        // 先释放旧缓冲区
        for (auto &[tex, batch] : m_batches)
        {
            if (batch.vertexBuffer)
                SDL_ReleaseGPUBuffer(device, batch.vertexBuffer);
        }
        m_batches.clear();

        // 临时收集每个纹理的顶点数据
        std::unordered_map<SDL_GPUTexture *, std::vector<engine::render::GPUVertex>> tempVertices;

        for (int ly = 0; ly < SIZE; ++ly)
        {
            for (int lx = 0; lx < SIZE; ++lx)
            {
                const auto &tile = m_tiles[ly * SIZE + lx];
                if (tile.type == TileType::Air)
                    continue;

                SDL_GPUTexture *texture = resMgr->getGPUTexture(m_textureId);
                if (!texture)
                    continue;

                float x0 = lx * tileSize.x;
                float y0 = ly * tileSize.y;
                float x1 = x0 + tileSize.x;
                float y1 = y0 + tileSize.y;

                float u0 = tile.uv_rect.x;
                float v0 = tile.uv_rect.y;
                float u1 = u0 + tile.uv_rect.z;
                float v1 = v0 + tile.uv_rect.w;

                glm::vec4 white = {1.0f, 1.0f, 1.0f, 1.0f};
                auto &vertices = tempVertices[texture];
                vertices.push_back({{x0, y0}, white, {u0, v0}});
                vertices.push_back({{x1, y0}, white, {u1, v0}});
                vertices.push_back({{x0, y1}, white, {u0, v1}});
                vertices.push_back({{x1, y0}, white, {u1, v0}});
                vertices.push_back({{x0, y1}, white, {u0, v1}});
                vertices.push_back({{x1, y1}, white, {u1, v1}});
            }
        }

        // 为每个纹理创建 GPU 缓冲区并上传数据
        for (const auto &[texture, vertices] : tempVertices)
        {
            if (vertices.empty())
                continue;

            size_t dataSize = vertices.size() * sizeof(engine::render::GPUVertex);
            SDL_GPUBufferCreateInfo bufInfo{};
            bufInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            bufInfo.size = dataSize;
            SDL_GPUBuffer *buffer = SDL_CreateGPUBuffer(device, &bufInfo);
            if (!buffer)
                continue;

            // 创建暂存缓冲区并上传
            SDL_GPUTransferBufferCreateInfo tbInfo{};
            tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            tbInfo.size = dataSize;
            SDL_GPUTransferBuffer *staging = SDL_CreateGPUTransferBuffer(device, &tbInfo);
            void *mapped = SDL_MapGPUTransferBuffer(device, staging, false);
            memcpy(mapped, vertices.data(), dataSize);
            SDL_UnmapGPUTransferBuffer(device, staging);

            // 获取命令缓冲并执行拷贝
            SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
            SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(cmd);
            SDL_GPUTransferBufferLocation src{staging, 0};
            SDL_GPUBufferRegion dst{buffer, 0, (Uint32)dataSize};
            SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);
            SDL_EndGPUCopyPass(copyPass);
            SDL_SubmitGPUCommandBuffer(cmd); // 注意：此处会阻塞等待，但通常 buildMesh 在加载时调用，可以接受

            SDL_ReleaseGPUTransferBuffer(device, staging);

            // 保存批次信息
            m_batches[texture] = {buffer, (Uint32)vertices.size()};
        }

        m_dirty = false;
        return true;
    }

    void Chunk::render(engine::core::Context &ctx)
    {
        // 判断是否需要渲染
        draw(ctx);
    }

    void Chunk::draw(engine::core::Context &ctx)
    {
        if (m_dirty)
        {
            // 需要传入 device 和 resMgr，可以从 ctx 获取
            auto *device = ctx.getResourceManager().getGPUDevice();
            auto *resMgr = &ctx.getResourceManager();
            buildMesh(m_textureId, glm::vec2(WorldConfig::TILE_SIZE), &ctx.getResourceManager());
        }

        glm::vec2 worldOffset = glm::vec2(m_chunkX * SIZE * WorldConfig::TILE_SIZE,
                                          m_chunkY * SIZE * WorldConfig::TILE_SIZE);

        // 调用渲染器的绘制函数，传递批次信息
        ctx.getRenderer().drawChunkBatches(ctx.getCamera(), m_batches, worldOffset);
    }

} // namespace engine::world