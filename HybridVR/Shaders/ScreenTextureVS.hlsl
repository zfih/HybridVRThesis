struct VSOutput
{
    float4 position : SV_Position0;
    float2 uv : TexCoord0;
};

VSOutput main(float4 pos : POSITION, float2 uv : TEXCOORD)
{
    VSOutput result;

    result.position = pos;
    result.uv = uv;

	return result;
}