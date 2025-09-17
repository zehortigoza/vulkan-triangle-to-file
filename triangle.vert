#version 450
#extension GL_ARB_separate_shader_objects : enable

// Input vertex attributes from the vertex buffer
layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColor;

// Output to the fragment shader
layout(location = 0) out vec4 outColor;

void main() {
    gl_Position = inPosition;
    outColor = inColor; // Pass color to the fragment shader
}