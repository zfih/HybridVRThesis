
Texture2D<float> LeftDepth : register(t0);
Texture2D<float> RightDepth : register(t1);
Texture2D<uint2> Stencil : register(t2);
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

	if (Stencil[DTid.xy].g == 0)
	{
		CombinedDepth[DTid.xy] = 1.0f;
		return;
	}
	CombinedDepth[DTid.xy] = Min(leftDepth, rightDepth);
}