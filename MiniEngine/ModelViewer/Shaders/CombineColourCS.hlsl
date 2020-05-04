
// TODO: Delete this file

cbuffer qL : register(b0)
{
	float4 leftTopLeft;
	float4 leftTopRight;
	float4 leftBottomLeft;
	float4 leftBottomRight;
}

cbuffer qR : register(b1)
{
	float4 rightTopLeft;
	float4 rightTopRight;
	float4 rightBottomLeft;
	float4 rightBottomRight;
}
RWTexture2DArray<float4> ColourTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	if (ColourTex[int3(DTid.xy, 0)].x == 0.0f && 
		ColourTex[int3(DTid.xy, 0)].y == 0.0f && 
		ColourTex[int3(DTid.xy, 0)].z == 0.0f)
	{
		ColourTex[int3(DTid.xy, 0)] = ColourTex[int3(DTid.xy, 2)];
	}

	if (ColourTex[int3(DTid.xy, 1)].x == 0.0f &&
		ColourTex[int3(DTid.xy, 1)].y == 0.0f &&
		ColourTex[int3(DTid.xy, 1)].z == 0.0f)
	{
		ColourTex[int3(DTid.xy, 1)] = ColourTex[int3(DTid.xy, 2)];
	}
}