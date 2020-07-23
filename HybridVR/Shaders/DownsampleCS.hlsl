RWTexture2D<float3> LeftMip : register(u0);
RWTexture2D<float3> RightMip : register(u1);
Texture2DArray<float3> LowPassImage : register(t0);

SamplerState Sampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	float nTextureWidth;
	float nTextureHeight;
	LeftMip.GetDimensions(nTextureWidth, nTextureHeight);
	
	float3 uv = float3(DTid.x / nTextureWidth, DTid.y / nTextureHeight, 0);
    LeftMip[uint2(DTid.xy)] = LowPassImage.SampleLevel(Sampler, uv, 0);

	uv.z = 1;
    RightMip[uint2(DTid.xy)] = LowPassImage.SampleLevel(Sampler, uv, 0);
}
