#version 450
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D texSampler;

void main() {
    // 仅仅输出采样颜色，不做任何逻辑判断
    outColor = texture(texSampler, inUV);
}