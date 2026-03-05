#pragma once
#include "renderer.h"
#include "sprite.h"
#include <optional>
#include <glm/glm.hpp>
#include <SDL3/SDL_stdinc.h>
#include <map>

struct SDL_Renderer;
struct SDL_FRect;
struct SDL_Texture;
struct SDL_Vertex;
namespace engine::resource
{
    class ResourceManager;
}

namespace engine::render
{
    class Camera;
    class Sprite;
    class SDLRenderer final : public Renderer
    {
    private:
        SDL_Renderer *_sdl_renderer = nullptr;

    public:
        SDLRenderer(SDL_Renderer *renderer);
        void drawSprite(const Camera &camera,
                        const Sprite &sprite,
                        const glm::vec2 &position,
                        const glm::vec2 &scale = {1.f, 1.f},
                        double angle = 0.0f,
                        const glm::vec4 &uv_rect = {0.f, 0.f, 1.f, 1.f});
        void drawParallax(const Camera &camera,
                          const Sprite &sprite,
                          const glm::vec2 &position,
                          const glm::vec2 &scroll_factor,
                          const glm::bvec2 &repeat = {true, true},
                          const glm::vec2 &scale = {1.0f, 1.0f},
                          double angle = 0.0f);

        void drawUISprite(const Sprite &sprite, const glm::vec2 &position, const std::optional<glm::vec2> &size = std::nullopt);
        void drawTileMap(const Camera &camera,
                                      const glm::ivec2 &map_size,
                                      const glm::vec2 &tile_size,
                                      const std::vector<engine::component::TileInfo> &tiles,
                                      const glm::vec2 &layer_offset) override;
        void present();
        void clearScreen();

        void setDrawColor(Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255);
        void setDrawColorFloat(float r, float g, float b, float a = 1.0f);
        void setResourceManager(engine::resource::ResourceManager *mgr) override;
        // 将窗口坐标（像素）转换为游戏内的逻辑坐标
        virtual glm::vec2 windowToLogical(float window_x, float window_y) const override;
        SDL_Renderer *getSDLRenderer() const { return _sdl_renderer; };
        virtual void clean() override;

        // 禁止拷贝和移动
        SDLRenderer(const SDLRenderer &) = delete;
        SDLRenderer &operator=(const SDLRenderer &) = delete;
        SDLRenderer(SDLRenderer &&) = delete;
        SDLRenderer &operator=(SDLRenderer &&) = delete;

    private:
        std::map<SDL_Texture*, std::vector<SDL_Vertex>> _batch_map = {};

        void init();
        std::optional<SDL_FRect> getSpriteRect(const Sprite &sprite);
        bool isRectInViewport(const Camera &camera, const SDL_FRect &rect);
    };
} // namespace engine::render