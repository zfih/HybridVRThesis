#pragma once

#ifndef HLSL
#include "../HlslCompat.h"
#endif

struct HybridSsrConstantBuffer
{
	//// Dynamic
	
	float4x4 ViewProjection;
	float4x4 InvViewProjection;
	float4x4 Projection;
	float4x4 InvProjection;
	float4x4 View;
	float4x4 InvView;
	float3 CameraPos;

	//// Constants

	float NearPlaneZ;
	float FarPlaneZ;
	
	// (Width, Height, 1 / Width, 1 / Height)
	float4 RenderTargetSize;

	// Unclear
	float SSRScale;
	
	// Thickness to ascribe to each pixel in the depth buffer
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
