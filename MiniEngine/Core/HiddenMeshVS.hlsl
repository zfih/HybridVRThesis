float4 main( float2 pos : POSITION ) : SV_POSITION
{
	return float4((pos.xy * 2) - 1, 1, 1);
}