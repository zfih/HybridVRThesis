
cbuffer consts : register(b0)
{
	uint FullRes;
};

Texture2DArray<float3> LowResImage : register(t0);
RWTexture2DArray<float3> HighResImage : register(u0);
RWTexture2DArray<float3> Residuals : register(u1);

SamplerState Sampler : register(s0);

void FullResPass(uint3 DTid, uint cam)
{
	float nTextureWidth;
	float nTextureHeight;
	float elements;
	HighResImage.GetDimensions(nTextureWidth, nTextureHeight, elements);
	float3 uv = float3(DTid.x / nTextureWidth, DTid.y / nTextureHeight, cam);

	float3 colour = (2 * HighResImage[uint3(DTid.xy, cam)]) - LowResImage.SampleLevel(Sampler, uv, 0);
	if (colour.x > 1.0f)
	{
		Residuals[uint3(DTid.xy, cam)] = float3(colour.x - 1.0f, Residuals[uint3(DTid.xy, cam)].y, Residuals[uint3(DTid.xy, cam)].z);
		colour.x = 1.0f;
	}

	if (colour.y > 1.0f)
	{
		Residuals[uint3(DTid.xy, cam)] = float3(Residuals[uint3(DTid.xy, cam)].x, colour.y - 1.0f, Residuals[uint3(DTid.xy, cam)].z);
		colour.y = 1.0f;
	}

	if (colour.z > 1.0f)
	{
		Residuals[uint3(DTid.xy, cam)] = float3(Residuals[uint3(DTid.xy, cam)].x, Residuals[uint3(DTid.xy, cam)].y, colour.z - 1.0f);
		colour.z = 1.0f;
	}

	HighResImage[uint3(DTid.xy, cam)] = colour;
}

void LowResPass(uint3 DTid, uint cam)
{
	float nTextureWidth;
	float nTextureHeight;
	float elements;
	HighResImage.GetDimensions(nTextureWidth, nTextureHeight, elements);
	float3 uv = float3(DTid.x / nTextureWidth, DTid.y / nTextureHeight, cam);

	HighResImage[uint3(DTid.xy, cam)] = LowResImage.SampleLevel(Sampler, uv, 0) + Residuals[uint3(DTid.xy, cam)];
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
