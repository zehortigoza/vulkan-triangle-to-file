#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// Bindless array: binding 0, set 0. Unbounded array.
layout(set = 0, binding = 0) uniform sampler2D globalTextures[];

layout(push_constant) uniform PushConsts {
    int textureID;
} push;

void main() {
    // Access texture at index 'textureID'
    outColor = texture(globalTextures[nonuniformEXT(push.textureID)], fragTexCoord);
}