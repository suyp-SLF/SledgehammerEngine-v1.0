#pragma once
#include <glm/glm.hpp>

namespace engine::render
{
    /**
     * @brief 顶点数据结构
     * 用于描述四边形的每一个点
     */
    struct Vertex {
        glm::vec2 pos; // 位置
        glm::vec2 uv;  // 纹理坐标
    };

    /**
     * @brief 推送常量 (Push Constants)
     * 用于从 CPU 快速传递每一帧都在变化的变换矩阵和颜色信息
     * ⚡️ 注意：SDL3 GPU 要求 Push Constant 的总大小通常不超过 128 字节
     */
    struct SpritePushConstants {
        glm::mat4 mvp;   // 64 字节 (4x4 矩阵)
        glm::vec4 color; // 16 字节 (RGBA)
        glm::vec4 uv_rect; // 对应 vec4 uv_rect
        // 当前已用 80 字节，还剩 48 字节可用于 UV 动画等扩展
    };

    /**
     * @brief 纹理采样模式 (可选扩展)
     */
    enum class SamplerMode {
        NEAREST, // 像素风
        LINEAR   // 平滑
    };
} // namespace engine::render