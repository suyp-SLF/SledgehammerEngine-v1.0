#include "parallax_render_system.h"
#include "../component/parallax_component.h"
#include "../component/transform_component.h"
#include "../core/context.h"
#include "renderer.h"
#include "camera.h"

namespace engine::render
{
    void ParallaxRenderSystem::renderAll(engine::core::Context &ctx)
    {
        auto &renderer = ctx.getRenderer();
        auto &camera = ctx.getCamera();

        // 💡 提示：如果背景层级很多，可以考虑在这里按 Transform 的 Z 轴或手动定义的 Layer 排序
        // std::sort(_parallaxs.begin(), _parallaxs.end(), ...);

        for (auto *comp : _parallaxs)
        {
            // 1. 基础状态过滤
            if (!comp || comp->isHidden())
                continue;

            // 2. ⚡️ 核心：按需更新（把原本 update 里的逻辑搬到这）
            // 内部会检查 version 和 dirty_flags
            comp->ensureResourcesReady();

            auto *transform = comp->getTransformComp();
            if (!transform)
                continue;

            // 3. ⚡️ 视口剔除 (Frustum Culling) - 可选
            // 如果精灵在相机范围外，直接跳过 draw 调用，节省 GPU/CPU 开销
            if (!camera.isBoxInView(transform->getPosition(), comp->getSprite().getSize())) continue;

            // 4. 调用组件自身的 draw（或者直接在这里调用 renderer）
            // 建议统一调用 comp->draw(ctx)，因为组件最清楚自己的 offset 怎么加
            comp->draw(ctx);
        }
    }
}