#version 450

layout(push_constant) uniform PushConstants {
    layout(offset = 0) vec2 pos0;
    layout(offset = 8) vec2 pos1;
    layout(offset = 16) vec2 pos2;
    // offset 24 to 31 is padding to ensure vec4 alignment
    layout(offset = 32) vec4 color;
} pc;

void main() {
    vec2 positions[3] = vec2[](pc.pos0, pc.pos1, pc.pos2);
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}