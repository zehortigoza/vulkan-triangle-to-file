#version 450
#extension GL_ARB_separate_shader_objects : enable

// Input vertex attributes from the vertex buffer
layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColor;

// Output to the fragment shader
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
        // Use data from the VkBuffer
        gl_Position = inPosition;
        outColor = inColor;
    } else {
        // Use data from the push constants
        gl_Position = push_consts.positions[gl_VertexIndex];
        outColor = push_consts.color;
    }
}