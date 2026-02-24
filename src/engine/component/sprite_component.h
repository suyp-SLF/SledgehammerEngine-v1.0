#pragma once

#include <glm/vec2.hpp>
#include <string>
#include <optional>
#include <cstdint>

#include "./component.h"
#include "../utils/alignment.h"
#include "../render/sprite.h"
#include "../utils/math.h"

// --- 前向声明 ---
namespace engine::core { class Context; }
namespace engine::resource { class ResourceManager; }

namespace engine::component
{
    class TransformComponent;

    /**
     * @brief 精灵渲染组件
     * 职责：持有纹理引用、处理对齐偏移、维护渲染状态。
     * 优化：利用脏标记（Dirty Flags）和变换版本号（Version）实现按需更新。
     */
    class SpriteComponent final : public engine::component::Component
    {
        // 允许 GameObject 进行生命周期注入
        friend class engine::object::GameObject;

        /** * @brief 内部脏标记位掩码 */
        enum SpriteDirtyFlags : uint8_t
        {
            CLEAN        = 0,
            DIRTY_SIZE   = 1 << 0,
            DIRTY_OFFSET = 1 << 1
        };

    public:
        // --- 构造与析构 ---
        SpriteComponent(engine::render::Sprite &&sprite);
        SpriteComponent(const std::string &texture_id,
                        engine::utils::Alignment alignment = engine::utils::Alignment::NONE,
                        std::optional<engine::utils::FRect> source_rect_opt = std::nullopt,
                        bool is_flipped = false);
        
        ~SpriteComponent() override;

        // 显式禁止拷贝与移动
        SpriteComponent(const SpriteComponent &) = delete;
        SpriteComponent &operator=(const SpriteComponent &) = delete;
        SpriteComponent(SpriteComponent &&) = delete;
        SpriteComponent &operator=(SpriteComponent &&) = delete;

        // --- 核心渲染流水线 ---
        
        /** * @brief 提交渲染命令 */
        void draw(engine::core::Context &ctx);

        /** * @brief 确保资源与偏移量在渲染前已就绪（Lazy Evaluation） */
        void ensureResourcesReady();

        // --- Getter ---
        const engine::render::Sprite& getSprite()       const { return _sprite; }
        const std::string&            getTextureId()    const { return _sprite.getTextureId(); }
        const glm::vec2               getSpriteSize()   const { return _sprite_size; }
        const glm::vec2               getOffset()       const { return _offset; }
        engine::utils::Alignment      getAlignment()    const { return _alignment; }
        TransformComponent* getTransformComp() const { return _transform_comp; }
        bool                          isFlipped()       const { return _sprite.isFlipped(); }
        bool                          isHidden()        const { return _is_hidden; }

        // --- Setter (部分会触发脏标记) ---
        void setHidden(bool hidden)    { _is_hidden = hidden; }
        void setFlipped(bool flipped); // 实现内更新状态
        
        void setSpriteById(const std::string &texture_id, 
                           std::optional<engine::utils::FRect> source_rect_opt = std::nullopt);
        
        void setSourceRect(const std::optional<engine::utils::FRect> &source_rect_opt);
        void setAlignment(engine::utils::Alignment anchor);

    protected:
        // --- Component 生命周期重写 ---
        void init() override;
        void update(float delta_time) override;
        void render() override {} // 已交由 SpriteRenderSystem 统一管理

    private:
        // --- 私有辅助计算 ---
        void updateOffset();
        void updateSpriteSize();

        // --- 状态追踪 ---
        uint8_t  _dirty_flags = CLEAN;
        uint32_t _last_transform_version = 0xFFFFFFFF; // 追踪 Transform 是否变动

        // --- 核心成员数据 ---
        TransformComponent* _transform_comp = nullptr; // 缓存 Transform 指针
        engine::render::Sprite   _sprite;
        engine::utils::Alignment _alignment = engine::utils::Alignment::NONE;
        
        glm::vec2 _sprite_size = {0.0f, 0.0f};
        glm::vec2 _offset      = {0.0f, 0.0f};
        bool      _is_hidden   = false;
    };
}