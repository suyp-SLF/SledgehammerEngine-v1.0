#include "opengl_renderer.h"
#include "camera.h"
#include "sprite.h"
#include "../resource/resource_manager.h"
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <imgui_impl_opengl3_loader.h>
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW  0x88B4
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif

namespace engine::render
{
    OpenGLRenderer::OpenGLRenderer(SDL_Window *window) : _window(window)
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

        _glContext = SDL_GL_CreateContext(_window);
        if (!_glContext)
        {
            spdlog::error("Failed to create OpenGL context: {}", SDL_GetError());
            return;
        }

        SDL_GL_MakeCurrent(_window, _glContext);
        // -1 = 自适应 VSync：帧时间超标时立即呈现，不锁半刷新率
        // 避免 macOS Metal PSO 预热期被锁在 30fps
        SDL_GL_SetSwapInterval(-1);

        if (imgl3wInit() != 0)
        {
            spdlog::error("OpenGLRenderer: imgl3wInit failed");
            return;
        }

        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        initTileShader();

        spdlog::info("OpenGL Renderer initialized");
    }

    OpenGLRenderer::~OpenGLRenderer()
    {
        if (_glContext)
        {
            SDL_GL_DestroyContext(_glContext);
        }
    }

    void OpenGLRenderer::clearScreen()
    {
        // 每帧开头重置缓存（ImGui 等第三方代码可能改变 GL 状态）
        _boundShader  = 0;
        _boundVAO     = 0;
        _boundTexture = 0;

        glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void OpenGLRenderer::present()
    {
        SDL_GL_SwapWindow(_window);
    }

    glm::vec2 OpenGLRenderer::windowToLogical(float window_x, float window_y) const
    {
        return windowToLogicalByScaling(window_x, window_y);
    }

    void OpenGLRenderer::drawRect(const Camera &camera, float x, float y, float w, float h, const glm::vec4 &color)
    {
        if (!_tileShader || !_whiteTex || w <= 0.0f || h <= 0.0f)
            return;

        glm::mat4 proj = camera.getProjectionMatrix();
        glm::mat4 view = camera.getViewMatrix();
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f));
        glm::mat4 mvp = proj * view * model;

        drawQuad(_whiteTex, mvp, {0.0f, 0.0f, 1.0f, 1.0f}, w, h, false, color);
    }

    void OpenGLRenderer::drawTexture(SDL_GPUTexture*, float, float, float, float)
    {
    }

    void OpenGLRenderer::clean()
    {
        if (imgl3wProcs.gl.DeleteProgram)
        {
            if (_tileShader) { glDeleteProgram(_tileShader); _tileShader = 0; }
            if (_quadVAO)    { glDeleteVertexArrays(1, &_quadVAO); _quadVAO = 0; }
            if (_quadVBO)    { glDeleteBuffers(1, &_quadVBO); _quadVBO = 0; }
            if (_whiteTex)   { glDeleteTextures(1, &_whiteTex); _whiteTex = 0; }
        }
    }

    void OpenGLRenderer::initTileShader()
    {
        const char *vert = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;
out vec2 vUV;
out vec4 vColor;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
}
)";
        const char *frag = R"(
