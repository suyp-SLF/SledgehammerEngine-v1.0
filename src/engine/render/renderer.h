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
        glm::vec2 _logical_size = {1280.0f, 720.0f};

        glm::vec2 windowToLogicalByScaling(float window_x, float window_y) const
        {
            SDL_Window *window = getWindow();
            if (!window || _logical_size.x <= 0.0f || _logical_size.y <= 0.0f)
                return {window_x, window_y};

            int win_w = 0;
            int win_h = 0;
            SDL_GetWindowSize(window, &win_w, &win_h);
            if (win_w <= 0 || win_h <= 0)
                return {window_x, window_y};

            return {
                window_x * (_logical_size.x / static_cast<float>(win_w)),
                window_y * (_logical_size.y / static_cast<float>(win_h))};
        }

    public:
        virtual ~Renderer() = default;

        // 将窗口坐标（像素）转换为游戏内的逻辑坐标
        virtual glm::vec2 windowToLogical(float window_x, float window_y) const = 0;
        virtual void setResourceManager(engine::resource::ResourceManager *mgr)
        {
            _res_mgr = mgr;
        }
        void setLogicalSize(const glm::vec2 &logical_size)
        {
            if (logical_size.x > 0.0f && logical_size.y > 0.0f)
                _logical_size = logical_size;
        }
        const glm::vec2 &getLogicalSize() const { return _logical_size; }

        virtual SDL_GPUDevice* getDevice() const { return nullptr; }
        virtual SDL_Window* getWindow() const { return nullptr; }

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

        // OpenGL 路径：直接传 VAO、VBO、顶点数、GL 纹理 ID
        virtual void drawChunkGL(const Camera &camera, unsigned int vao, unsigned int vbo, int vertexCount,
                                 unsigned int glTex, const glm::vec2 &worldOffset) {}

        // OpenGL 路径：在 renderer 内部构建 chunk mesh，确保 GL 函数在正确上下文调用
        virtual bool buildChunkMeshGL(unsigned int &vao, unsigned int &vbo, int &vertexCount,
                                      const std::vector<float> &vertices) { return false; }

        virtual void drawTexture(SDL_GPUTexture* texture, float x, float y, float w, float h) = 0;
        virtual void drawRect(const Camera &camera, float x, float y, float w, float h, const glm::vec4 &color) = 0;
        virtual void clean() = 0;
    };
}