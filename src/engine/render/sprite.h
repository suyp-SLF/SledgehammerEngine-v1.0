#pragma once

#include "../utils/math.h"
#include <string>
#include <optional>
namespace engine::render
{
    class Sprite final
    {
    private:
        std::string _texture_id;
        // 需要截取的纹理区域
        std::optional<engine::utils::FRect> _source_rect;
        // 全图大小
        glm::vec2 _size = {0.0f, 0.0f};
        bool _is_flipped = false;

    public:
        /**
         * 默认构造函数
         */
        Sprite() = default;
        /**
         * 构造函数
         */
        Sprite(const std::string &texture_id, const std::optional<engine::utils::FRect> &source_rect = std::nullopt, bool is_flipped = false)
            : _texture_id(texture_id),
              _source_rect(source_rect),
              _is_flipped(is_flipped),
              _size(0, 0)
        {
        }

        // GETTER
        const std::string &getTextureId() const { return _texture_id; }
        const std::optional<engine::utils::FRect> &getSourceRect() const { return _source_rect; }
        glm::vec2 getSize() const
        {
            // 如果手动设置了逻辑大小，优先返回
            if (_size.x > 0.0f && _size.y > 0.0f) return _size;
            return {0.0f, 0.0f};
        };
        bool isFlipped() const { return _is_flipped; }
        // SETTER
        void setTextureId(const std::string &texture_id) { _texture_id = texture_id; }
        void setSourceRect(const std::optional<engine::utils::FRect> &source_rect) { _source_rect = source_rect; }
        void setFlipped(bool is_flipped) { _is_flipped = is_flipped; }
        void setSize(const glm::vec2 &size)
        {
            _size = size;
        }
    };
};