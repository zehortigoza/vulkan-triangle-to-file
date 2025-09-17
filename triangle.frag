#version 450
#extension GL_ARB_separate_shader_objects : enable

// Input from the vertex shader
layout(location = 0) in vec4 inColor;

layout(location = 0) out vec4 outColor;

// Push constant block for conditional rendering
layout(push_constant) uniform PushConstants {
    vec4 positions[3];
    vec4 color;
    uint test;
    uint use_buffer;
} push_consts;

void main() {
    if (push_consts.use_buffer == 0) {
        // Use interpolated color from the vertex shader (originating from the buffer)
        outColor = inColor;
    } else {
        // Use the color from the push constant block
        outColor = push_consts.color;
    }
}