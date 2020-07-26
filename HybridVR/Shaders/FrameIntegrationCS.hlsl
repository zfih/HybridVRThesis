cbuffer consts : register(b0)
{
    uint cam;
};

Texture2DArray<float3> LowResImage : register(t0);
RWTexture2DArray<float3> HighResImage : register(u0);
RWTexture2DArray<float3> Residuals : register(u1);
Texture2DArray<float3> LowPassedImage : register(t1);

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
    float3 uv = float3(DTid.x / nTextureWidth, DTid.y / nTextureHeight, cam);

    float3 colour = (2 * RemoveSRGBCurve(HighResImage[uint3(DTid.xy, cam)]))
		- RemoveSRGBCurve(LowResImage.SampleLevel(Sampler, uv, 2)); // Mip level 2
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
    float3 uv = float3(DTid.x / nTextureWidth, DTid.y / nTextureHeight, cam);

    float3 sampledColour =
		RemoveSRGBCurve(LowResImage.SampleLevel(Sampler, uv, 2)); // Mip level 2
    float3 contrast = abs(LowPassedImage[uint3(DTid.xy, cam)] - sampledColour)
		/ (LowPassedImage[uint3(DTid.xy, cam)] + sampledColour);
    float3 s = float3(5, 5, 5); // TODO: Test if this is scene or HMD dependent
    float3 weight = exp(-s * contrast);

    HighResImage[uint3(DTid.xy, cam)] =
		ApplySRGBCurve(sampledColour + weight * Residuals[uint3(DTid.xy, cam)]);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    FullResPass(DTid, cam);
    LowResPass(DTid, !cam);
}