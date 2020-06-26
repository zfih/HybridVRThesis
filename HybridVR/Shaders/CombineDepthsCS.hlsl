
Texture2D<float> LeftDepth : register(t0);
Texture2D<float> RightDepth : register(t1);
Texture2DArray<uint2> Stencil : register(t2);
RWTexture2D<float> CombinedDepth : register(u0);

float Min(float f1, float f2)
{
	return f1 < f2 ? f1 : f2;
}

float Max(float f1, float f2)
{
	return f1 > f2 ? f1 : f2;
}

[numthreads(8, 8, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	float leftDepth = LeftDepth[DTid.xy];
	float rightDepth = RightDepth[DTid.xy];

	CombinedDepth[DTid.xy] = Min(leftDepth, rightDepth);
	
	if ((Stencil[int3(DTid.xy, 0)].g == 0x00 || Stencil[int3(DTid.xy, 0)].g == 0x01) &&
		(Stencil[int3(DTid.xy, 1)].g == 0x00 || Stencil[int3(DTid.xy, 1)].g == 0x01))
	{
		CombinedDepth[DTid.xy] = 1.0f;
	}
}