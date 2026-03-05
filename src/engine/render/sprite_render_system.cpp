#include "sprite_render_system.h"
#include "../component/sprite_component.h"
#include "../component/transform_component.h"
#include "../core/context.h"
#include "renderer.h"
#include "camera.h"

namespace engine::render
{
    /**
     * @brief 渲染所有可见的精灵组件
     *
     * 该方法负责批量渲染场景中的所有精灵组件，包括：
     * 1. 获取渲染后端和相机引用
     * 2. 遍历所有精灵组件进行渲染
     * 3. 对每个精灵进行可见性检查和变换计算
     * 4. 通过抽象渲染接口执行实际绘制
     *
     * @param ctx 引擎上下文对象，提供渲染器和相机访问
     *
     * @note 该方法会跳过隐藏的精灵组件和没有变换组件的精灵
     * @note 渲染时考虑了精灵的偏移、缩放和旋转变换
     * @note 通过抽象渲染接口实现跨平台渲染（SDL/Vulkan等）
     */
    void SpriteRenderSystem::renderAll(engine::core::Context &ctx)
    {
        auto &renderer = ctx.getRenderer();
        auto &camera = ctx.getCamera();

        for (auto *comp : _sprites)
        {
            if (!comp || comp->isHidden())
                continue;

            // 1. 确保资源和偏移量计算完毕
            comp->ensureResourcesReady();

            // 这样可以避免第一帧因为数据不全而闪烁出“全图”
            if (comp->getSpriteSize().x <= 0)
            {
                continue;
            }

            auto *transform = comp->getTransformComp();
            if (!transform)
                continue;

            // 2. 视口剔除 (Frustum Culling)
            // 关键点：使用 getSpriteSize() 而不是纹理原始大小
            if (!camera.isBoxInView(transform->getPosition(), comp->getSpriteSize()))
                continue;

            // 3. 执行绘制
            comp->draw(ctx);
        }
    }
}