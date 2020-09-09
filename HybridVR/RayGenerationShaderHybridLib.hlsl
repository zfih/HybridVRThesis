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

void ScreenSpaceReflection(float3 normal, float specular, int3 pixel)
{
	float depth = g_depth[pixel];

	float3 origin;
	float3 direction;
	float reflectivity;

	GenerateSSRRay(
		pixel.xy, depth, normal, specular,
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

inline bool PixelCanBeReused(float specular, float alpha)
{
	return specular == 0.0 && alpha != 0;
}

inline bool PixelNeedsReflections(float specular, float alpha)
{
	return specular != 0 && alpha != 0;
}

inline bool PixelNeedsFullRaytrace(float alpha)
{
	return alpha == 0;
}

[shader("raygeneration")]
void RayGen()
{
	int3 pixel = int3(DispatchRaysIndex().xy, g_dynamic.curCam);

	float4 normalSpecular = g_normals[pixel];
	float specular = normalSpecular.w;
	float3 normal = normalSpecular.xyz;

	float4 color = g_screenOutput[pixel];
	
	if(PixelCanBeReused(specular, color.a))
	{
		// Do nothing
	}
	else if(PixelNeedsReflections(specular, color.a))
	{
		ScreenSpaceReflection(normal, specular, pixel);
	}
	else if (PixelNeedsFullRaytrace(color.a))
	{
		FullTrace(pixel);
	}
}
