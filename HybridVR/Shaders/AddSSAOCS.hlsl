
Texture2D<float> g_SSAOFullScreen : register(t0);
RWTexture2D<float4> g_SceneColorBuffer : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	g_SceneColorBuffer[DTid.xy] = 
		g_SceneColorBuffer[DTid.xy] * g_SSAOFullScreen[DTid.xy];
}
