#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) out vec2 outUV;

out gl_PerVertex
{
    vec4 gl_Position;
};


void main()
{
	float x = (gl_VertexIndex & 0x1) == 0 ? 0.0 : 1.0;
	float y = (gl_VertexIndex & 0x2) == 0 ? 0.0 : 1.0;

	outUV = vec2(x, 0.0 - y);
	gl_Position = vec4(vec2(x, y) * 2.0 - 1.0, 0.0, 1.0);
}
