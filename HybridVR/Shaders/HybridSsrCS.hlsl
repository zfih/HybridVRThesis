// By Morgan McGuire and Michael Mara at Williams College 2014
// Released as open source under the BSD 2-Clause License
// http://opensource.org/licenses/BSD-2-Clause
//
// Copyright (c) 2014, Morgan McGuire and Michael Mara
// All rights reserved.
//
// From McGuire and Mara, Efficient GPU Screen-Space Ray Tracing,
// Journal of Computer Graphics Techniques, 2014
//
// This software is open source under the "BSD 2-clause license":
//
// Redistribution and use in source and binary forms, with or
// without modification, are permitted provided that the following
// conditions are met:
//
// 1. Redistributions of source code must retain the above
// copyright notice, this list of conditions and the following
// disclaimer.
//
// 2. Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following
// disclaimer in the documentation and/or other materials provided
// with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
// CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
// USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
// AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
// IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.
/**
* The ray tracing step of the SSLR implementation.
* Modified version of the work stated above.
*/

#define HLSL
#include "HybridSsrCSCompat.h"

//// Resources


/// CBVs
cbuffer cbSSLR : register(b0)
{
HybridSsrConstantBuffer cb;
};

/// SRVs

// Combined buffer
Texture2D<float4> mainBuffer: register(t0);

// Depth buffer texture
Texture2D<float> depthBuffer : register(t1);

// Normal + reflection buffer
Texture2D<float4> normalReflectiveBuffer: register(t2);

// Color
Texture2D<float4> albedoBuffer: register(t3);


/// UAVs

// Output
RWTexture2D<float4> outputRT : register(u0);


/**
 * Returns true if the ray hit something
 * 
 * vsOrig: Camera-space ray origin, must be within the view volume
 * vsDir: Unit camera-space ray direction
 * jitter: Number (0; 1) for how far to bump the ray in stride units
 *			to conceal banding artifacts
 * hitPixel: Pixel coordinates for first scene intersection
 * hitPoint: Camera-space location of the ray hit
 */
bool TraceScreenSpaceRay(
	float3 vsOrig,
	float3 vsDir,
	float jitter,
	out float2 hitPixel,
	out float3 hitPoint);

// Returns the squared distance between a and b, avoiding the sqrt operation.
float DistanceSquared(float2 a, float2 b);

// Returns true if the ray intersects the depth buffer
bool IntersectsDepthBuffer(float z, float minZ, float maxZ);

// Returns camera depth in linear depth
float LineariseDepth(float depth, float nearPlane, float farPlane);

// Swaps the values of a and b
void Swap(inout float a, inout float b);

// Calculate the Fresnel
float3 Fresnel(float3 F0, float3 L, float3 H);

// Returns true if a~b within an epsilon
bool eq(float a, float b)
{
	return abs(a - b) < 0.0005;
}

// Returns true if the mat is all zero
bool zero(float4x4 mat)
{
	for(int x = 0; x < 4; x++)
	{
		for(int y = 0; y < 4; y++)
		{
			if(mat[x][y] != 0) return true;
		}
	}
	return false;
}


#define THREADX 8
#define THREADY 8
#define THREADGROUPSIZE (THREADX*THREADY)

