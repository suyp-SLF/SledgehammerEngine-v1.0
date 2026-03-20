#pragma once
#include "renderer.h"
#include <SDL3/SDL.h>

namespace engine::render
{
    class OpenGLRenderer final : public Renderer
    {
    public:
        OpenGLRenderer(SDL_Window *window);
        ~OpenGLRenderer() override;

        SDL_Window *getWindow() const override { return _window; }
        void clearScreen() override;
        void present() override;
        void clean() override;
        glm::vec2 windowToLogical(float window_x, float window_y) const override;

        void drawSprite(const Camera &camera, const Sprite &sprite, const glm::vec2 &position,
                        const glm::vec2 &scale = {1.0f, 1.0f}, double angle = 0.0f,
                        const glm::vec4 &uv_rect = {0.0f, 0.0f, 1.0f, 1.0f}) override;
        void drawRect(const Camera &camera, float x, float y, float w, float h, const glm::vec4 &color) override;
        void drawTexture(SDL_GPUTexture* texture, float x, float y, float w, float h) override;
        void setDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override {}
        void drawParallax(const Camera &, const Sprite &, const glm::vec2 &, const glm::vec2 &,
                          const glm::bvec2 & = {true, true}, const glm::vec2 & = {1.0f, 1.0f}, double = 0.0f) override {}
        void drawChunkBatches(const Camera &, const std::unordered_map<SDL_GPUTexture *, engine::world::TextureBatch> &,
                              const glm::vec2 &) override {}
        void drawChunkVertices(const Camera &, const std::unordered_map<SDL_GPUTexture *, std::vector<GPUVertex>> &,
                               const glm::vec2 &) override {}
        void drawChunkGL(const Camera &camera, unsigned int vao, unsigned int vbo, int vertexCount,
                         unsigned int glTex, const glm::vec2 &worldOffset) override;
        bool buildChunkMeshGL(unsigned int &vao, unsigned int &vbo, int &vertexCount,
                              const std::vector<float> &vertices) override;

    private:
        SDL_Window *_window = nullptr;
        SDL_GLContext _glContext = nullptr;

        // Tile shader
        unsigned int _tileShader = 0;
        int _tileUniformMVP = -1;
        int _tileUniformColor = -1;

        // Sprite quad (dynamic VBO, reused every draw call)
        unsigned int _quadVAO = 0;
        unsigned int _quadVBO = 0;
        unsigned int _whiteTex = 0;

        // glDrawArrays function pointer (loaded via SDL)
        using PFNGLDRAWARRAYSPROC = void(*)(unsigned int, int, int);
        using PFNGLUNIFORM4FPROC = void(*)(int, float, float, float, float);
        PFNGLDRAWARRAYSPROC _glDrawArrays = nullptr;
        PFNGLUNIFORM4FPROC _glUniform4f = nullptr;

        void initTileShader();
        void drawQuad(unsigned int glTex, const glm::mat4 &mvp, const glm::vec4 &uvRect, float w, float h, bool flipped,
                      const glm::vec4 &color);
    };
}
