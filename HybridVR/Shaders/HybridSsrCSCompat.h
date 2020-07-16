#pragma once

#ifndef HLSL
#include "../HlslCompat.h"
#endif

struct HybridSsrConstantBuffer
{
	float4 RenderTargetSize;

	float4x4 View;
	float4x4 Projection;
	float4x4 InvProjection;

	float NearPlaneZ;
	float FarPlaneZ;

	// (Width, Height, 1 / Width, 1 / Height)

	// Unclear
	float SSRScale;

	/**
	 * Based on how far away from the camera the depth is,
	 * adding a bit of extra thickness can help improve some
	 * artifacts. Driving this value up too high can cause
	 * artifacts of its own.
	 */
	float ZThickness;

	// Step in horizontal or vertical pixels between samples.
	//  Float because integer math is slow on GPUs.
	float Stride;

	// Max number of iterations. More = Prettier but more expensive 
	float MaxSteps;

	// Maximum distance to trace before returning a miss
	float MaxDistance;

	/*
	 * More distant pixels are smaller in screen space. This
	 * value tells at what point to start relaxing the stride
	 * to give higher quality reflections for objects far from
     * the camera.
	 */
	float StrideZCutoff;
};
