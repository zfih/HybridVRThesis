#pragma once

#ifndef HLSL
#include "../HlslCompat.h"
#endif


struct HybridSsrConstantBuffer
{
	// Dynamic
	float4x4 ViewProjection;
	float4x4 InvViewProjection;
	float4x4 Projection;
	float4x4 InvProjection;
	float4x4 View;
	float4x4 InvView;
	float3 CameraPos;
	float4 LightDirection;

	//// Constants

	float4 RTSize; // (Width, Height, 1 / Width, 1 / Height)
	float SSRScale; // Unclear
	float ZThickness; // Thickness to ascribe to each pixel in the depth buffer
	float NearPlaneZ;
	float FarPlaneZ;
	float Stride; // Step in horizontal or vertical pixels between samples. Float because integer math is slow on GPUs.
	float MaxSteps; // Max number of iterations. More = Prettier but more expensive 
	float MaxDistance; // Maximum distance to trace before returning a miss
	float StrideZCutoff; // More distant pixels are smaller in screen space. This value tells at what point to
	// start relaxing the stride to give higher quality reflections for objects far from
	// the camera.

	float ReflectionsMode;
};
