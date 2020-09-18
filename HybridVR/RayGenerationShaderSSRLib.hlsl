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

	float4 normalSpecular = normals[pixel];
	float3 normal = normalSpecular.xyz;
	float specular = normalSpecular.w;


	float3 origin;
	float3 direction;
	float reflectivity;
	GenerateSSRRay(
		pixel.xy, depth, normal, specular,
		origin, direction, reflectivity);
	
	if (reflectivity == 0.0)
		return;

	const int numBounces = 1;
	FireRay(origin, direction, numBounces, reflectivity);
	
}
