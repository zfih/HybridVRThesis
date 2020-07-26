RWTexture2D<float3> LowResImage : register(u0);
Texture2DArray<float3> LowPassedImage : register(t0);
cbuffer consts : register(b0)
{
	uint cam;
}

SamplerState Sampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	float nTextureWidth;
	float nTextureHeight;
	LowResImage.GetDimensions(nTextureWidth, nTextureHeight);
	
	float3 uv = float3(DTid.x / nTextureWidth, DTid.y / nTextureHeight, cam);
	LowResImage[uint2(DTid.xy)] = LowPassedImage.SampleLevel(Sampler, uv, 0);
}