[numthreads(THREADX, THREADY, 1)]
void CSMain(uint3 Gid : SV_GroupID, uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID,
            uint GI : SV_GroupIndex)
{
	const uint2 screenPos = DTid.xy;

	// Sample textures
	float3 mainRT = mainBuffer[screenPos].xyz;
	float depth = depthBuffer[screenPos];
	float4 albedo = albedoBuffer[screenPos];
	
	float4 normalReflective = normalReflectiveBuffer[screenPos];
	float3 normal = normalReflective.xyz;
	float reflectiveness = normalReflective.w;

	// If this pixel has not been rendered to, let the skybox/clearvalue remain
	if(depth == 0 || reflectiveness == 0)
	{
		outputRT[screenPos] = float4(mainRT, 1);
		//outputRT[screenPos] = float4(1,0,0, 1);
		return;
	}

	// Get world position from depth
	float2 uv = (screenPos.xy + 0.5) * cb.RenderTargetSize.zw;
	float4 clipPos = float4(2 * uv - 1, depth, 1);
	clipPos.y = -clipPos.y;

	// Convert normal to view space to find the correct reflection vector
	float3 normalVS = mul(cb.View, normal);

	float4 viewPos = mul(cb.InvProjection, clipPos);
	viewPos.xyz /= viewPos.w;

	float3 rayOrigin = viewPos.xyz * 0.9;//+ normalVS * 0.01;
	float3 toPosition = normalize(rayOrigin.xyz);

	/*
	* Since position is reconstructed in view space, just normalize it to get the
	* vector from the eye to the position and then reflect that around the normal to
	* get the ray direction to trace.
	*/
	float3 rayDirection = reflect(toPosition, normalVS);

	float jitter = cb.Stride > 1.0f ? float(int(screenPos.x + screenPos.y) & 1) * 0.5f : 0.0f;

	// out parameters
	float2 hitPixel = float2(0.0f, 0.0f);
	float3 hitPoint = float3(0.0f, 0.0f, 0.0f);

	// perform ray tracing - true if hit found, false otherwise
	bool intersection = TraceScreenSpaceRay(rayOrigin, rayDirection, jitter, hitPixel, hitPoint);

	// move hit pixel from pixel position to UVs;
	if(hitPixel.x > cb.RenderTargetSize.x || hitPixel.x < 0.0f ||
		hitPixel.y > cb.RenderTargetSize.y || hitPixel.y < 0.0f ||
		hitPoint.z > 0.999)
	{
		intersection = false;
	}

	float4 result = intersection ? mainBuffer[hitPixel] : 0;

	//calculate fresnel for the world point/pixel we are shading
	float3 L = rayDirection.xyz;
	float3 H = normalize(-toPosition + L);
	float3 specularColor = lerp(0.04f, albedo.rgb, reflectiveness);

	float3 F = Fresnel(specularColor, L, H);

	//outputRT[screenPos] = float4(LineariseDepth(depth, cb.NearPlaneZ, cb.FarPlaneZ),0,0,1); 
	//outputRT[screenPos] = float4(result.rgb * (F * cb.SSRScale) + mainRT.rgb, 1);
	outputRT[screenPos] = float4(result.rgb, 1);
}

