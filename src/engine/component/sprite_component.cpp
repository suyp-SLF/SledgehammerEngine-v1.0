#include "sprite_component.h"
#include "./transform_component.h"
#include "../resource/resource_manager.h"
#include "../utils/alignment.h"
#include "../object/game_object.h"
#include "../core/context.h"
#include "../render/renderer.h"
#include "../render/sprite_render_system.h"
#include <spdlog/spdlog.h>

namespace engine::component
{
    SpriteComponent::SpriteComponent(engine::render::Sprite &&sprite)
        : _sprite(std::move(sprite))
    {
        spdlog::trace("创建 SpriteComponent，纹理ID: {}", sprite.getTextureId());
    }

    SpriteComponent::SpriteComponent(const std::string &texture_id,
                                     engine::utils::Alignment alignment,
                                     std::optional<engine::utils::FRect> source_rect_opt,
                                     bool is_flipped)
        : _sprite(texture_id, source_rect_opt, is_flipped),
          _alignment(alignment)
    {
        spdlog::trace("创建 SpriteComponent，纹理ID: {}", texture_id);
    }

    SpriteComponent::~SpriteComponent()
    {
        // ⚡️ 必须在析构时注销，防止渲染系统持有野指针
        if (_context)
        {
            _context->getSpriteRenderSystem().unregisterComponent(this);
            spdlog::trace("SpriteComponent 已从渲染系统中注销");
        }
    }

    void SpriteComponent::init()
    {
        if (!_owner || !_context)
        {
            spdlog::error("SpriteComponent 初始化失败：_owner 或 _context 未绑定");
            return;
        }

        // 1. 自动绑定 Transform 组件
        _transform_comp = _owner->getComponent<TransformComponent>();
        if (!_transform_comp)
        {
            _transform_comp = _owner->addComponent<TransformComponent>();
        }

        // 2. 注册到渲染系统
        _context->getSpriteRenderSystem().registerComponent(this);

        // 3. 标记初始状态为脏，确保第一次 ensureResourcesReady 时执行计算
        _dirty_flags |= (DIRTY_SIZE | DIRTY_OFFSET);
    }

    void SpriteComponent::update(float delta_time)
    {
        // 极简 Update：仅做版本比对。
        // 如果 Transform 发生变动（位置、缩放、旋转），则标记偏移量需要重算。
        if (_transform_comp && _transform_comp->getVersion() != _last_transform_version)
        {
            _dirty_flags |= DIRTY_OFFSET;
            _last_transform_version = _transform_comp->getVersion();
        }
    }

    void SpriteComponent::ensureResourcesReady()
    {
        // 1. 处理延迟加载：若尺寸未知，尝试从资源管理器同步
        if (_sprite_size.x <= 0 || _sprite_size.y <= 0)
        {
            updateSpriteSize();
            if (_sprite_size.x > 0)
                _dirty_flags |= DIRTY_OFFSET;
        }

        // 2. 按需计算：仅在脏标记存在时执行 switch 和浮点运算
        if (_dirty_flags & DIRTY_OFFSET)
        {
            updateOffset();
            _dirty_flags &= ~DIRTY_OFFSET; // 清除标记
        }
    }

    void SpriteComponent::draw(engine::core::Context &ctx)
    {
        // 渲染前最后同步资源与偏移
        ensureResourcesReady();

        if (!_transform_comp || _is_hidden)
            return;

        const glm::vec2 &pos = _transform_comp->getPosition();
        const glm::vec2 &scale = _transform_comp->getScale();
        float rotation = _transform_comp->getRotation();

        // 提交至渲染器，应用计算好的 _offset
        ctx.getRenderer().drawSprite(
            ctx.getCamera(),
            _sprite,
            pos + _offset,
            scale,
            rotation);
    }

    // --- Setter 接口实现 ---

    void SpriteComponent::setAlignment(engine::utils::Alignment archor)
    {
        if (_alignment == archor)
            return;
        _alignment = archor;
        _dirty_flags |= DIRTY_OFFSET;
    }

    void SpriteComponent::setFlipped(bool flipped)
    {
        _sprite.setFlipped(flipped);
        // Flipped 状态直接在渲染时由 _sprite 提供给渲染器，无需重算 Offset
    }

    void SpriteComponent::setSpriteById(const std::string &texture_id, std::optional<engine::utils::FRect> source_rect_opt)
    {
        _sprite.setTextureId(texture_id);
        _sprite.setSourceRect(source_rect_opt);
        _dirty_flags |= (DIRTY_SIZE | DIRTY_OFFSET);
    }

    void SpriteComponent::setSourceRect(const std::optional<engine::utils::FRect> &source_rect_opt)
    {
        _sprite.setSourceRect(source_rect_opt);
        _dirty_flags |= (DIRTY_SIZE | DIRTY_OFFSET);
    }

    // --- 内部辅助计算 ---

    void SpriteComponent::updateOffset()
    {
        if (!_transform_comp || _sprite_size.x <= 0 || _sprite_size.y <= 0)
        {
            _offset = {0.0f, 0.0f};
            return;
        }

        const glm::vec2 &scale = _transform_comp->getScale();

        //
        switch (_alignment)
        {
        case engine::utils::Alignment::CENTER:
            _offset = glm::vec2(-_sprite_size.x * scale.x / 2.0f, -_sprite_size.y * scale.y / 2.0f);
            break;
        case engine::utils::Alignment::TOP_LEFT:
            _offset = glm::vec2(0.0f, 0.0f);
            break;
        case engine::utils::Alignment::TOP_RIGHT:
            _offset = glm::vec2(-_sprite_size.x * scale.x, 0.0f);
            break;
        case engine::utils::Alignment::BOTTOM_LEFT:
            _offset = glm::vec2(0.0f, -_sprite_size.y * scale.y);
            break;
        case engine::utils::Alignment::BOTTOM_RIGHT:
            _offset = glm::vec2(-_sprite_size.x * scale.x, -_sprite_size.y * scale.y);
            break;
        case engine::utils::Alignment::TOP_CENTER:
            _offset = glm::vec2(-_sprite_size.x * scale.x / 2.0f, 0.0f);
            break;
        case engine::utils::Alignment::BOTTOM_CENTER:
            _offset = glm::vec2(-_sprite_size.x * scale.x / 2.0f, -_sprite_size.y * scale.y);
            break;
        case engine::utils::Alignment::CENTER_LEFT:
            _offset = glm::vec2(0.0f, -_sprite_size.y * scale.y / 2.0f);
            break;
        case engine::utils::Alignment::CENTER_RIGHT:
            _offset = glm::vec2(-_sprite_size.x * scale.x, -_sprite_size.y * scale.y / 2.0f);
            break;
        default:
            _offset = {0.0f, 0.0f};
            break;
        }
    }

    void SpriteComponent::updateSpriteSize()
    {
        auto source_rect_opt = _sprite.getSourceRect();
        if (source_rect_opt.has_value())
        {
            _sprite_size = source_rect_opt->size;
        }
        else if (_context)
        {
            _sprite_size = _context->getResourceManager().getTextureSize(_sprite.getTextureId());
        }

        _sprite.setSize(_sprite_size);
        spdlog::trace("Sprite 尺寸更新: {}x{}", _sprite_size.x, _sprite_size.y);
    }
}