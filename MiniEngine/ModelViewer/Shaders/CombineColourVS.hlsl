
//cbuffer quad : register(b0)
//{
//	float4 topLeft;
//	float4 topRight;
//	float4 bottomLeft;
//	float4 bottomRight;
//}

void main(
	in uint VertID : SV_VertexID,
	out float4 Pos : SV_Position,
	out float3 Tex : TexCoord0)
{
	if (VertID == 0) {
		Tex = float3(0, 0, 0);
		Pos = float4(-1, -1, 0, 0.1f);
	}
	else if (VertID == 1 || VertID == 3) {
		Tex = float3(0, 1, 0);
		Pos = float4(-1, 1, 0, 0.1f);
	}
	else if (VertID == 2 || VertID == 5) {
		Tex = float3(1, 0, 0);
		Pos = float4(-(1.0f / 3.0f), -1, 0, 0.1f);
	}
	else if (VertID == 4) {
		Tex = float3(1, 1, 0);
		Pos = float4(-(1.0f / 3.0f), 1, 0, 0.1f);
	}

	/*if (VertID == 0) {
		Tex = float3(0, 0, 2);
		Pos = topLeft;
	}
	else if (VertID == 1 || VertID == 3) {
		Tex = float3(0, 1, 2);
		Pos = bottomLeft;
	}
	else if (VertID == 2 || VertID == 5) {
		Tex = float3(1, 0, 2);
		Pos = topRight;
	}
	else if (VertID == 4) {
		Tex = float3(1, 1, 2);
		Pos = bottomRight;
	}*/
}