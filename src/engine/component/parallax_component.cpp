#include "parallax_component.h"
#include "transform_component.h"
#include "../render/parallax_render_system.h"
#include "../object/game_object.h"
#include "../resource/resource_manager.h"
#include "../core/context.h"
#include "../render/renderer.h"
#include "../render/sprite.h"
#include <spdlog/spdlog.h>

namespace engine::component
{
    /**
     * @brief 构造函数
     * @param texture_id 纹理资源 ID
     * @param scroll_factor 滚动因子（0=静止，1=随相机同步，<1=远景，>1=近景）
     * @param repeat 是否在 X 或 Y 轴上进行无限重复填充
     */
    ParallaxComponent::ParallaxComponent(const std::string &texture_id, 
                                         const glm::vec2 &scroll_factor, 
                                         glm::bvec2 repeat)
        : _sprite(texture_id), 
          _scroll_factor(scroll_factor), 
          _repeat(repeat)
    {
        spdlog::trace("创建 ParallaxComponent，纹理: {}", texture_id);
    }

    ParallaxComponent::~ParallaxComponent()
    {
        // ⚡️ 必须在析构时注销，防止渲染系统持有野指针
        if (_context)
        {
            _context->getParallaxRenderSystem().unregisterComponent(this);
            spdlog::trace("ParallaxComponent 已从渲染系统中注销");
        }
    }

    void ParallaxComponent::draw(engine::core::Context &ctx)
    {
        if (_is_hidden || !_transform_comp) return;

        // 1. 确保尺寸数据已从 ResourceManager 同步到 _sprite
        ensureResourcesReady();

        // 2. 获取基础变换数据
        const glm::vec2& base_pos = _transform_comp->getPosition();
        const glm::vec2& scale    = _transform_comp->getScale();
        float rotation           = _transform_comp->getRotation();

        // 3. 提交给渲染器
        // 注意：具体的视差位移公式计算被封装在渲染器中，以保持 System 和 Component 的简洁
        ctx.getRenderer().drawParallax(
            ctx.getCamera(),
            _sprite,
            base_pos,
            _scroll_factor,
            _repeat,
            scale,
            rotation
        );
    }

    void ParallaxComponent::ensureResourcesReady()
    {
        // 只有标记为 DIRTY_SIZE 时才去访问资源管理器
        // 这在处理大量重复背景时能节省大量的哈希查找开销
        if (_dirty_flags & DIRTY_SIZE) 
        {
            glm::vec2 size = _context->getResourceManager().getTextureSize(_sprite.getTextureId());
            if (size.x > 0 && size.y > 0) {
                _sprite.setSize(size);
                _dirty_flags &= ~DIRTY_SIZE;
                spdlog::trace("Parallax 资源就绪: {} ({}x{})", _sprite.getTextureId(), size.x, size.y);
            }
        }
    }

    void ParallaxComponent::setTexture(const std::string &texture_id)
    {
        if (_sprite.getTextureId() == texture_id) return;
        _sprite.setTextureId(texture_id);
        _dirty_flags |= DIRTY_SIZE; // 标记尺寸需要重查
    }

    /**
     * @brief 初始化组件
     * 职责：确保关联到 Transform，并验证宿主对象状态。
     */
    void ParallaxComponent::init()
    {
        if (!_owner || !_context)
        {
            spdlog::error("ParallaxComponent 初始化失败：_owner 或 _context 未绑定");
            return;
        }

        // 1. 绑定 Transform
        _transform_comp = _owner->getComponent<TransformComponent>();
        if (!_transform_comp) {
            _transform_comp = _owner->addComponent<TransformComponent>();
        }

        // 2. 注册到视差渲染系统
        _context->getParallaxRenderSystem().registerComponent(this);
        
        // 初始标记为脏，强制触发第一次尺寸获取
        _dirty_flags |= DIRTY_SIZE;
    }

    /**
     * @brief 每一帧的逻辑更新
     * @note 视差偏移通常依赖于 Camera 位置，该逻辑建议放在专用的 ParallaxRenderSystem 中。
     * 这里的 update 保持为空，符合“去逻辑化”的高性能设计。
     */
    void ParallaxComponent::update(float /*delta_time*/)
    {
        // 预留：如果将来需要实现“自发性滚动”（如自动流动的云），可以在此计算累加偏移。
        // 视差组件通常不需要主动更新 Transform 版本
        // 因为渲染器在 draw 时会直接读取 Transform 最新坐标
        // 但如果以后有“自动滚动速度”，逻辑写在这里
    }
} // namespace engine::component