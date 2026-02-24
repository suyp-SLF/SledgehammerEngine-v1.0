#include "tilelayer_render_system.h"
#include "../component/tilelayer_component.h"
#include "../core/context.h"
#include "renderer.h"
#include "camera.h"
namespace engine::render
{
    void TilelayerRenderSystem::renderAll(engine::core::Context &ctx)
    {
        // 1. 获取渲染后端和相机的引用（通过你刚才定义的抽象 Renderer）
        auto &renderer = ctx.getRenderer();
        auto &camera = ctx.getCamera();

        // ⚡️ 优化 A：渲染前排序 (Depth Sorting)
        // 如果不排序，后生成的对象永远在最前面，无法处理遮挡关系
        // std::sort(_sprites.begin(), _sprites.end(), [](auto* a, auto* b) {
        //     // 假设你在 SpriteComponent 里有个 getZIndex()
        //     return a->getZIndex() < b->getZIndex();
        // });
        // 2. 批量遍历精灵（线性内存访问）
        for (auto *comp : _tilelayers)
        {
            // 1. 基础状态过滤
            if (!comp || comp->isHidden())
                continue;

            // 2. ⚡️ 核心：按需更新（把原本 update 里的逻辑搬到这）
            // 内部会检查 version 和 dirty_flags
            comp->ensureResourcesReady();

            // 3. ⚡️ 视口剔除 (Frustum Culling) - 可选
            // 如果精灵在相机范围外，直接跳过 draw 调用，节省 GPU/CPU 开销
            // if (!camera.isBoxInView(transform->getPosition(), comp->getSpriteSize())) continue;

            // 4. 调用组件自身的 draw（或者直接在这里调用 renderer）
            // 建议统一调用 comp->draw(ctx)，因为组件最清楚自己的 offset 怎么加
            comp->draw(ctx);
        }
    }
}
