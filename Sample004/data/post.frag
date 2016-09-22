#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;

layout (binding = 1) uniform sampler2D samColor;

void main() 
{
	vec4 color = texture(samColor, inUV, 0);
	outFragColor = vec4(1.0 - color.rgb, color.a);
}