bool TraceScreenSpaceRay(
	float3 vsOrig,
	float3 vsDir,
	float jitter,
	out float2 hitPixel,
	out float3 hitPoint)
{
	// Max distance
	float depthDistanceTraversed = vsOrig.z + vsDir.z * cb.MaxDistance;

	float rayLength = cb.MaxDistance;
	if(depthDistanceTraversed < cb.NearPlaneZ)
	{
		rayLength = (cb.NearPlaneZ - vsOrig.z) / vsDir.z;
	}

	float3 vsEndPoint = vsOrig + vsDir * rayLength;

	// Project into homogeneous clip space
	float4 H0 = mul(cb.Projection, float4(vsOrig, 1.0f));
	float4 H1 = mul(cb.Projection, float4(vsEndPoint, 1.0f));

	float k0 = 1.0f / H0.w;
	float k1 = 1.0f / H1.w;

	// The interpolated homogeneous version of the camera-space points
	float3 Q0 = vsOrig * k0;
	float3 Q1 = vsEndPoint * k1;

	// Screen-space endpoints
	float2 P0 = H0.xy * k0;
	float2 P1 = H1.xy * k1;

	P0 = P0 * float2(0.5, -0.5) + float2(0.5, 0.5);
	P1 = P1 * float2(0.5, -0.5) + float2(0.5, 0.5);

	P0.xy *= cb.RenderTargetSize.xy;
	P1.xy *= cb.RenderTargetSize.xy;

	// If the line is degenerate, make it cover at least one pixel
	// to avoid handling zero-pixel extent as a special case later
	P1 += (DistanceSquared(P0, P1) < 0.0001f) ? float2(0.01f, 0.01f) : 0.0f;
	float2 delta = P1 - P0;

	// Permute so that the primary iteration is in x to collapse
	// all quadrant-specific DDA cases later
	bool permute = false;
	if(abs(delta.x) < abs(delta.y))
	{
		// This is a more-vertical line
		permute = true;
		delta = delta.yx;
		P0 = P0.yx;
		P1 = P1.yx;
	}

	float stepDir = sign(delta.x);
	float invdx = stepDir / delta.x;

	// Track the derivatives of Q and k
	float3 dQ = (Q1 - Q0) * invdx;
	float dk = (k1 - k0) * invdx;
	float2 dP = float2(stepDir, delta.y * invdx);

	// Scale derivatives by the desired pixel stride and then
	// offset the starting values by the jitter fraction
	float strideScale = 1.0f - min(1.0f, vsOrig.z * cb.StrideZCutoff);
	float stride = 1.0f + strideScale * cb.Stride;
	dP *= stride;
	dQ *= stride;
	dk *= stride;

	P0 += dP * jitter;
	Q0 += dQ * jitter;
	k0 += dk * jitter;

	// Slide P from P0 to P1, (now-homogeneous) Q from Q0 to Q1, k from k0 to k1
	float4 PQk = float4(P0, Q0.z, k0);
	float4 dPQk = float4(dP, dQ.z, dk);
	float3 Q = Q0;

	// Adjust end condition for iteration direction
	float end = P1.x * stepDir;

	float stepCount = 0.0f;
	float prevZMaxEstimate = vsOrig.z;
	float rayZMin = prevZMaxEstimate;
	float rayZMax = prevZMaxEstimate;
	float sceneZMax = rayZMax + 200.0f;

	for(;
		((PQk.x * stepDir) <= end) && (stepCount < cb.MaxSteps) &&
		!IntersectsDepthBuffer(sceneZMax, rayZMin, rayZMax) &&
		(sceneZMax != 0.0f);
		++stepCount)
	{
		rayZMin = prevZMaxEstimate;
		rayZMax = (dPQk.z * 0.5f + PQk.z) / (dPQk.w * 0.5f + PQk.w);
		prevZMaxEstimate = rayZMax;

		if(rayZMin > rayZMax)
		{
			Swap(rayZMin, rayZMax);
		}

		hitPixel = permute ? PQk.yx : PQk.xy;

		sceneZMax = -LineariseDepth(1 - depthBuffer[hitPixel].r, cb.NearPlaneZ, cb.FarPlaneZ);

		PQk += dPQk;
	}

	// Advance Q based on the number of steps
	Q.xy += dQ.xy * stepCount;
	hitPoint = Q * (1.0f / PQk.w);

	return IntersectsDepthBuffer(sceneZMax, rayZMin, rayZMax);
}

float LineariseDepth(float depth, float nearPlane, float farPlane)
{
	float ProjectionA = farPlane / (farPlane - nearPlane);
	float ProjectionB = (-farPlane * nearPlane) / (farPlane - nearPlane);

	float linearDepth = ProjectionB / (depth - ProjectionA);

	return linearDepth;
}

float DistanceSquared(float2 a, float2 b)
{
	a -= b;
	return dot(a, a);
}

bool IntersectsDepthBuffer(float z, float minZ, float maxZ)
{
	/*
	* Based on how far away from the camera the depth is,
	* adding a bit of extra thickness can help improve some
	* artifacts. Driving this value up too high can cause
	* artifacts of its own.
	*/
	//float depthScale = min(1.0f, z * StrideZCutoff);
	//z += ZThickness + lerp(0.0f, 2.0f, depthScale);
	return (maxZ + cb.ZThickness >= z) && (minZ <= z);
}

void Swap(inout float a, inout float b)
{
	float t = a;
	a = b;
	b = t;
}

float3 Fresnel(float3 F0, float3 L, float3 H)
{
	float dotLH = saturate(dot(L, H));
	float dotLH5 = pow(1.0f - dotLH, 5);
	return F0 + (1.0 - F0) * (dotLH5);
}
