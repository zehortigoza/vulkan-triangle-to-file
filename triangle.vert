#version 450
#extension GL_ARB_separate_shader_objects : enable

// Push constant block matching the C struct
layout(push_constant) uniform PushConstants {
    vec4 positions[3];
    vec4 color;
} push_consts;

void main() {
    gl_Position = push_consts.positions[gl_VertexIndex];
}