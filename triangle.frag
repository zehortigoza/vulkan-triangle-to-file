#version 450
#extension GL_ARB_separate_shader_objects : enable

// Input from the vertex shader
layout(location = 0) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main() {
    // Set the output color to the interpolated color from the vertex shader
    outColor = inColor;
}