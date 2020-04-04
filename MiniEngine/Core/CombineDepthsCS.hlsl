
RWTexture2D<float> LeftDepth : register(u0);
RWTexture2D<float> RightDepth : register(u1);

float Max(float f1, float f2)
{
	return f1 > f2 ? f1 : f2;
}

[numthreads(1, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	float nTextureWidth;
	float nTextureHeight;
	LeftDepth.GetDimensions(nTextureWidth, nTextureHeight);
	for (int h = 0; h < nTextureHeight; h++) // TODO: should it be h <= nTextureHeight?
	{
		for (int w = 0; w < nTextureWidth; w++) // TODO: should it be w <= nTextureWidth?
		{
			int2 index = int2(w, h);
			LeftDepth[index] = Max(LeftDepth[index], RightDepth[index]);
		}
	}
}