#version 450
#extension GL_ARB_separate_shader_objects : enable

// Input vertex attributes from the vertex buffer
layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColor;

// Output to the fragment shader
layout(location = 0) out vec4 outColor;

// Push constant block with separate offsets
layout(push_constant) uniform PushConstants {
    vec4 positions[3];
    vec4 color;
    vec4 vertex_offset;
    vec4 color_offset;
    uint test;
    uint use_buffer;
} push_consts;

void main() {
    outColor = inColor;
    gl_Position = inPosition;
}