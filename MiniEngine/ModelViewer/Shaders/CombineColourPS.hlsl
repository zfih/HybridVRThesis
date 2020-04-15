
//Texture2DArray<float4> ColourTex : register(t0);

float3 main(float4 position : SV_Position, float3 uvw : TexCoord0) : SV_Target0
{
	/*float nTextureWidth;
	float nTextureHeight;
	float elements;
	ColourTex.GetDimensions(nTextureWidth, nTextureHeight, elements);
	uvw.y = 1 - uvw.y;
	int3 index = uvw * int3(nTextureWidth, nTextureHeight, 1);*/
		
	return float4(0, 1, 0, 0); //ColourTex[index];
}