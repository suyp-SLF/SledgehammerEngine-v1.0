#pragma once
#include <memory>

// 前置声明
namespace engine::input { class InputManager; }
namespace engine::render { class Renderer; class Camera; class SpriteRenderSystem; class ParallaxRenderSystem; class TilelayerRenderSystem; }
namespace engine::resource { class ResourceManager; }

namespace engine::core
{
    class Context final
    {

    public:
        // 全局静态指针，方便组件在非性能敏感处访问
        static Context *Current;

        Context(engine::input::InputManager &input_manager,
                engine::render::Renderer &renderer,
                engine::render::Camera &camera,
                engine::resource::ResourceManager &resource_manager);
        ~Context();
        // 禁止拷贝和移动
        Context(const Context &) = delete;
        Context &operator=(const Context &) = delete;
        Context(Context &&) = delete;
        Context &operator=(Context &&) = delete;

        // GETTER
        engine::resource::ResourceManager &getResourceManager() const { return _resource_manager; };
        engine::render::Renderer &getRenderer() const { return _renderer; };
        engine::render::Camera &getCamera() const { return _camera; };
        engine::input::InputManager &getInputManager() const { return _input_manager; };

        // 获取渲染系统
        engine::render::SpriteRenderSystem &getSpriteRenderSystem() { return *_sprite_render_system; }
        engine::render::ParallaxRenderSystem &getParallaxRenderSystem() { return *_parallax_render_system; }
        engine::render::TilelayerRenderSystem &getTilelayerRenderSystem() { return *_tilelayer_render_system; }
    private:
        engine::input::InputManager &_input_manager;
        engine::render::Renderer &_renderer;
        engine::render::Camera &_camera;
        engine::resource::ResourceManager &_resource_manager;

        std::unique_ptr<engine::render::SpriteRenderSystem> _sprite_render_system;
        std::unique_ptr<engine::render::ParallaxRenderSystem> _parallax_render_system;
        std::unique_ptr<engine::render::TilelayerRenderSystem> _tilelayer_render_system;
    };
} // namespace engine::core