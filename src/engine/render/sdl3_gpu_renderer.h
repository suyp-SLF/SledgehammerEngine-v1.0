#pragma once
#include "renderer.h"
#include <SDL3/SDL_gpu.h>
#include <vector>
namespace engine::resource
{
    class ResourceManager;
}
namespace engine::render
{
    class SDL3GPURenderer final : public Renderer
    {
    public:
        float _logical_w = 640.0f;
        float _logical_h = 360.0f;

        SDL3GPURenderer(SDL_Window *window);
        ~SDL3GPURenderer() override;

        void setResourceManager(engine::resource::ResourceManager *mgr) override;
        SDL_GPUDevice *getDevice() const { return _device; }

        // 实现基类接口
        void clearScreen() override;
        void present() override;

        // 高性能 GPU 绘制
        void drawSprite(const Camera &camera,
                        const Sprite &sprite,
                        const glm::vec2 &position,
                        const glm::vec2 &scale,
                        double angle,
                        const glm::vec4 &uv_rect) override;
        void setDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
        void drawParallax(const Camera &camera, const Sprite &sprite, const glm::vec2 &position,
                          const glm::vec2 &scroll_factor, const glm::bvec2 &repeat,
                          const glm::vec2 &scale, double angle) override;
        void drawTileMap(const Camera &camera,
                         const glm::ivec2 &map_size,
                         const glm::vec2 &tile_size,
                         const std::vector<engine::component::TileInfo> &tiles,
                         const glm::vec2 &layer_offset) override;
        // 将窗口坐标（像素）转换为游戏内的逻辑坐标
        virtual glm::vec2 windowToLogical(float window_x, float window_y) const override;
        virtual void clean() override;

    private:
        // 本地缓存
        SDL_GPUDevice *_device = nullptr;
        SDL_Window *_window = nullptr;

        // GPU 状态缓存
        SDL_GPURenderPass *_active_pass = nullptr;
        SDL_GPUGraphicsPipeline *_sprite_pipeline = nullptr;
        SDL_GPUCommandBuffer *_current_cmd = nullptr;
        SDL_GPUTexture *_current_swapchain_texture = nullptr;
        SDL_GPUBuffer* _tile_vertex_buffer = nullptr; // 瓦片地图顶点缓冲区
        SDL_GPUSampler* _default_sampler = nullptr; // 默认采样器
        SDL_GPUBuffer* _unit_quad_buffer = nullptr; // 单位四边形缓冲区

        void initGPU();
        void createPipeline();
    };
}