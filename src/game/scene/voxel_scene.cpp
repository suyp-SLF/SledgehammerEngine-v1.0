#include "voxel_scene.h"

#include "../../engine/core/context.h"
#include "../../engine/input/input_manager.h"
#include "../../engine/render/renderer.h"
#include "../../engine/render/opengl_renderer.h"
#include "../../engine/scene/scene_manager.h"
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <imgui_impl_opengl3_loader.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>

#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_DEPTH_BUFFER_BIT
#define GL_DEPTH_BUFFER_BIT 0x00000100
#endif
#ifndef GL_BACK
#define GL_BACK 0x0405
#endif

namespace game::scene
{
    namespace
    {
        constexpr float kMouseSensitivity = 0.12f;
        constexpr float kMoveSpeed = 8.5f;

        unsigned int compileShader(unsigned int type, const char *source)
        {
            unsigned int shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            int success = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success)
            {
                char infoLog[512];
                glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
                SDL_Log("Voxel shader compile error: %s", infoLog);
            }
            return shader;
        }
    }

    VoxelScene::VoxelScene(const std::string &name,
                           engine::core::Context &context,
                           engine::scene::SceneManager &sceneManager)
        : Scene(name, context, sceneManager)
    {
    }

    void VoxelScene::init()
    {
        Scene::init();
        initImGui();
        initGLResources();
        generateWorld();
        rebuildMesh();
        m_lastMousePos = _context.getInputManager().getMousePosition();
    }

    void VoxelScene::initImGui()
    {
        SDL_Window *window = _context.getRenderer().getWindow();
        if (!window)
            return;

        m_glContext = SDL_GL_GetCurrentContext();
        if (!m_glContext)
            return;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.Fonts->AddFontFromFileTTF(
            "assets/fonts/VonwaonBitmap-16px.ttf",
            16.0f, nullptr,
            io.Fonts->GetGlyphRangesChineseSimplifiedCommon());

        ImGui_ImplSDL3_InitForOpenGL(window, m_glContext);
        ImGui_ImplOpenGL3_Init("#version 330");
    }

    void VoxelScene::initGLResources()
    {
        const char *vertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
out vec3 vColor;
uniform mat4 uMVP;
void main()
{
    gl_Position = uMVP * vec4(aPos, 1.0);
    vColor = aColor;
}
)";

        const char *fragSrc = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main()
{
    FragColor = vec4(vColor, 1.0);
}
)";

        unsigned int vs = compileShader(GL_VERTEX_SHADER, vertSrc);
        unsigned int fs = compileShader(GL_FRAGMENT_SHADER, fragSrc);
        m_shader = glCreateProgram();
        glAttachShader(m_shader, vs);
        glAttachShader(m_shader, fs);
        glLinkProgram(m_shader);
        glDeleteShader(vs);
        glDeleteShader(fs);

        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        m_glDrawArrays = reinterpret_cast<DrawArraysProc>(SDL_GL_GetProcAddress("glDrawArrays"));
        m_glCullFace = reinterpret_cast<CullFaceProc>(SDL_GL_GetProcAddress("glCullFace"));
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
        glBindVertexArray(0);
    }

    int VoxelScene::voxelIndex(int x, int y, int z) const
    {
        return x + y * WORLD_X + z * WORLD_X * WORLD_Y;
    }

    bool VoxelScene::isInside(int x, int y, int z) const
    {
        return x >= 0 && x < WORLD_X && y >= 0 && y < WORLD_Y && z >= 0 && z < WORLD_Z;
    }

    bool VoxelScene::isSolid(int x, int y, int z) const
    {
        return voxelAt(x, y, z) != 0;
    }

    unsigned char VoxelScene::voxelAt(int x, int y, int z) const
    {
        if (!isInside(x, y, z))
            return 0;
        return m_voxels[voxelIndex(x, y, z)];
    }

    void VoxelScene::setVoxel(int x, int y, int z, unsigned char value)
    {
        if (!isInside(x, y, z))
            return;
        m_voxels[voxelIndex(x, y, z)] = value;
    }

    glm::vec3 VoxelScene::blockColor(unsigned char type, float shade) const
    {
        glm::vec3 base(0.7f);
        switch (type)
        {
        case 1: base = {0.30f, 0.72f, 0.28f}; break;
        case 2: base = {0.46f, 0.31f, 0.18f}; break;
        case 3: base = {0.55f, 0.57f, 0.62f}; break;
        case 4: base = {0.85f, 0.77f, 0.32f}; break;
        default: break;
        }
        return glm::clamp(base * shade, 0.0f, 1.0f);
    }

    void VoxelScene::generateWorld()
    {
        m_voxels.assign(WORLD_X * WORLD_Y * WORLD_Z, 0);

        for (int z = 0; z < WORLD_Z; ++z)
        {
            for (int x = 0; x < WORLD_X; ++x)
            {
                float wave = std::sin(static_cast<float>(x) * 0.22f) * 2.1f
                           + std::cos(static_cast<float>(z) * 0.19f) * 2.4f;
                int height = 7 + static_cast<int>(wave);
                height = std::clamp(height, 4, WORLD_Y - 3);

                for (int y = 0; y <= height; ++y)
                {
                    unsigned char type = 3;
                    if (y == height)
                        type = 1;
                    else if (y >= height - 2)
                        type = 2;
                    setVoxel(x, y, z, type);
                }

                if ((x + z) % 17 == 0 && height + 4 < WORLD_Y)
                {
                    for (int y = height + 1; y <= height + 3; ++y)
                        setVoxel(x, y, z, 4);
                }
            }
        }
    }

    void VoxelScene::rebuildMesh()
    {
        std::vector<Vertex> vertices;
        vertices.reserve(WORLD_X * WORLD_Y * WORLD_Z * 18);

        const glm::ivec3 dirs[6] = {
            { 0,  0,  1},
            { 0,  0, -1},
            {-1,  0,  0},
            { 1,  0,  0},
            { 0,  1,  0},
            { 0, -1,  0},
        };

        const glm::vec3 faceVerts[6][4] = {
            {{0,0,1},{1,0,1},{0,1,1},{1,1,1}},
            {{1,0,0},{0,0,0},{1,1,0},{0,1,0}},
            {{0,0,0},{0,0,1},{0,1,0},{0,1,1}},
            {{1,0,1},{1,0,0},{1,1,1},{1,1,0}},
            {{0,1,1},{1,1,1},{0,1,0},{1,1,0}},
            {{0,0,0},{1,0,0},{0,0,1},{1,0,1}},
        };

        const float shades[6] = {0.92f, 0.68f, 0.76f, 0.84f, 1.08f, 0.58f};

        for (int z = 0; z < WORLD_Z; ++z)
        {
            for (int y = 0; y < WORLD_Y; ++y)
            {
                for (int x = 0; x < WORLD_X; ++x)
                {
                    unsigned char type = voxelAt(x, y, z);
                    if (type == 0)
                        continue;

                    glm::vec3 base(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
                    for (int f = 0; f < 6; ++f)
                    {
                        glm::ivec3 n = glm::ivec3(x, y, z) + dirs[f];
                        if (isSolid(n.x, n.y, n.z))
                            continue;

                        glm::vec3 color = blockColor(type, shades[f]);
                        const glm::vec3 &p0 = faceVerts[f][0];
                        const glm::vec3 &p1 = faceVerts[f][1];
                        const glm::vec3 &p2 = faceVerts[f][2];
                        const glm::vec3 &p3 = faceVerts[f][3];

                        vertices.push_back({base + p0, color});
                        vertices.push_back({base + p1, color});
                        vertices.push_back({base + p2, color});
                        vertices.push_back({base + p1, color});
                        vertices.push_back({base + p3, color});
                        vertices.push_back({base + p2, color});
                    }
                }
            }
        }

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_DYNAMIC_DRAW);
        m_vertexCount = static_cast<int>(vertices.size());
    }

    glm::vec3 VoxelScene::getForward() const
    {
        glm::vec3 forward;
        forward.x = std::cos(glm::radians(m_yaw)) * std::cos(glm::radians(m_pitch));
        forward.y = std::sin(glm::radians(m_pitch));
        forward.z = std::sin(glm::radians(m_yaw)) * std::cos(glm::radians(m_pitch));
        return glm::normalize(forward);
    }

    glm::vec3 VoxelScene::getRight() const
    {
        return glm::normalize(glm::cross(getForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
    }

    VoxelScene::TargetBlock VoxelScene::raycastBlock() const
    {
        TargetBlock result;
        glm::vec3 origin = m_cameraPos;
        glm::vec3 dir = getForward();
        glm::vec3 lastAir = origin;

        for (float t = 0.0f; t < 8.0f; t += 0.08f)
        {
            glm::vec3 sample = origin + dir * t;
            glm::ivec3 block = glm::floor(sample);
            if (isSolid(block.x, block.y, block.z))
            {
                result.hit = true;
                result.block = block;
                result.place = glm::floor(lastAir);
                return result;
            }
            lastAir = sample;
        }

        return result;
    }

    void VoxelScene::update(float dt)
    {
        const bool *keys = SDL_GetKeyboardState(nullptr);

        glm::vec3 forward = getForward();
        glm::vec3 flatForward = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
        if (glm::length(flatForward) < 0.001f)
            flatForward = {0.0f, 0.0f, -1.0f};
        glm::vec3 right = getRight();

        glm::vec3 move(0.0f);
        if (keys[SDL_SCANCODE_W]) move += flatForward;
        if (keys[SDL_SCANCODE_S]) move -= flatForward;
        if (keys[SDL_SCANCODE_D]) move += right;
        if (keys[SDL_SCANCODE_A]) move -= right;
        if (keys[SDL_SCANCODE_SPACE]) move.y += 0.76f;
        if (keys[SDL_SCANCODE_LSHIFT]) move.y -= 0.76f;

        if (glm::length(move) > 0.001f)
            m_cameraPos += glm::normalize(move) * kMoveSpeed * dt;
        else
            m_cameraPos += move * kMoveSpeed * dt;

        glm::vec2 mouse = _context.getInputManager().getMousePosition();
        if (m_firstMouseFrame)
        {
            m_lastMousePos = mouse;
            m_firstMouseFrame = false;
        }

        glm::vec2 delta = mouse - m_lastMousePos;
        m_lastMousePos = mouse;
        m_yaw += delta.x * kMouseSensitivity;
        m_pitch -= delta.y * kMouseSensitivity;
        m_pitch = std::clamp(m_pitch, -88.0f, 88.0f);

        if (keys[SDL_SCANCODE_LEFT])  m_yaw -= 90.0f * dt;
        if (keys[SDL_SCANCODE_RIGHT]) m_yaw += 90.0f * dt;
        if (keys[SDL_SCANCODE_UP])    m_pitch = std::min(m_pitch + 70.0f * dt, 88.0f);
        if (keys[SDL_SCANCODE_DOWN])  m_pitch = std::max(m_pitch - 70.0f * dt, -88.0f);
    }

    void VoxelScene::handleInput()
    {
        auto target = raycastBlock();
        Uint32 mouseButtons = SDL_GetMouseState(nullptr, nullptr);
        bool leftDown = (mouseButtons & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0;
        bool rightDown = (mouseButtons & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) != 0;

        if (target.hit && leftDown && !m_prevLeftMouse)
        {
            setVoxel(target.block.x, target.block.y, target.block.z, 0);
            rebuildMesh();
        }
        if (target.hit && rightDown && !m_prevRightMouse)
        {
            if (isInside(target.place.x, target.place.y, target.place.z) && !isSolid(target.place.x, target.place.y, target.place.z))
            {
                setVoxel(target.place.x, target.place.y, target.place.z, 1);
                rebuildMesh();
            }
        }

        m_prevLeftMouse = leftDown;
        m_prevRightMouse = rightDown;
    }

    void VoxelScene::renderOverlay(const TargetBlock &target)
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos({10.0f, 10.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.45f);
        ImGui::Begin("##voxel_debug", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::Text("Voxel 3D Prototype");
        ImGui::Text("Pos: %.1f %.1f %.1f", m_cameraPos.x, m_cameraPos.y, m_cameraPos.z);
        ImGui::Text("Yaw/Pitch: %.1f / %.1f", m_yaw, m_pitch);
        ImGui::Text("WASD 移动  Space/Shift 升降");
        ImGui::Text("鼠标视角  左键挖  右键放");
        if (target.hit)
            ImGui::Text("Target: %d %d %d", target.block.x, target.block.y, target.block.z);
        else
            ImGui::TextUnformatted("Target: <none>");
        ImGui::End();

        ImDrawList *dl = ImGui::GetForegroundDrawList();
        ImVec2 center = {ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f};
        dl->AddLine({center.x - 8.0f, center.y}, {center.x + 8.0f, center.y}, IM_COL32(255,255,255,220), 1.8f);
        dl->AddLine({center.x, center.y - 8.0f}, {center.x, center.y + 8.0f}, IM_COL32(255,255,255,220), 1.8f);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    void VoxelScene::render()
    {
        SDL_Window *window = _context.getRenderer().getWindow();
        if (!window)
            return;

        int width = 1280;
        int height = 720;
        SDL_GetWindowSize(window, &width, &height);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        if (m_glCullFace)
            m_glCullFace(GL_BACK);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 proj = glm::perspective(glm::radians(72.0f), static_cast<float>(width) / static_cast<float>(std::max(height, 1)), 0.05f, 400.0f);
        glm::mat4 view = glm::lookAt(m_cameraPos, m_cameraPos + getForward(), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 mvp = proj * view;

        glUseProgram(m_shader);
        glUniformMatrix4fv(glGetUniformLocation(m_shader, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
        glBindVertexArray(m_vao);
        if (m_glDrawArrays)
            m_glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
        glBindVertexArray(0);

        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);

        renderOverlay(raycastBlock());
    }

    void VoxelScene::clean()
    {
        if (m_shader)
        {
            glDeleteProgram(m_shader);
            m_shader = 0;
        }
        if (m_vbo)
        {
            glDeleteBuffers(1, &m_vbo);
            m_vbo = 0;
        }
        if (m_vao)
        {
            glDeleteVertexArrays(1, &m_vao);
            m_vao = 0;
        }

        if (m_glContext)
        {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            m_glContext = nullptr;
        }
    }
}