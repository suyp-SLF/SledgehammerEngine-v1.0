#include "tilelayer_component.h"
#include "../core/context.h"
#include "../render/renderer.h"
#include "../resource/resource_manager.h"
#include "../render/tilelayer_render_system.h"
#include <spdlog/spdlog.h>
namespace engine::component
{
    TilelayerComponent::TilelayerComponent(glm::ivec2 tile_size, glm::ivec2 map_size, std::vector<TileInfo> &&tiles)
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
        // 性能优化：获取当前摄像机的裁剪区域（可选实现）
        // 只绘制屏幕内的 Tile 可以大幅提升性能

        for (int y = 0; y < _map_size.y; ++y)
        {
            for (int x = 0; x < _map_size.x; ++x)
            {
                size_t index = y * _map_size.x + x;
                if (index < _tiles.size() && _tiles[index].type != TileType::Empty)
                {
                    const auto &tile_info = _tiles[index];

                    // 1. 计算基础位置 (基于 Tile 坐标)
                    glm::vec2 pos = {
                        _offset.x + static_cast<float>(x * _tile_size.x),
                        _offset.y + static_cast<float>(y * _tile_size.y)};

                    // 2. 处理非常规尺寸（如装饰物比格位大）
                    glm::vec2 sprite_size = tile_info.sprite.getSize();
                    if (glm::ivec2(sprite_size) != _tile_size)
                    {
                        // 居中对齐偏移
                        pos += (glm::vec2(_tile_size) - sprite_size) * 0.5f;
                    }

                    // 3. ⚡️ 核心修正：像素对齐
                    // 在调用 drawSprite 之前，确保位置是整数，防止采样漂移产生白线
                    pos = glm::floor(pos);

                    // 4. 执行绘制
                    // 此时 renderer 内部会根据 tile_info.sprite.getSourceRect()
                    // 自动计算 uv_rect 传给 Shader
                    ctx.getRenderer().drawSprite(ctx.getCamera(), tile_info.sprite, pos);
                }
            }
        }
    }
    void TilelayerComponent::ensureResourcesReady()
    {
        // 如果瓦片大小无效，直接返回
        if (_tile_size.x <= 0 || _tile_size.y <= 0)
            return;

        // ⚡️ 修正：瓦片层的资源准备通常只需要确保 Tileset 的纹理已加载
        // 我们可以抽样检查第一个非空瓦片的纹理
        if (_dirty_flags & DIRTY_SIZE)
        {
            for (const auto &tile : _tiles)
            {
                if (tile.type != TileType::Empty)
                {
                    glm::vec2 tex_size = _context->getResourceManager().getTextureSize(tile.sprite.getTextureId());
                    if (tex_size.x > 0)
                    {
                        _dirty_flags &= ~DIRTY_SIZE;
                        spdlog::debug("TilelayerComponent: Tileset 资源已就绪");
                        break;
                    }
                }
            }
        }
    }

    const TileInfo *TilelayerComponent::getTileInfoAt(glm::ivec2 position) const
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
        const TileInfo *tile_info = getTileInfoAt(position);
        return tile_info ? tile_info->type : TileType::Empty;
    }

    TileType TilelayerComponent::getTileTypeAtWorldPos(glm::vec2 world_position) const
    {
        glm::vec2 relative_pos = world_position - _offset;
        glm::ivec2 tile_pos = {
            static_cast<int>(std::floor(relative_pos.x / _tile_size.x)),
            static_cast<int>(std::floor(relative_pos.y / _tile_size.y))};
        return getTileTypeAt(tile_pos);
    }
}