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

Texture2DArray<float> depth : register(t12);
Texture2DArray<float4> normals : register(t13);

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
}

void ScreenSpaceReflection(float4 normalData, int2 screenPos, float sceneDepth)
{
	float3 normal = normalData.xyz;

    // Unproject into the world position using depth
	float4 unprojected = mul(g_dynamic.cameraToWorld, float4(screenPos, sceneDepth, 1));
	float3 world = unprojected.xyz / unprojected.w;

	float3 primaryRayDirection = normalize(world - g_dynamic.worldCameraPosition);

    // R
	float3 direction = normalize(primaryRayDirection - 2 * dot(primaryRayDirection, normal) * normal);
	float3 origin = world - primaryRayDirection * 0.1f; // Lift off the surface a bit

	float numBounces = 1;
	float reflectivity = normalData.w;
	
	FireRay(origin, direction, numBounces, reflectivity);
}

void FullTrace(int2 pixel)
{
	float3 origin, direction;
	GenerateCameraRay(pixel, origin, direction);
	
	float numBounces = 0;
	float reflectivity = 1;
	
	FireRay(origin, direction, numBounces, reflectivity);
}

[shader("raygeneration")]
void RayGen()
{
	int3 pixel = int3(DispatchRaysIndex().xy, g_dynamic.curCam);
	float2 xy = pixel.xy + 0.5;

	float4 normalData = normals.Load(int4(pixel.xy, 0, 0));
	if (normalData.w == 0.0)
	{
		if (g_screenOutput[int3(xy, 1)].a == 0)
		{
			FullTrace(pixel.xy);
			g_screenOutput[pixel] = float4(1, 0, 0, 1);

		}
		else
		{
			g_screenOutput[pixel] = float4(0, 1, 0, 1);
		}
		return;
	}
	
    // Screen position for the ray
	float2 screenPos = xy / g_dynamic.resolution * 2.0 - 1.0;

    // Invert Y for DirectX-style coordinates
	screenPos.y = -screenPos.y;

    // Read depth and normal
	float sceneDepth = depth.Load(int4(pixel, 0));
    
	ScreenSpaceReflection(normalData, screenPos, sceneDepth);
	//g_screenOutput[pixel] = float4(pixel / float2(DispatchRaysDimensions().xy), 0, 1);
	g_screenOutput[pixel] = float4(1, 1, 0, 1);
}
