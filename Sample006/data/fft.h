// require defines
// LENGTH : row or collumn pixel length
// BUTTERFLY_COUNT : butterfly pass count
// ROWPASS : 1 is row pass, 0 is collumn pass
// TRANSFORM_INVERSE : 1 is ifft, 0 is fft

precision highp float;

layout(local_size_x = LENGTH) in;
#if ROWPASS && !TRANSFORM_INVERSE
layout(binding = 0, rgba8) uniform readonly image2D inputImageR;
#else
layout(binding = 0, rgba16f) uniform readonly image2D inputImageR;
#endif
layout(binding = 1, rgba16f) uniform readonly image2D inputImageI;
layout(binding = 2, rgba16f) uniform image2D outputImageR;
layout(binding = 3, rgba16f) uniform image2D outputImageI;

#define PI 3.14159265

void GetButterflyValues(uint passIndex, uint x, out uvec2 indices, out vec2 weights)
{
	uint sectionWidth = 2 << passIndex;
	uint halfSectionWidth = sectionWidth / 2;

	uint sectionStartOffset = x & ~(sectionWidth - 1);
	uint halfSectionOffset = x & (halfSectionWidth - 1);
	uint sectionOffset = x & (sectionWidth - 1);

	float a = 2.0 * PI * float(sectionOffset) / float(sectionWidth);
	weights.y = sin(a);
	weights.x = cos(a);
	weights.y = -weights.y;

	indices.x = sectionStartOffset + halfSectionOffset;
	indices.y = sectionStartOffset + halfSectionOffset + halfSectionWidth;

	if (passIndex == 0)
	{
		indices = bitfieldReverse(indices) >> (32 - BUTTERFLY_COUNT) & (LENGTH - 1);
	}
}

shared highp vec3 pingPongArray[4][LENGTH];
void ButterflyPass(uint passIndex, uint x, uint t0, uint t1, out vec3 resultR, out vec3 resultI)
{
	uvec2 Indices;
	vec2 Weights;
	GetButterflyValues(passIndex, x, Indices, Weights);

	vec3 inputR1 = pingPongArray[t0][Indices.x];
	vec3 inputI1 = pingPongArray[t1][Indices.x];

	vec3 inputR2 = pingPongArray[t0][Indices.y];
	vec3 inputI2 = pingPongArray[t1][Indices.y];

#if TRANSFORM_INVERSE
	resultR = (inputR1 + Weights.x * inputR2 + Weights.y * inputI2) * 0.5;
	resultI = (inputI1 - Weights.y * inputR2 + Weights.x * inputI2) * 0.5;
#else
	resultR = inputR1 + Weights.x * inputR2 - Weights.y * inputI2;
	resultI = inputI1 + Weights.y * inputR2 + Weights.x * inputI2;
#endif
}

void ButterflyPassFinalNoI(uint passIndex, uint x, uint t0, uint t1, out vec3 resultR)
{
	uvec2 Indices;
	vec2 Weights;
	GetButterflyValues(passIndex, x, Indices, Weights);

	vec3 inputR1 = pingPongArray[t0][Indices.x];

	vec3 inputR2 = pingPongArray[t0][Indices.y];
	vec3 inputI2 = pingPongArray[t1][Indices.y];

	resultR = (inputR1 + Weights.x * inputR2 + Weights.y * inputI2) * 0.5;
}

void main()
{
	uvec2 position = uvec2(gl_GlobalInvocationID.xy);
#if ROWPASS
	uvec2 texturePos = position.xy;
	uvec2 outPos = texturePos;
#else
	uvec2 texturePos = position.yx;
	uvec2 outPos = texturePos;
#endif

	// Load entire row or column into scratch array
	vec4 inputR = imageLoad(inputImageR, ivec2(texturePos));
	pingPongArray[0][position.x].xyz = inputR.xyz;
#if ROWPASS && !TRANSFORM_INVERSE
	// don't load values from the imaginary texture when loading the original texture
	pingPongArray[1][position.x].xyz = vec3(0.0);
#else
	pingPongArray[1][position.x].xyz = imageLoad(inputImageI, ivec2(texturePos)).xyz;
#endif

	uvec4 textureIndices = ivec4(0, 1, 2, 3);

	for (int i = 0; i < BUTTERFLY_COUNT - 1; i++)
	{
		groupMemoryBarrier();
		barrier();
		ButterflyPass(i, position.x, textureIndices.x, textureIndices.y, pingPongArray[textureIndices.z][position.x].xyz, pingPongArray[textureIndices.w][position.x].xyz);
		textureIndices.xyzw = textureIndices.zwxy;
	}

	// Final butterfly will write directly to the target texture
	groupMemoryBarrier();
	barrier();

	// The final pass writes to the output UAV texture
#if !ROWPASS && TRANSFORM_INVERSE
	// last pass of the inverse transform. The imaginary value is no longer needed
	vec3 outputR;
	ButterflyPassFinalNoI(BUTTERFLY_COUNT - 1, position.x, textureIndices.x, textureIndices.y, outputR);
	imageStore(outputImageR, ivec2(outPos), vec4(outputR.rgb, inputR.a));
#else
	vec3 outputR, outputI;
	ButterflyPass(BUTTERFLY_COUNT - 1, position.x, textureIndices.x, textureIndices.y, outputR, outputI);
	imageStore(outputImageR, ivec2(outPos), vec4(outputR.rgb, inputR.a));
	imageStore(outputImageI, ivec2(outPos), vec4(outputI.rgb, inputR.a));
#endif
}