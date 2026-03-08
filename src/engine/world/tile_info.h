// tile_info.h
#pragma once

#include <string>
#include <glm/glm.hpp>

namespace engine::world
{
    /**
     * @brief 定义瓦片类型
     */
    enum class TileType : uint8_t
    {
        Air = 0,
        Stone = 1,
        Dirt = 2,
        // 后续可扩展，例如 Grass = 3,
    };

    /**
     * @brief 单个瓦片的渲染和逻辑信息
     */
    struct TileData
    {
        glm::vec4 uv_rect;      // 纹理坐标
        TileType type;          // 瓦片类型
        std::string texture_id; // 纹理ID（多图片模式）

        // 默认构造函数
        TileData() = default;

        // 新增：接受 TileType 的构造函数
        explicit TileData(TileType t)
            : uv_rect(0.0f, 0.0f, 1.0f, 1.0f), // 默认使用整张纹理
              type(t),
              texture_id("default")            // 默认纹理 ID
        {}

        // 原有的三参数构造函数
        TileData(glm::vec4 uv_rect, TileType type, std::string texture_id)
            : uv_rect(uv_rect), type(type), texture_id(std::move(texture_id)) {}
    };

} // namespace engine::world