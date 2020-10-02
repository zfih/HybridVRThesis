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
Texture2DArray<float> g_ratios : register(t14);

void ScreenSpaceReflection(int3 pixel, float3 normal, float specular, float ratio)
{
	float depth = g_depth[pixel];

	float3 origin;
	float3 direction;
	float reflectivity;

	GenerateSSRRay(
		pixel.xy, depth, normal, specular,
		origin, direction, reflectivity);

	float numBounces = 1;
	FireRay(origin, direction, numBounces, reflectivity * ratio);
}

void FullTrace(int3 pixel)
{
	float3 origin, direction;
	GenerateCameraRay(pixel.xy, origin, direction);

	float numBounces = 0;
	float reflectivity = 1;

	FireRay(origin, direction, numBounces, reflectivity);
}

inline bool PixelCanBeReused(float ratio, float specular)
{
	return ratio == 0.0 && specular != 0;
}

inline bool PixelNeedsReflections(float ratio, float specular)
{
	return ratio != 0 && specular != 0;
}

inline bool PixelNeedsFullRaytrace(float specular)
{
	return specular == 0;
}

[shader("raygeneration")]
void RayGen()
{
	int3 pixel = int3(DispatchRaysIndex().xy, g_dynamic.curCam);

	float4 normalXYZ_ratio = g_normals[pixel];
	float3 normal = normalXYZ_ratio.xyz;
	float ratio = normalXYZ_ratio.w;
	
	float4 color_specular = g_screenOutput[pixel];
	float specular = color_specular.w;

	if(PixelCanBeReused(ratio, specular))
	{
		// Do nothing
	}
	else if(PixelNeedsReflections(ratio, specular))
	{
		ScreenSpaceReflection(pixel, normal, specular, ratio);
	}
	else if (PixelNeedsFullRaytrace(specular))
	{
		FullTrace(pixel);
	}
}
