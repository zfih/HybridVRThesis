#pragma once

#ifndef HLSL
#include "../HlslCompat.h"
#endif

struct HybridSsrConstantBuffer
{
	float4x4 ViewProjection;
	float4x4 InvViewProjection;
	float4x4 Projection;
	float4x4 InvProjection;
	float4x4 View;
	float4x4 InvView;
	float4 RTSize;
	float3 CameraPos;
	float SSRScale;
	float4 LightDirection;

	float ZThickness;
	float NearPlaneZ;
	float FarPlaneZ;
	float Stride;
	float MaxSteps;
	float MaxDistance;
	float StrideZCutoff;
	float ReflectionsMode;
};