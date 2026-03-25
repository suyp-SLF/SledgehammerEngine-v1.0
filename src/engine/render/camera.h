#pragma once
#include "../utils/math.h"

namespace engine::render
{
    class Camera final
    {
    private:
        enum class ProjectionMode
        {
            Flat2D,
            Pseudo3D,
        };

        float _zoom = 1; // 缩放比例
        glm::vec2 _viewport_size;
        glm::vec2 _position;                               // 左上角坐标
        std::optional<engine::utils::FRect> _limit_bounds; // 限制范围
        ProjectionMode _projection_mode = ProjectionMode::Pseudo3D;
        float _pseudo3d_vertical_scale = 0.90f;

        glm::vec2* _follow_target = nullptr; // 跟随目标
        float _follow_smoothness = 5.0f; // 跟随平滑度

    public:
        Camera(const glm::vec2 &viewport_size,
               const glm::vec2 &position = glm::vec2(0.0f, 0.0f),
               const std::optional<engine::utils::FRect> limit_bounds = std::nullopt);

        void update(float delta_timer);
        void setFollowTarget(const glm::vec2* target, float smoothness = 5.0f);
        void move(const glm::vec2 &offset);

        /**
         * @brief 检查一个矩形包围盒是否在相机视口内
         * @param position 物体的世界坐标
         * @param size 物体的尺寸 (宽, 高)
         */
        bool isBoxInView(const glm::vec2& position, const glm::vec2& size) const;

        /**
         * @brief 获取视图矩阵 (View Matrix)
         * 处理相机的移动、旋转和缩放
         */
        glm::mat4 getViewMatrix() const; // TODO: 添加旋转和缩放

        /**
         * @brief 获取投影矩阵 (Projection Matrix)
         * 将像素坐标系映射到 GPU 的裁剪空间 (-1 到 1)
         */
        glm::mat4 getProjectionMatrix() const; // TODO: 添加透视投影

        glm::vec2 worldToScreen(const glm::vec2 &world_pos) const;
        glm::vec2 worldToScreenWithParallax(const glm::vec2 &world_pos, const glm::vec2 &parallax_factor) const; // 视差滚动背景
        glm::vec2 screenToWorld(const glm::vec2 &screen_pos) const;

        void setPosition(const glm::vec2 &position);
        void setLimitBounds(const std::optional<engine::utils::FRect> &limit_bounds);
        void setZoom(float zoom);
        void setPseudo3DEnabled(bool enabled);
        bool isPseudo3DEnabled() const { return _projection_mode == ProjectionMode::Pseudo3D; }

        const glm::vec2 &getPosition() const;
        std::optional<engine::utils::FRect> getLimitBounds() const; // 获取限制范围
        const glm::vec2 &getViewportSize() const;
        float getZoom() const;

    private:
        void clampPosition(); // 限制位置
    };
}; // namespace engine::render
