//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard
//
// A vertex shader for full-screen effects without a vertex buffer.  The
// intent is to output an over-sized triangle that encompasses the entire
// screen.  By doing so, we avoid rasterization inefficiency that could
// result from drawing two triangles with a shared edge.
//
// Use null input layout
// Draw(3)

#include "PresentRS.hlsli"

cbuffer qL : register(b1)
{
	float4 leftTopLeft;
	float4 leftTopRight;
	float4 leftBottomLeft;
	float4 leftBottomRight;
}

cbuffer qR : register(b2)
{
	float4 rightTopLeft;
	float4 rightTopRight;
	float4 rightBottomLeft;
	float4 rightBottomRight;
}

[RootSignature(Present_RootSig)]
void main(
    in uint VertID : SV_VertexID,
    out float4 Pos : SV_Position,
    out float3 Tex : TexCoord0
)
{
    // Texture coordinates range [0, 2], but only [0, 1] appears on screen.
    //Tex = float2(uint2(VertID, VertID << 1) & 2);
    //Pos = float4(lerp(float2(-0.5, 0.5), float2(0.5, -0.5), Tex), 0, 1);
	if (VertID == 0) {
		Tex = float3(0, 0, 0);
		Pos = float4(-1, -1, 0, 1);
	}
	else if (VertID == 1 || VertID == 3) {
		Tex = float3(0, 1, 0);
		Pos = float4(-1, 1, 0, 1);
	}
	else if (VertID == 2 || VertID == 5) {
		Tex = float3(1, 0, 0);
		Pos = float4(0, -1, 0, 1);
	}
	else if (VertID == 4) {
		Tex = float3(1, 1, 0);
		Pos = float4(0, 1, 0, 1);
	}

	else if (VertID == 6) {
		Tex = float3(0, 0, 1);
		Pos = float4(0, -1, 0, 1);
	}
	else if (VertID == 7 || VertID == 9) {
		Tex = float3(0, 1, 1);
		Pos = float4(0, 1, 0, 1);
	}
	else if (VertID == 8 || VertID == 11) {
		Tex = float3(1, 0, 1);
		Pos = float4(1, -1, 0, 1);
	}
	else if (VertID == 10) {
		Tex = float3(1, 1, 1);
		Pos = float4(1, 1, 0, 1);
	}

	//else if (VertID == 12) {
	//	Tex = float3(0, 0, 2);
	//	Pos = rightTopLeft;
	//	//Pos = float4(-1.02758825, -0.999671221, 1.16415322e-10, 1.00000000);
	//	//Pos = float4(-1, -1, 0, 1);
	//}
	//else if (VertID == 13 || VertID == 15) {
	//	Tex = float3(0, 1, 2);
	//	Pos = rightBottomLeft;
	//	//Pos = float4(-1.02758825, 1.00646734, 1.16415322e-10, 1.00000000);
	//	//Pos = float4(-1, 1, 0, 1);
	//}
	//else if (VertID == 14 || VertID == 17) {
	//	Tex = float3(1, 0, 2);
	//	Pos = rightTopRight;
	//	//Pos = float4(0.992596447, -0.999671221, 1.16415322e-10, 1.00000000);
	//	//Pos = float4(0, -1, 0, 1);
	//}
	//else if (VertID == 16) {
	//	Tex = float3(1, 1, 2);
	//	Pos = rightBottomRight;
	//	//Pos = float4(0.992596447, 1.00646734, 1.16415322e-10, 1.00000000);
	//	//Pos = float4(0, 1, 0, 1);
	//}
}
