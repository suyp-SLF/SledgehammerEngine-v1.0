#include "tilelayer_component.h"
#include "../core/context.h"
#include "../render/renderer.h"
#include "../resource/resource_manager.h"
#include "../render/tilelayer_render_system.h"
#include <spdlog/spdlog.h>
namespace engine::component
{
    TilelayerComponent::TilelayerComponent(glm::ivec2 tile_size, glm::ivec2 map_size, std::vector<TileData> &&tiles)
        : _tile_size(tile_size),
          _map_size(map_size),
          _tiles(std::move(tiles))
    {
        if (_tiles.size() != static_cast<size_t>(map_size.x * map_size.y))
        {
            spdlog::error("TilelayerComponent: 地图大小与瓦片数量不匹配，瓦片数据将被重置");
            _tiles.clear();
            _map_size = {0, 0};
        }
        spdlog::trace("TilelayerComponent: 创建瓦片层组件完成");
    }

    TilelayerComponent::~TilelayerComponent()
    {
        // ⚡️ 必须在析构时注销，防止渲染系统持有野指针
        if (_context)
        {
            _context->getTilelayerRenderSystem().unregisterComponent(this);
            spdlog::trace("SpriteComponent 已从渲染系统中注销");
        }
    }
    void TilelayerComponent::init()
    {
        if (!_owner || !_context)
        {
            spdlog::error("TilelayerComponent 初始化失败：_owner 或 _context 未绑定");
            return;
        }
        // 注册到渲染系统
        _context->getTilelayerRenderSystem().registerComponent(this);

        // 标记初始状态为脏，确保第一次 ensureResourcesReady 时执行计算
        _dirty_flags |= (DIRTY_SIZE | DIRTY_OFFSET);
    }
    void TilelayerComponent::draw(engine::core::Context &ctx)
    {
        // ⚡️ 修复报错：不再循环调用 drawSprite
        // 而是将整个瓦片数组、地图尺寸、纹理 ID 一次性交给渲染器
        ctx.getRenderer().drawTileMap(
            ctx.getCamera(),
            _map_size,
            glm::vec2(_tile_size),
            _tiles,
            _offset);
    }
    void TilelayerComponent::ensureResourcesReady()
    {
        if (!(_dirty_flags & DIRTY_SIZE) || _tiles.size() == 0)
            return;
    }

    const TileData *TilelayerComponent::getTileDataAt(glm::ivec2 position) const
    {
        if (position.x < 0 || position.y < 0 || position.x >= _map_size.x || position.y >= _map_size.y)
        {
            spdlog::warn("TilelayerComponent: 获取瓦片信息时，位置超出地图范围");
            return nullptr;
        }
        size_t index = static_cast<size_t>(position.y * _map_size.x + position.x);
        // 瓦片索引不能越界
        if (index < _tiles.size())
        {
            return &_tiles[index];
        }
        spdlog::warn("TilelayerComponent: 获取瓦片信息 {} 时，瓦片索引越界", index);
        return nullptr;
    }
    TileType TilelayerComponent::getTileTypeAt(glm::ivec2 position) const
    {
        const TileData *tile_info = getTileDataAt(position);
        return tile_info ? tile_info->type : TileType::Empty;
    }

    TileType TilelayerComponent::getTileTypeAtWorldPos(glm::vec2 world_position) const
    {
        // 建议：如果 GameObject 有 Transform，应该用 Transform 的坐标
        // glm::vec2 origin = _owner->getComponent<TransformComponent>()->getPosition();
        glm::vec2 relative_pos = world_position - _offset;

        // 使用 floor 处理负坐标情况（比如相机抖动或边界外检测）
        glm::ivec2 tile_pos = {
            static_cast<int>(std::floor(relative_pos.x / (float)_tile_size.x)),
            static_cast<int>(std::floor(relative_pos.y / (float)_tile_size.y))};
        return getTileTypeAt(tile_pos);
    }
}