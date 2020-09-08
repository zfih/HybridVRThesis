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

void FireRay(float3 origin, float3 direction, float bounces, float reflectivity)
{
	RayDesc rayDesc =
	{
		origin,
		0.0f,
		direction,
		FLT_MAX
	};

	RayPayload payload;
	payload.SkipShading = false;
	payload.RayHitT = FLT_MAX;
	payload.Bounces = bounces;
	payload.Reflectivity = reflectivity;
	TraceRay(g_accel, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, rayDesc, payload);
	
    //g_screenOutput[int3(DispatchRaysIndex().xy, g_dynamic.curCam)] = float4(reflectivity, 0, 0, 1);
}

void ScreenSpaceReflection(float4 normal, int3 pixel)
{
	float depth = g_depth[pixel];

	float3 pixelInWorld = UnprojectPixel(pixel.xy, depth);

	float3 primaryRayDirection = normalize(pixelInWorld - g_dynamic.worldCameraPosition);

	float3 direction = reflect(primaryRayDirection, normal.xyz);

	// Step back along ray to get out of surface
	float3 origin = pixelInWorld - primaryRayDirection * 0.001f;
	float numBounces = 1;
	float reflectivity = normal.w;

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
