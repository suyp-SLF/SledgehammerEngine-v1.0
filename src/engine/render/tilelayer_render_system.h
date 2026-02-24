#pragma once
#include <vector>
#include <algorithm>

namespace engine::core { class Context; }
namespace engine::component { class TilelayerComponent; }

namespace engine::render
{
    class TilelayerRenderSystem
    {
    private:
        // 核心：存储所有存活的精灵组件指针
        // 在内存中连续排列，对 CPU 缓存极其友好
        std::vector<engine::component::TilelayerComponent*> _tilelayers;

    public:
        TilelayerRenderSystem() = default;
        ~TilelayerRenderSystem() = default;

        // 禁止拷贝
        TilelayerRenderSystem(const TilelayerRenderSystem&) = delete;
        TilelayerRenderSystem& operator=(const TilelayerRenderSystem&) = delete;

        // 注册与注销逻辑
        void registerComponent(engine::component::TilelayerComponent* tilelayer) {
            _tilelayers.push_back(tilelayer);
        }

        void unregisterComponent(engine::component::TilelayerComponent* tilelayer) {
            _tilelayers.erase(std::remove(_tilelayers.begin(), _tilelayers.end(), tilelayer), _tilelayers.end());
        }

        // 高性能批量渲染函数
        void renderAll(engine::core::Context& ctx);
    };
}