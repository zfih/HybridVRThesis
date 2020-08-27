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

void ScreenSpaceReflection(float4 normal, int2 xy, int3 pixel)
{
	// Screen position for the ray
	float2 screenPos = xy / g_dynamic.resolution * 2.0 - 1.0;

	// Invert Y for DirectX-style coordinates
	screenPos.y = -screenPos.y;

	// Read depth and normal
	float depth = g_depth[pixel];

	// Unproject into the world position using depth
	float4 unprojected = mul(g_dynamic.cameraToWorld, float4(screenPos, depth, 1));
	float3 world = unprojected.xyz / unprojected.w;

	float3 primaryRayDirection = normalize(world - g_dynamic.worldCameraPosition);

	float3 direction = reflect(primaryRayDirection, normal.xyz);
	float3 origin = world - primaryRayDirection * 0.1f; // Lift off the surface a bit

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

	float2 xy = pixel.xy + 0.5; // center in the middle of the pixel

	float4 normal = g_normals[int3(xy, g_dynamic.curCam)];

	float4 color = g_screenOutput[int3(xy, g_dynamic.curCam)];
	
	// Pixel Good - Green
	if(normal.w == 0.0 && color.a != 0)
	{
		//g_screenOutput[pixel] = float4(0, 1, 0, 1);
	}
	else if(normal.w != 0.0 && color.a != 0)// Need refl - Yellow
	{
		ScreenSpaceReflection(normal, xy, int3(xy, g_dynamic.curCam));
		
	}
	else if (color.a == 0) // Needs full - Red
	{
		FullTrace(int3(xy, g_dynamic.curCam));
		//g_screenOutput[pixel] = float4(1, 0, 0, 1);
	}
	else // Should never happen
	{
		g_screenOutput[int3(xy, g_dynamic.curCam)] = float4(1, 0, 1, 1);
	}
}
