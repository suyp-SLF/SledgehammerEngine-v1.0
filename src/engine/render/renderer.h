#pragma once
#include "render_types.h"
#include <glm/glm.hpp>
#include <SDL3/SDL.h>
#include <cstdint>
namespace engine::resource
{
    class ResourceManager;
}
namespace engine::world
{
    class TextureBatch;
}
namespace engine::render
{
    class Camera;
    class Sprite;

    class Renderer
    {
    protected:
        engine::resource::ResourceManager *_res_mgr = nullptr;

    public:
        virtual ~Renderer() = default;

        // 将窗口坐标（像素）转换为游戏内的逻辑坐标
        virtual glm::vec2 windowToLogical(float window_x, float window_y) const = 0;
        virtual void setResourceManager(engine::resource::ResourceManager *mgr)
        {
            _res_mgr = mgr;
        }

        // --- 核心绘图接口 (System 必须调用的) ---
        virtual void drawSprite(const Camera &camera,
                                const Sprite &sprite,
                                const glm::vec2 &position,
                                const glm::vec2 &scale = {1.0f, 1.0f},
                                double angle = 0.0f,
                                const glm::vec4 &uv_rect = {0.0f, 0.0f, 1.0f, 1.0f}) = 0;

        // --- 帧生命周期管理 ---
        virtual void clearScreen() = 0;
        virtual void present() = 0;

        // --- 状态设置 ---
        virtual void setDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) = 0;

        // 如果你希望在其他 System 里画视差背景，也可以考虑抽象
        virtual void drawParallax(const Camera &camera, const Sprite &sprite, const glm::vec2 &position,
                                  const glm::vec2 &scroll_factor, const glm::bvec2 &repeat,
                                  const glm::vec2 &scale, double angle) = 0;
        virtual void drawChunkVertices(const Camera &camera,
                                       const std::unordered_map<SDL_GPUTexture *, std::vector<GPUVertex>> &verticesPerTexture,
                                       const glm::vec2 &worldOffset) = 0;
        virtual void drawChunkBatches(const Camera &camera,
                                      const std::unordered_map<SDL_GPUTexture *, engine::world::TextureBatch> &batches,
                                      const glm::vec2 &worldOffset) = 0;
        virtual void clean() = 0;
    };
}