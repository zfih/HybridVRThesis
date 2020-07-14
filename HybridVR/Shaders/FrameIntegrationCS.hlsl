
cbuffer consts : register(b0)
{
	uint FullRes;
};

RWTexture2D<float3> LeftMip : register(u0);
RWTexture2D<float3> RightMip : register(u1);
RWTexture2DArray<float3> HighResImage : register(u2);
RWTexture2DArray<float3> Residuals : register(u3);

SamplerState Sampler : register(s0);

float3 ApplySRGBCurve(float3 x)
{
	// Approximately pow(x, 1.0 / 2.2)
	return x < 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

float3 RemoveSRGBCurve(float3 x)
{
	// Approximately pow(x, 2.2)
	return x < 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}

void FullResPass(uint3 DTid, uint cam)
{
	float nTextureWidth;
	float nTextureHeight;
	float elements;
	HighResImage.GetDimensions(nTextureWidth, nTextureHeight, elements);
	float2 uv = float2(DTid.x / nTextureWidth, DTid.y / nTextureHeight);

	float3 removedCurve;
	
	if (cam == 0) {
		removedCurve = RemoveSRGBCurve(LeftMip[uv]);
	}
	else {
		removedCurve = RemoveSRGBCurve(RightMip[uv]);
	}

	float3 colour = (2 * RemoveSRGBCurve(HighResImage[uint3(DTid.xy, cam)])) - removedCurve;
	if (colour.x > 1.0f)
	{
		Residuals[uint3(DTid.xy, cam)] = float3(
			colour.x - 1.0f, 
			Residuals[uint3(DTid.xy, cam)].y, 
			Residuals[uint3(DTid.xy, cam)].z);
		colour.x = 1.0f;
	}

	if (colour.y > 1.0f)
	{
		Residuals[uint3(DTid.xy, cam)] = float3(
			Residuals[uint3(DTid.xy, cam)].x, 
			colour.y - 1.0f, 
			Residuals[uint3(DTid.xy, cam)].z);
		colour.y = 1.0f;
	}

	if (colour.z > 1.0f)
	{
		Residuals[uint3(DTid.xy, cam)] = float3(
			Residuals[uint3(DTid.xy, cam)].x, 
			Residuals[uint3(DTid.xy, cam)].y, 
			colour.z - 1.0f);
		colour.z = 1.0f;
	}

	HighResImage[uint3(DTid.xy, cam)] = ApplySRGBCurve(colour);
}

void LowResPass(uint3 DTid, uint cam)
{
	float nTextureWidth;
	float nTextureHeight;
	float elements;
	HighResImage.GetDimensions(nTextureWidth, nTextureHeight, elements);
	float2 uv = float2(DTid.x / nTextureWidth, DTid.y / nTextureHeight);

	float3 sampledColour = 
		RemoveSRGBCurve(cam == 0 ? LeftMip[uv] : RightMip[uv]);
	float3 contrast = abs(HighResImage[uint3(DTid.xy, cam)] - sampledColour) 
		/ (HighResImage[uint3(DTid.xy, cam)] + sampledColour);
	float3 s = float3(5, 5, 5); // TODO: Test if this is scene or HMD dependent
	float3 weight = exp(-s * contrast);

	HighResImage[uint3(DTid.xy, cam)] =
		ApplySRGBCurve(sampledColour + weight * Residuals[uint3(DTid.xy, cam)]);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	if (FullRes)
	{
		FullResPass(DTid, 0);
		FullResPass(DTid, 1);
	}
	else
	{
		LowResPass(DTid, 0);
		LowResPass(DTid, 1);
	}
}
