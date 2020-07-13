
Texture2DArray<float3> HighResImage : register(t0);
RWTexture2DArray<float3> LowPassedImage : register(u0);

float3 ApplySRGBCurve(float3 x)
{
	// Approximately pow(x, 1.0 / 2.2)
	return x < 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

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

	float3 colourSum = float3(0, 0, 0);

	for (int y = -2; y < 3; y++)
	{
		for (int x = -2; x < 3; x++)
		{
			float weight = weights[x + 2][y + 2];
			colourSum += weight * 
						 HighResImage[float3(DTid.x + x, DTid.y + y, cam)];
		}
	}

	LowPassedImage[uint3(DTid.x, DTid.y, cam)] = colourSum;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	filter(DTid, 0);
	filter(DTid, 1);
}
