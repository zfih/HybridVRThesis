
Texture2DArray<float3> HighResImage : register(t0);
RWTexture2D<float3> LeftMip : register(u0);
RWTexture2D<float3> RightMip : register(u1);

void filter(uint3 DTid, int cam)
{
	float weights[5][5] =
	{
		{ 1.0278445 / 273.0f, 4.10018648 / 273.0f, 6.49510362 / 273.0f,  
			4.10018648 / 273.0f, 1.0278445 / 273.0f },
		{ 4.10018648 / 273.0f, 16.35610171 / 273.0f, 25.90969361 / 273.0f,
			16.35610171 / 273.0f, 4.10018648 / 273.0f},
		{ 6.49510362 / 273.0f, 25.90969361 / 273.0f, 41.0435344 / 273.0f, 
			25.90969361 / 273.0f, 6.49510362 / 273.0f},
		{ 4.10018648 / 273.0f, 16.35610171 / 273.0f, 25.90969361 / 273.0f, 
			16.35610171 / 273.0f, 4.10018648 / 273.0f},
		{ 1.0278445 / 273.0f, 4.10018648 / 273.0f, 6.49510362 / 273.0f, 
			4.10018648 / 273.0f, 1.0278445 / 273.0f }
	};

	float3 colorSum = float3(0, 0, 0);

	for (int y = -2; y < 3; y++)
	{
		for (int x = -2; x < 3; x++)
		{
			float weight = weights[x + 2][y + 2];
			colorSum += weight * 
						 HighResImage[float3(DTid.x + x, DTid.y + y, cam)];
		}
	}

	if (cam == 0)
	{
		LeftMip[uint2(DTid.xy)] = colorSum;
	}
	else
	{
		RightMip[uint2(DTid.xy)] = colorSum;
	}
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	filter(DTid, 0);
	filter(DTid, 1);
}
