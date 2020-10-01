//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#define HLSL
#include "ModelViewerRaytracing.h"

Texture2DArray<float> depths : register(t12);
Texture2DArray<float4> normals : register(t13);

[shader("raygeneration")]
void RayGen()
{
	uint3 pixel = float3(DispatchRaysIndex().xy, g_dynamic.curCam);
	
	float depth = depths[pixel];

	float4 normalXY_Ratio_Specular = normals[pixel];
	float2 normalXY = normalXY_Ratio_Specular.xy;
	float ratio = normalXY_Ratio_Specular.z;
	float specular = normalXY_Ratio_Specular.w;

	if(ratio == 0)
	{
		return;
	}

	float3 origin;
	float3 direction;
	float reflectivity;

	float3 normal = GenerateSSRRay(
		pixel.xy,
		depth,
		normalXY,
		specular,
		origin,
		direction,
		reflectivity);

	if (reflectivity == 0.0)
	{
		return;
	}
	RENDER_AND_RETURN(normal.xyzz);
	const int numBounces = 1;

	FireRay(origin, direction, numBounces, reflectivity);
}
