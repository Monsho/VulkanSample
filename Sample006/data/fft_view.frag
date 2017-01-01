#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec4 inColor;
layout (location = 1) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;

layout (binding = 1) uniform sampler2D texR;
layout (binding = 2) uniform sampler2D texI;

void main() 
{
	vec4 r = texture(texR, inUV - 0.5, 0);
	vec4 i = texture(texI, inUV - 0.5, 0);
	outFragColor.rgb = r.rgb * r.rgb + i.rgb * i.rgb;
	outFragColor.rgb = 0.1 * log2(sqrt(outFragColor.rgb));
	outFragColor.a = 1.0;
}