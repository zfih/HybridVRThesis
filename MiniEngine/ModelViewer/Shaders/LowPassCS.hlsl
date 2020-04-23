
Texture2DArray<float3> HighResImage : register(t0);
RWTexture2DArray<float3> LowPassedImage : register(u0);

float3 ApplySRGBCurve(float3 x)
{
	// Approximately pow(x, 1.0 / 2.2)
	return x < 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

void filter(uint3 DTid, int cam)
{
	float weight = 1.0f / 9.0f;

	uint3 a = uint3(DTid.x - 1, DTid.y - 1, cam); 
	uint3 b = uint3(DTid.x,     DTid.y - 1, cam); 
	uint3 c = uint3(DTid.x + 1, DTid.y - 1, cam); 
	
	uint3 d = uint3(DTid.x - 1, DTid.y, cam); 
	uint3 e = uint3(DTid.x,     DTid.y, cam); 
	uint3 f = uint3(DTid.x + 1, DTid.y, cam); 
	
	uint3 g = uint3(DTid.x - 1, DTid.y + 1, cam); 
	uint3 h = uint3(DTid.x,     DTid.y + 1, cam); 
	uint3 i = uint3(DTid.x + 1, DTid.y + 1, cam); 

	/*LowPassedImage[uint3(DTid.x, DTid.y, cam)] = 
		weight * HighResImage[a] + weight * HighResImage[b] + weight * HighResImage[c] +
		weight * HighResImage[d] + weight * HighResImage[e] + weight * HighResImage[f] +
		weight * HighResImage[g] + weight * HighResImage[h] + weight * HighResImage[i];*/

	//LowPassedImage[uint3(DTid.x, DTid.y, cam)] = ApplySRGBCurve(HighResImage[uint3(DTid.x, DTid.y, cam)]);
	LowPassedImage[uint3(DTid.x, DTid.y, cam)] = HighResImage[uint3(DTid.x, DTid.y, cam)];
	//LowPassedImage[uint3(DTid.x, DTid.y, cam)] = float3(1,0,0.5);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	filter(DTid, 0);
	filter(DTid, 1);
}
