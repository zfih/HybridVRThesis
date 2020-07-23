RWTexture2D<float3> Mip : register(u0);
Texture2DArray<float3> LowPassImage : register(t0);

SamplerState Sampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	float nTextureWidth;
	float nTextureHeight;
	Mip.GetDimensions(nTextureWidth, nTextureHeight);
	
	float3 uv = float3(DTid.x / nTextureWidth, DTid.y / nTextureHeight, 0);
    Mip[uint2(DTid.xy)] = LowPassImage.SampleLevel(Sampler, uv, 0);
}
