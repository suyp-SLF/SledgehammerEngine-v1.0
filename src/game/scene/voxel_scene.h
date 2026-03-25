#pragma once

#include "../../engine/scene/scene.h"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <vector>

namespace game::scene
{
    class VoxelScene final : public engine::scene::Scene
    {
    public:
        VoxelScene(const std::string &name,
                   engine::core::Context &context,
                   engine::scene::SceneManager &sceneManager);

        void init() override;
        void update(float dt) override;
        void render() override;
        void handleInput() override;
        void clean() override;

    private:
        struct Vertex
        {
            glm::vec3 pos;
            glm::vec3 color;
        };

        struct TargetBlock
        {
            bool hit = false;
            glm::ivec3 block{0, 0, 0};
            glm::ivec3 place{0, 0, 0};
        };

        static constexpr int WORLD_X = 48;
        static constexpr int WORLD_Y = 24;
        static constexpr int WORLD_Z = 48;

        SDL_GLContext m_glContext = nullptr;
        unsigned int m_shader = 0;
        unsigned int m_vao = 0;
        unsigned int m_vbo = 0;
        int m_vertexCount = 0;
        using DrawArraysProc = void(*)(unsigned int, int, int);
        using CullFaceProc = void(*)(unsigned int);
        DrawArraysProc m_glDrawArrays = nullptr;
        CullFaceProc m_glCullFace = nullptr;

        std::vector<unsigned char> m_voxels;
        glm::vec3 m_cameraPos{24.0f, 11.0f, 42.0f};
        float m_yaw = -90.0f;
        float m_pitch = -18.0f;
        glm::vec2 m_lastMousePos{0.0f, 0.0f};
        bool m_firstMouseFrame = true;
        bool m_prevLeftMouse = false;
        bool m_prevRightMouse = false;

        void initImGui();
        void initGLResources();
        void generateWorld();
        void rebuildMesh();
        void renderOverlay(const TargetBlock &target);

        int voxelIndex(int x, int y, int z) const;
        bool isInside(int x, int y, int z) const;
        bool isSolid(int x, int y, int z) const;
        unsigned char voxelAt(int x, int y, int z) const;
        void setVoxel(int x, int y, int z, unsigned char value);

        glm::vec3 getForward() const;
        glm::vec3 getRight() const;
        glm::vec3 blockColor(unsigned char type, float shade) const;
        TargetBlock raycastBlock() const;
    };
}