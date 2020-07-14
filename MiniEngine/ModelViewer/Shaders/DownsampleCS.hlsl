
RWTexture2D<float3> LeftMip : register(u0);
RWTexture2D<float3> RightMip : register(u1);
RWTexture2DArray<float3> LowResImage : register(u2);

SamplerState Sampler : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	float nTextureWidth;
	float nTextureHeight;
	float elements;
	LowResImage.GetDimensions(nTextureWidth, nTextureHeight, elements);
	
	float3 uv = float3(DTid.x / nTextureWidth, DTid.y / nTextureHeight, 0);
	LowResImage[uint3(DTid.xy, 0)] = LeftMip[uint2(DTid.xy)];

	uv.z = 1;
	LowResImage[uint3(DTid.xy, 1)] = RightMip[uint2(DTid.xy)];
}
