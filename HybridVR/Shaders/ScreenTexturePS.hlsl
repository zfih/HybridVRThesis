SamplerState sampler0 : register(s0);
Texture2D<float4> texture0 : register(t0);

struct VSOutput
{
    sample float4 position : SV_Position;
    sample float2 uv : TexCoord0;
};

float4 main(VSOutput vsOutput) : SV_TARGET
{
    //return float4(1,0,0,1);
    return texture0.Sample(sampler0, vsOutput.uv);
}