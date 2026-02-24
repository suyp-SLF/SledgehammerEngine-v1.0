#pragma once
#include "../render/sprite.h"
#include "component.h"
#include <vector>
#include <glm/glm.hpp>
namespace engine::render
{
    class Sprite;
} // namespace engine::render
namespace engine::core
{
    class Context;
}
namespace engine::component
{
    /**
     * @brief 定义瓦片类型
     */
    enum class TileType
    {
        Empty = 0,  // 空瓦片
        Normal = 1, // 普通瓦片
        SOLID = 2,  // 固体瓦片
    };
    /**
     * @brief 单个瓦片的渲染和逻辑信息
     */
    struct TileInfo
    {
        render::Sprite sprite; // 假设 Sprite 内部自持纹理句柄或引用计数
        TileType type;         // 瓦片类型

        // 建议使用初始化列表，并修复参数名冲突
        TileInfo(const render::Sprite &s = render::Sprite(), TileType t = TileType::Empty)
            : sprite(s), type(t) {}
    };
    class TilelayerComponent : public Component
    {
        friend class engine::object::GameObject;

        /** * @brief 内部脏标记位掩码 */
        enum SpriteDirtyFlags : uint8_t
        {
            CLEAN        = 0,
            DIRTY_SIZE   = 1 << 0,
            DIRTY_OFFSET = 1 << 1
        };
    public:
        /**
         * @brief 无参构造函数
         */
        TilelayerComponent() = default;
        /**
         * @brief 构造函数，初始化瓦片层
         * @param tile_size 瓦片大小
         * @param map_size 地图大小
         * @param tiles 瓦片信息 （会被移动）
         * @note tiles 会被移动，因此调用者不应再使用该参数
         */
        TilelayerComponent(glm::ivec2 tile_size, glm::ivec2 map_size, std::vector<TileInfo> &&tiles);
        /**
         * @brief 析构函数
         */
        ~TilelayerComponent() override;
        /**
         * @brief 获取指定位置的瓦片信息
         * @param position 瓦片位置
         * @return 瓦片信息
         *  @note 如果位置超出地图范围，返回 nullptr
         * @note 返回的指针指向的是 TilelayerComponent 内部的数据，不应被修改
         */
        const TileInfo *getTileInfoAt(glm::ivec2 position) const;
        /**
         * @brief 获取指定位置的瓦片类型
         * @param position 瓦片位置
         * @return 瓦片类型
         *  @note 如果位置超出地图范围，返回 TileType::Empty
         */
        TileType getTileTypeAt(glm::ivec2 position) const;
        /**
         * @brief 获取指定世界坐标的瓦片类型
         * @param position 世界坐标
         * @return 瓦片类型
         *  @note 如果位置超出地图范围，返回 TileType::Empty
         */
        TileType getTileTypeAtWorldPos(glm::vec2 world_position) const;
        // GETTER
        const glm::ivec2 &getTileSize() const { return _tile_size; }
        const glm::ivec2 &getMapSize() const { return _map_size; }
        const glm::vec2 &getOffset() const { return _offset; }
        const std::vector<TileInfo> &getTiles() const { return _tiles; }
        bool isHidden() const { return _is_hidden; }
        const glm::ivec2 getWorldSize() const
        {
            return _tile_size * _map_size;
        }
        // SETTER
        void setOffset(const glm::vec2 &offset) { _offset = offset; }
        void setHidden(bool hidden) { _is_hidden = hidden; }

        // --- 核心渲染流水线 ---
        
        /** * @brief 提交渲染命令 */
        void draw(engine::core::Context &ctx);

        /** * @brief 确保资源与偏移量在渲染前已就绪（Lazy Evaluation） */
        void ensureResourcesReady();
    protected:
        // 核心循环方法
        void init() override;
        void update(float delta_time) override {};
        void render() override {};
    private:
        glm::ivec2 _tile_size;            // 瓦片大小
        glm::ivec2 _map_size;             // 地图大小
        std::vector<TileInfo> _tiles;     // 瓦片信息
        glm::vec2 _offset = {0.0f, 0.0f}; // 地图偏移量

        // --- 状态追踪 ---
        uint8_t  _dirty_flags = CLEAN;
        uint32_t _last_transform_version = 0xFFFFFFFF; // 追踪 Transform 是否变动

        bool _is_hidden = false; // 是否隐藏，不渲染
    };
}