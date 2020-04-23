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

#include "ShaderUtility.hlsli"
#include "PresentRS.hlsli"

Texture2DArray<float3> ColorTex : register(t0);
Texture2DArray<float3> ColorTex2 : register(t1);

[RootSignature(Present_RootSig)]
float3 main(float4 position : SV_Position, float3 uvw : TexCoord0) : SV_Target0
{
	float nTextureWidth;
	float nTextureHeight;
	float elements;
	ColorTex.GetDimensions(nTextureWidth, nTextureHeight, elements);
	uvw.y = 1 - uvw.y;
	int3 index = uvw * int3(nTextureWidth, nTextureHeight, 1);

	/*return ColorTex[index];*/
	if (index.z == 0)
	{
		float3 LinearRGB = RemoveDisplayProfile(ColorTex[index], LDR_COLOR_FORMAT);
		return ApplyDisplayProfile(LinearRGB, DISPLAY_PLANE_FORMAT);
	}
	else
	{
		float3 LinearRGB = RemoveDisplayProfile(ColorTex2[index], LDR_COLOR_FORMAT);
		return ApplyDisplayProfile(LinearRGB, DISPLAY_PLANE_FORMAT);
	}
}
