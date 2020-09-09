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

	// Read depth and normal
	float depth = depths[pixel];

	float4 normalSpecular = normals[pixel];
	float3 normal = normalSpecular.xyz;
	float specular = normalSpecular.w;

	float reflectivity;
	RayDesc rayDesc;
	rayDesc.TMin = 0;
	rayDesc.TMax = FLT_MAX;

	GenerateSSRRay(
		pixel.xy, depth, normal, specular,
		rayDesc.Origin, rayDesc.Direction, reflectivity);

	if (reflectivity == 0.0)
		return;

	RayPayload payload;
	payload.SkipShading = false;
	payload.RayHitT = FLT_MAX;
	payload.Bounces = 1;
	payload.Reflectivity = reflectivity;
	TraceRay(g_accel, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, rayDesc, payload);
}
