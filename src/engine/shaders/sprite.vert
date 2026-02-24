#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 color;
    vec4 uv_rect; // ⚡️ 新增：x=U偏移, y=V偏移, z=U宽度, w=V高度
} pc;

layout(location = 0) out vec2 outUV;

void main() {
    vec2 positions[6] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
        vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0)
    );

    vec2 pos = positions[gl_VertexIndex];

    // ⚡️ 核心修正：计算裁剪后的 UV
    // 公式：最终UV = (原始UV * 瓦片宽高) + 瓦片起始点
    outUV = (pos * pc.uv_rect.zw) + pc.uv_rect.xy;

    gl_Position = pc.mvp * vec4(pos, 0.0, 1.0);
}