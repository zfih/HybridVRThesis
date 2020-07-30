
Texture2D<float> g_SSAOFullScreen : register(t0);
RWTexture2D<float4> g_SceneColorBuffer : register(u0);

float3 RemoveSRGBCurve(float3 x)
{
    // Approximately pow(x, 2.2)
	return x < 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}

float3 ApplySRGBCurve(float3 x)
{
    // Approximately pow(x, 1.0 / 2.2)
	return x < 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	float alpha = g_SceneColorBuffer[DTid.xy].w;
	float3 colorNoSRGB = RemoveSRGBCurve(g_SceneColorBuffer[DTid.xy].xyz);
	float3 colorAO = colorNoSRGB * g_SSAOFullScreen[DTid.xy];
	
	g_SceneColorBuffer[DTid.xy] = float4(ApplySRGBCurve(colorAO), alpha);
	//g_SceneColorBuffer[DTid.xy] = 
	//	g_SceneColorBuffer[DTid.xy] * g_SSAOFullScreen[DTid.xy];
}
