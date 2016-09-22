#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;

layout (binding = 1) uniform sampler2D samColor;
layout (binding = 2) uniform sampler2D samDepth;

void main() 
{
	vec4 color = texture(samColor, inUV, 0);
	float depth = texture(samDepth, inUV, 0).r;
	outFragColor = (depth < 0.999) ? vec4(1.0 - color.rgb, color.a) : vec4(0, 0, 0, 0);
}
