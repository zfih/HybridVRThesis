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

Texture2DArray<float>    depth    : register(t12);
Texture2DArray<float4>   normals  : register(t13);
RWTexture2D<float> reflectionDistance : register(u3);

[shader("raygeneration")]
void RayGen()
{
    uint2 DTid = DispatchRaysIndex().xy;
    float2 xy = DTid.xy + 0.5;

    // Screen position for the ray
    float2 screenPos = xy / g_dynamic.resolution * 2.0 - 1.0;

    // Invert Y for DirectX-style coordinates
    screenPos.y =  -screenPos.y;

    // Read depth and normal
    float sceneDepth = depth[int3(xy, g_dynamic.curCam)];
	float4 normalData = normals[int3(xy, g_dynamic.curCam)];
	if (normalData.w == 0.0)
		return;
    
    float3 normal = normalData.xyz;

    // Unproject into the world position using depth
    float4 unprojected = mul(g_dynamic.cameraToWorld, float4(screenPos, sceneDepth, 1));
    float3 world = unprojected.xyz / unprojected.w;

	float3 primaryRay = world - g_dynamic.worldCameraPosition;
    reflectionDistance[DTid] = length(primaryRay);
	float3 primaryRayDirection = normalize(primaryRay);

    // R
	float3 direction = reflect(primaryRayDirection, normal);
    float3 origin = world - primaryRayDirection * 0.1f;     // Lift off the surface a bit

    RayDesc rayDesc = { origin,
        0.0f,
        direction,
        FLT_MAX };

    RayPayload payload;
    payload.SkipShading = false;
    payload.RayHitT = FLT_MAX;
    payload.Bounces = 1;
	payload.Reflectivity = normalData.w;
    TraceRay(g_accel, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0,0,1,0, rayDesc, payload);
    
	//g_screenOutput[int3(DispatchRaysIndex().xy, g_dynamic.curCam)] = float4(normalData.w, 0, 0, 1);
}