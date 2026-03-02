#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    layout(offset = 0) vec2 pos0;
    layout(offset = 8) vec2 pos1;
    layout(offset = 16) vec2 pos2;
    layout(offset = 32) vec4 color;
} pc;

void main() {
    outColor = pc.color;
}