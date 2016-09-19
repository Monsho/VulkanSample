#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec2 inUV;

layout (binding = 0) uniform SceneData
{
	mat4 mtxView;
	mat4 mtxProj;
} uScene;

layout (push_constant) uniform MeshData
{
	mat4 mtxWorld;
} uMesh;

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec2 outUV;

out gl_PerVertex
{
    vec4 gl_Position;
};


void main()
{
	outColor = inColor;
	outUV = inUV;
	gl_Position = uScene.mtxProj * uScene.mtxView * uMesh.mtxWorld * vec4(inPos.xyz, 1.0);
}
