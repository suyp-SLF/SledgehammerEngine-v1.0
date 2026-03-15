#include "vulkan_renderer.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

namespace engine::render
{
    VulkanRenderer::VulkanRenderer(SDL_Window *window)
    {
    }
    VulkanRenderer::~VulkanRenderer()
    {
    }
    void VulkanRenderer::drawSprite(const Camera &camera,
                                    const Sprite &sprite,
                                    const glm::vec2 &position,
                                    const glm::vec2 &scale,
                                    double angle,
                                    const glm::vec4 &uv_rect)
    {
    }

    void VulkanRenderer::drawChunkVertices(const Camera &camera,
                                           const std::unordered_map<SDL_GPUTexture *, std::vector<GPUVertex>> &verticesPerTexture,
                                           const glm::vec2 &worldOffset)
    {
    }

    void VulkanRenderer::drawChunkBatches(const Camera &camera, const std::unordered_map<SDL_GPUTexture *, engine::world::TextureBatch> &batches, const glm::vec2 &worldOffset)
    {
    }

    void drawChunkVertices(const Camera &camera,
                           const std::unordered_map<SDL_Texture *, std::vector<GPUVertex>> &verticesPerTexture,
                           const glm::vec2 &worldOffset)
    {
    }

    void VulkanRenderer::present()
    {
    }

    void VulkanRenderer::clearScreen()
    {
    }
    void VulkanRenderer::setDrawColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
    }
} // namespace ngine::render
