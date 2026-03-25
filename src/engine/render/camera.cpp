#include "camera.h"
#include <spdlog/spdlog.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>

namespace engine::render
{
    Camera::Camera(const glm::vec2 &viewport_size, const glm::vec2 &position, const std::optional<engine::utils::FRect> limit_bounds)
        : _viewport_size(viewport_size),
          _position(position),
          _limit_bounds(limit_bounds)
    {
        spdlog::trace("Camera 初始化成功，位置: ({}, {}), 限制边界: {}, 大小: ({}, {})",
                      _position.x, _position.y,
                      _limit_bounds.has_value() ? "Has Value" : "None",
                      _viewport_size.x, _viewport_size.y);
    }

    void Camera::update(float delta_timer)
    {
        if (_follow_target)
        {
            glm::vec2 target_pos = *_follow_target - _viewport_size * 0.5f;
            _position = glm::mix(_position, target_pos, _follow_smoothness * delta_timer);
            clampPosition();
        }
    }

    void Camera::setFollowTarget(const glm::vec2* target, float smoothness)
    {
        _follow_target = const_cast<glm::vec2*>(target);
        _follow_smoothness = smoothness;
    }

    void Camera::move(const glm::vec2 &offset)
    {
        _position += offset;
        clampPosition();
    }

    bool Camera::isBoxInView(const glm::vec2 &position, const glm::vec2 &size) const
    {
        // 获取相机在世界空间中的可视边界
        float cam_left = _position.x;
        float cam_right = _position.x + _viewport_size.x;
        float cam_top = _position.y;
        float cam_bottom = _position.y + _viewport_size.y;

        // 物体的边界
        float obj_left = position.x;
        float obj_right = position.x + size.x;
        float obj_top = position.y;
        float obj_bottom = position.y + size.y;

        // AABB 碰撞检测逻辑：如果物体不完全在相机边界之外，则可见
        return !(obj_right < cam_left ||
                 obj_left > cam_right ||
                 obj_bottom < cam_top ||
                 obj_top > cam_bottom);
    }

    glm::mat4 Camera::getViewMatrix() const
    {
        glm::vec2 center = _viewport_size * 0.5f;
        glm::mat4 view = glm::mat4(1.0f);
        view = glm::translate(view, glm::vec3(center, 0.0f));
        float verticalScale = _projection_mode == ProjectionMode::Pseudo3D
            ? _pseudo3d_vertical_scale
            : 1.0f;
        view = glm::scale(view, glm::vec3(_zoom, _zoom * verticalScale, 1.0f));
        view = glm::translate(view, glm::vec3(-center, 0.0f));
        view = glm::translate(view, glm::vec3(-_position.x, -_position.y, 0.0f));
        return view;
    }
    glm::mat4 Camera::getProjectionMatrix() const
    {
        // 确保 near=0.0f, far=1.0f
        // 并且 Y 轴是从 0 到 height (向下)
        return glm::ortho(0.0f, _viewport_size.x, _viewport_size.y, 0.0f, 0.0f, 1.0f);
    }
    glm::vec2 Camera::worldToScreen(const glm::vec2 &world_pos) const
    {
        glm::vec2 center = _viewport_size * 0.5f;
        glm::vec2 local = world_pos - _position - center;
        float verticalScale = _projection_mode == ProjectionMode::Pseudo3D
            ? _pseudo3d_vertical_scale
            : 1.0f;
        return {
            center.x + local.x * _zoom,
            center.y + local.y * _zoom * verticalScale
        };
    }

    glm::vec2 Camera::worldToScreenWithParallax(const glm::vec2 &world_pos, const glm::vec2 &parallax_factor) const
    {
        glm::vec2 center = _viewport_size * 0.5f;
        glm::vec2 local = world_pos - _position * parallax_factor - center;
        float verticalScale = _projection_mode == ProjectionMode::Pseudo3D
            ? _pseudo3d_vertical_scale
            : 1.0f;
        return {
            center.x + local.x * _zoom,
            center.y + local.y * _zoom * verticalScale
        };
    }

    glm::vec2 Camera::screenToWorld(const glm::vec2 &screen_pos) const
    {
        glm::vec2 center = _viewport_size * 0.5f;
        float verticalScale = _projection_mode == ProjectionMode::Pseudo3D
            ? _pseudo3d_vertical_scale
            : 1.0f;
        return {
            (screen_pos.x - center.x) / _zoom + center.x + _position.x,
            (screen_pos.y - center.y) / (_zoom * verticalScale) + center.y + _position.y
        };
    }

    void Camera::setPosition(const glm::vec2 &position)
    {
        _position = position;
    }

    void Camera::setLimitBounds(const std::optional<engine::utils::FRect> &limit_bounds)
    {
        _limit_bounds = limit_bounds;
        clampPosition();
    }

    const glm::vec2 &Camera::getPosition() const
    {
        return _position;
    }

    std::optional<engine::utils::FRect> Camera::getLimitBounds() const
    {
        return _limit_bounds;
    }

    const glm::vec2 &Camera::getViewportSize() const
    {
        return _viewport_size;
    }

    void Camera::setZoom(float zoom)
    {
        _zoom = glm::clamp(zoom, 0.5f, 3.0f);
    }

    void Camera::setPseudo3DEnabled(bool enabled)
    {
        _projection_mode = enabled ? ProjectionMode::Pseudo3D : ProjectionMode::Flat2D;
    }

    float Camera::getZoom() const
    {
        return _zoom;
    }

    void Camera::clampPosition()
    {
        // 检查限制边界是否有效
        if (_limit_bounds.has_value() && _limit_bounds->size.x > 0 && _limit_bounds->size.y > 0)
        {
            // 计算相机位置在限制边界内的最大和最小值
            glm::vec2 min_pos = _limit_bounds->position;
            glm::vec2 max_pos = _limit_bounds->position + _limit_bounds->size - _viewport_size;

            // 将相机位置限制在最大和最小值之间
            max_pos.x = std::max(max_pos.x, min_pos.x);
            max_pos.y = std::max(max_pos.y, min_pos.y);

            _position.x = std::clamp(_position.x, min_pos.x, max_pos.x);
            _position.y = std::clamp(_position.y, min_pos.y, max_pos.y);
        }
    }
}