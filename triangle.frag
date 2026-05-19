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
    vec4 base_color;

    if (push_consts.use_buffer == 0) {
        // Use interpolated color from the vertex shader (already includes offset)
        base_color = inColor;
    } else {
        // Use the color from the push constant and apply the offset
        base_color = push_consts.color;
    }

    vec4 final_color = base_color + push_consts.color_offset;

    // Clamp the final color to the valid [0.0, 1.0] range and output it
    outColor = clamp(final_color, 0.0, 1.0);
}