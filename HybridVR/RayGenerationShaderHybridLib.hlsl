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


Texture2DArray<float> g_depth : register(t12);
Texture2DArray<float4> g_normals : register(t13);

void ScreenSpaceReflection(float4 normalSpecular, int3 pixel)
{
	float depth = g_depth[pixel];

	float3 origin;
	float3 direction;
	float reflectivity;

	GenerateSSRRay(
		pixel.xy, depth, normalSpecular.xyz, normalSpecular.w,
		origin, direction, reflectivity);

	float numBounces = 1;

	FireRay(origin, direction, numBounces, reflectivity);
}

void FullTrace(int3 pixel)
{
	float3 origin, direction;
	GenerateCameraRay(pixel.xy, origin, direction);

	float numBounces = 0;
	float reflectivity = 1;

	FireRay(origin, direction, numBounces, reflectivity);
}

[shader("raygeneration")]
void RayGen()
{
	int3 pixel = int3(DispatchRaysIndex().xy, g_dynamic.curCam);
	float4 normal = g_normals[pixel];
	float4 color = g_screenOutput[pixel];
	
	// Pixel Good - Green
	if(normal.w == 0.0 && color.a != 0)
	{
	}
	else if(normal.w != 0.0 && color.a != 0)// Need refl
	{
		ScreenSpaceReflection(normal, pixel);
	}
	else if (color.a == 0) // Needs full
	{
		FullTrace(pixel);
	}
	else // Should never happen
	{
		g_screenOutput[pixel] = float4(1, 0, 1, 1);
	}
}
