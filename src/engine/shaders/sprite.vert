#version 450

// ⚡️ 必须声明这部分，对应 attributes[0] 和 attributes[1]
layout(location = 0) in vec2 a_pos; 
layout(location = 1) in vec2 a_uv;

layout(location = 0) out vec2 v_uv;

layout(push_constant) uniform Constants {
    mat4 mvp;
    vec4 color;
    vec4 uv_rect; // drawSprite 用，drawTileMap 时我们会传默认值
} pc;

void main() {
    // ⚡️ 确保使用的是 a_pos 而不是硬编码的数组
    gl_Position = pc.mvp * vec4(a_pos, 0.0, 1.0);
    v_uv = a_uv;
}