#version 330 core
in vec2 vUV;
in vec4 vColor;
out vec4 FragColor;
uniform sampler2D uTex;
uniform vec4 uColor;
void main() {
    FragColor = texture(uTex, vUV) * vColor * uColor;
}
)";
        auto compile = [](GLenum type, const char *src) -> GLuint {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char buf[512]; glGetShaderInfoLog(s, 512, nullptr, buf);
                spdlog::error("Shader compile error: {}", buf);
            }
            return s;
        };
        GLuint vs = compile(GL_VERTEX_SHADER, vert);
        GLuint fs = compile(GL_FRAGMENT_SHADER, frag);
        _tileShader = glCreateProgram();
        glAttachShader(_tileShader, vs);
        glAttachShader(_tileShader, fs);
        glLinkProgram(_tileShader);
        glDeleteShader(vs);
        glDeleteShader(fs);
        _tileUniformMVP = glGetUniformLocation(_tileShader, "uMVP");
        _tileUniformColor = glGetUniformLocation(_tileShader, "uColor");
        glUseProgram(_tileShader);
        glUniform1i(glGetUniformLocation(_tileShader, "uTex"), 0);
        if (_glUniform4f)
            _glUniform4f(_tileUniformColor, 1.0f, 1.0f, 1.0f, 1.0f);
        glUseProgram(0);

        _glDrawArrays = (PFNGLDRAWARRAYSPROC)SDL_GL_GetProcAddress("glDrawArrays");
        _glUniform4f = (PFNGLUNIFORM4FPROC)SDL_GL_GetProcAddress("glUniform4f");
        if (!_glDrawArrays)
            spdlog::error("OpenGLRenderer: failed to load glDrawArrays");
        if (!_glUniform4f)
            spdlog::error("OpenGLRenderer: failed to load glUniform4f");

        glGenVertexArrays(1, &_quadVAO);
        glGenBuffers(1, &_quadVBO);
        glBindVertexArray(_quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, _quadVBO);
        glBufferData(GL_ARRAY_BUFFER, 6 * 8 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glBindVertexArray(0);

        unsigned int white_pixel = 0xFFFFFFFFu;
        glGenTextures(1, &_whiteTex);
        glBindTexture(GL_TEXTURE_2D, _whiteTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white_pixel);
        glBindTexture(GL_TEXTURE_2D, 0);

        spdlog::info("OpenGLRenderer: tile shader compiled");
    }

    bool OpenGLRenderer::buildChunkMeshGL(unsigned int &vao, unsigned int &vbo, int &vertexCount,
                                          const std::vector<float> &vertices)
    {
        if (vertices.empty())
        {
            vertexCount = 0;
            return true;
        }

        if (vao == 0) glGenVertexArrays(1, &vao);
        if (vbo == 0) glGenBuffers(1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        // macOS Core Profile: use GL_DYNAMIC_DRAW instead of GL_STATIC_DRAW
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(vertices.size() * sizeof(float)), vertices.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        vertexCount = static_cast<int>(vertices.size() / 8);
        return true;
    }

    void OpenGLRenderer::drawChunkGL(const Camera &camera, unsigned int vao, unsigned int vbo, int vertexCount,
                                     unsigned int glTex, const glm::vec2 &worldOffset)
    {
        if (!_tileShader || !vao || !vbo || !glTex || vertexCount == 0)
            return;

        glm::mat4 proj = camera.getProjectionMatrix();
        glm::mat4 view = camera.getViewMatrix();
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(worldOffset, 0.0f));
        glm::mat4 mvp = proj * view * model;

        // 只在 shader 切换时调用 glUseProgram（帧内切换代价极高）
        if (_boundShader != _tileShader)
        {
            glUseProgram(_tileShader);
            _boundShader = _tileShader;
        }
        glUniformMatrix4fv(_tileUniformMVP, 1, GL_FALSE, glm::value_ptr(mvp));
        if (_glUniform4f)
            _glUniform4f(_tileUniformColor, 1.0f, 1.0f, 1.0f, 1.0f);

        if (_boundTexture != glTex)
        {
            glBindTexture(GL_TEXTURE_2D, glTex);
            _boundTexture = glTex;
        }

        if (_boundVAO != vao)
        {
            glBindVertexArray(vao);
            _boundVAO = vao;
        }
        if (_glDrawArrays) _glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertexCount);
        // 不解绑 — 留给下一个 draw call 覆盖，省去无意义的 driver 刷新
    }

    void OpenGLRenderer::drawSprite(const Camera &camera, const Sprite &sprite,
                                     const glm::vec2 &position, const glm::vec2 &scale,
                                     double angle, const glm::vec4 &uv_rect)
    {
        if (!_res_mgr || !_tileShader) return;

        unsigned int glTex = _res_mgr->getGLTexture(sprite.getTextureId());
        if (!glTex) return;

        glm::vec2 size = sprite.getSize();
        if (size.x <= 0 || size.y <= 0) return;

        glm::mat4 proj = camera.getProjectionMatrix();
        glm::mat4 view = camera.getViewMatrix();
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(position, 0.0f));
        model = glm::scale(model, glm::vec3(scale, 1.0f));
        if (angle != 0.0)
            model = glm::rotate(model, (float)glm::radians(angle), glm::vec3(0, 0, 1));
        glm::mat4 mvp = proj * view * model;

        drawQuad(glTex, mvp, uv_rect, size.x, size.y, sprite.isFlipped(), glm::vec4(1.0f));
    }

    void OpenGLRenderer::drawQuad(unsigned int glTex, const glm::mat4 &mvp,
                                   const glm::vec4 &uvRect, float w, float h, bool flipped,
                                   const glm::vec4 &color)
    {
        float u0 = uvRect.x;
        float v0 = uvRect.y;
        float u1 = u0 + uvRect.z;
        float v1 = v0 + uvRect.w;
        if (flipped) std::swap(u0, u1);

        float verts[6][8] = {
            {0,  0,  1, 1, 1, 1,  u0, v0},
            {w,  0,  1, 1, 1, 1,  u1, v0},
            {0,  h,  1, 1, 1, 1,  u0, v1},
            {w,  0,  1, 1, 1, 1,  u1, v0},
            {w,  h,  1, 1, 1, 1,  u1, v1},
            {0,  h,  1, 1, 1, 1,  u0, v1},
        };

        if (_boundShader != _tileShader)
        {
            glUseProgram(_tileShader);
            _boundShader = _tileShader;
        }
        glUniformMatrix4fv(_tileUniformMVP, 1, GL_FALSE, glm::value_ptr(mvp));
        if (_glUniform4f)
            _glUniform4f(_tileUniformColor, color.r, color.g, color.b, color.a);

        if (_boundTexture != glTex)
        {
            glBindTexture(GL_TEXTURE_2D, glTex);
            _boundTexture = glTex;
        }
        if (_boundVAO != _quadVAO)
        {
            glBindVertexArray(_quadVAO);
            _boundVAO = _quadVAO;
        }
        glBindBuffer(GL_ARRAY_BUFFER, _quadVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
        if (_glDrawArrays) _glDrawArrays(GL_TRIANGLES, 0, 6);
        // 不解绑 — 留给下一个 draw call 覆盖
    }
}
