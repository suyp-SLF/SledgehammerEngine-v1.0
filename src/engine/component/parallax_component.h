#pragma once

#include <string>
#include <glm/vec2.hpp>
#include <cstdint>

#include "component.h"
#include "../render/sprite.h"

namespace engine::component
{
    class TransformComponent;

    /**
     * @brief 视差滚动组件
     * 职责：通过滚动因子（Scroll Factor）实现背景与相机的相对位移，营造深度感。
     * 协作：ParallaxRenderSystem 将利用此处的 _scroll_factor 计算最终渲染位置。
     */
    class ParallaxComponent final : public Component
    {
        friend class engine::object::GameObject;

        /** * @brief 内部脏标记位掩码 */
        enum ParallaxDirtyFlags : uint8_t
        {
            CLEAN        = 0,
            DIRTY_SIZE   = 1 << 0,
            DIRTY_OFFSET = 1 << 1
        };
    public:
        // --- 构造与析构 ---
        ParallaxComponent(const std::string& texture_id,
                          const glm::vec2& scroll_factor = glm::vec2(1.0f, 1.0f),
                          glm::bvec2 repeat = glm::bvec2(false, false));

        virtual ~ParallaxComponent() override;

        // 禁止拷贝与移动
        ParallaxComponent(const ParallaxComponent&) = delete;
        ParallaxComponent& operator=(const ParallaxComponent&) = delete;

        // --- 核心渲染流水线 ---
        
        /** * @brief 提交渲染命令 */
        void draw(engine::core::Context &ctx);

        /** * @brief 确保资源与偏移量在渲染前已就绪（Lazy Evaluation） */
        void ensureResourcesReady();


        // --- Getter ---
        const engine::render::Sprite& getSprite()       const { return _sprite; }
        const glm::vec2&              getScrollFactor() const { return _scroll_factor; }
        glm::bvec2                    getRepeat()       const { return _repeat; }
        bool                          isHidden()        const { return _is_hidden; }
        TransformComponent* getTransformComp()const { return _transform_comp; }

        // --- Setter ---
        void setScrollFactor(const glm::vec2& scroll_factor) { _scroll_factor = scroll_factor; }
        void setRepeat(const glm::bvec2& repeat)             { _repeat = repeat; }
        void setHidden(bool is_hidden)                       { _is_hidden = is_hidden; }
        
        // 通常视差背景的纹理更换频率较低，但保留此接口以支持动态切换背景
        void setTexture(const std::string& texture_id);

    protected:
        // --- 生命周期重写 ---
        void init() override;
        void update(float delta_time) override;
        void render() override {} // 视差渲染由专门的 ParallaxRenderSystem 负责

    private:
        // --- 状态追踪 ---
        uint32_t _last_transform_version = 0xFFFFFFFF;
        TransformComponent* _transform_comp = nullptr;

        uint8_t _dirty_flags = DIRTY_SIZE | DIRTY_OFFSET;

        // --- 核心数据 ---
        engine::render::Sprite _sprite;
        
        /** * @brief 滚动因子
         * (0, 0) 表示随相机完全静止（如 UI 或远古星空）
         * (1, 1) 表示与相机同步移动（普通物体）
         * (0.5, 0.5) 表示移动速度是相机的一半，产生远景效果
         */
        glm::vec2 _scroll_factor;
        
        /** * @brief 是否循环重复
         * x: 水平循环，y: 垂直循环。常用于无限滚动的地面或云层。
         */
        glm::bvec2 _repeat;
        
        bool _is_hidden = false;
    };
} // namespace engine::component