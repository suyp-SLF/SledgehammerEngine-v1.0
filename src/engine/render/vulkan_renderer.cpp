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

    void VulkanRenderer::drawTileMap(const Camera &camera,
                                     const glm::ivec2 &map_size,
                                     const glm::vec2 &tile_size,
                                     const std::vector<engine::component::TileInfo> &tiles,
                                     const glm::vec2 &layer_offset)
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
