#include "context.h"
#include "../input/input_manager.h"
#include "../render/camera.h"
#include "../render/renderer.h"
#include "../render/sprite_render_system.h"
#include "../render/parallax_render_system.h"
#include "../render/tilelayer_render_system.h"
#include "../resource/resource_manager.h"
#include <spdlog/spdlog.h>

namespace engine::core
{
    // 必须在 cpp 中初始化静态成员
    Context* Context::Current = nullptr;
    Context::Context(engine::input::InputManager &input_manager,
                     engine::render::Renderer &renderer,
                     engine::render::Camera &camera,
                     engine::resource::ResourceManager &resource_manager)
                     : _input_manager(input_manager),
                       _renderer(renderer),
                       _camera(camera),
                       _resource_manager(resource_manager)
    {
        // 1. 绑定当前上下文到静态指针
        Current = this;
        // 2. 初始化高性能渲染系统
        _sprite_render_system = std::make_unique<engine::render::SpriteRenderSystem>();
        _parallax_render_system = std::make_unique<engine::render::ParallaxRenderSystem>();
        _tilelayer_render_system = std::make_unique<engine::render::TilelayerRenderSystem>();
        spdlog::trace("Context 初始化完成。静态指针已绑定，SpriteRenderSystem, ParallaxRenderSystem 已创建。");
    }
    Context::~Context()
    {
        if (Current == this)
        {
            Current = nullptr;
        }
        spdlog::trace("Context 已销毁。");
    }
}