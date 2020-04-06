
Texture2D<float> LeftDepth : register(t0);
Texture2D<float> RightDepth : register(t1);
RWTexture2D<float> CombinedDepth : register(u0);

float Max(float f1, float f2)
{
	return f1 > f2 ? f1 : f2;
}

[numthreads(8, 8, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	float nTextureWidth;
	float nTextureHeight;
	LeftDepth.GetDimensions(nTextureWidth, nTextureHeight);

	int2 index = DTid.xy * int2(nTextureWidth, nTextureHeight);
	CombinedDepth[index] = Max(LeftDepth[index], RightDepth[index]);
}