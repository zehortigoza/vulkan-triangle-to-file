#version 450
#extension GL_ARB_separate_shader_objects : enable

// Input from the vertex shader
layout(location = 0) in vec4 inColor;

layout(location = 0) out vec4 outColor;

// Push constant block for conditional rendering
layout(push_constant) uniform PushConstants {
    vec4 positions[3];
    vec4 color;
    vec4 vertex_offset;
    vec4 color_offset;
    uint test;
    uint use_buffer;
} push_consts;

void main() {
    vec4 base_color = inColor;
    //vec4 color_offset_internal = vec4(1.0f, 0.0f, 1.0f, 0);

    //if (push_consts.test == 24)
    //    base_color = base_color + color_offset_internal;

    outColor = base_color;
}