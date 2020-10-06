#ifndef HLSL
#include "HlslCompat.h"
#endif

#ifdef HLSL
struct RayPayload
{
    bool SkipShading;
    float RayHitT;
    float Bounces;
    float Reflectivity;
};

#endif

#define RENDER(x) g_screenOutput[pixel] = x;
#define RENDER_AND_RETURN(x) g_screenOutput[pixel] = x;return
#define EPSILON 0.01

#pragma once
// Volatile part (can be split into its own CBV). 
struct DynamicCB
{
    float4x4 cameraToWorld;
    float4x4 worldToView;
    float3   worldCameraPosition;
    uint     padding;
    float2   resolution;
	uint     curCam;
    uint     useSceneLighting;
};

#ifdef HLSL
#ifndef SINGLE
static const float FLT_MAX = asfloat(0x7F7FFFFF);
#endif

RaytracingAccelerationStructure g_accel : register(t0);

RWTexture2DArray<float4> g_screenOutput : register(u2);

cbuffer HitShaderConstants : register(b0)
{
    float3 SunDirection;
    float3 SunColor;
    float3 AmbientColor;
    float4 ShadowTexelSize;
    float4x4 ModelToShadow;
    uint IsReflection;
    uint UseShadowRays;
    float NormalTextureStrength;
    int FlipNormals;
}

cbuffer b1 : register(b1)
{
    DynamicCB g_dynamic;
};

inline float3 UnprojectPixel(uint2 pixel, float depth)
{
    float2 xy = pixel + 0.5; // center in the middle of the pixel
    float2 screenPos = (xy / g_dynamic.resolution) * 2.0 - 1.0;

    // Invert y for DirectX-style coordinates
    screenPos.y = -screenPos.y;

    // Unproject into a ray
    float4 unprojected = mul(g_dynamic.cameraToWorld, float4(screenPos, depth, 1));
    float3 result = unprojected.xyz / unprojected.w;
    return result;
}

inline void GenerateCameraRay(uint2 pixel, out float3 origin, out float3 direction)
{
    float3 world = UnprojectPixel(pixel, 0);
    origin = g_dynamic.worldCameraPosition;
    direction = normalize(world - origin);
}

void GenerateReflectionRay(float3 position, float3 incidentDirection, float3 normal, out float3 out_origin, out float3 out_direction)
{
    out_direction = normalize(reflect(incidentDirection, normal));
    out_origin = position - incidentDirection * 0.001f;
}

float CalculateReflectivity(float specular, float3 viewDir, float3 normal)
{
	float result = specular * pow(1.0 - saturate(dot(-viewDir, normal)), 5.0);
    return 0.8;
}

void GenerateSSRRay(
    float2 pixel, 
    float depth,
    float3 normal, 
    float specular,
    out float3 out_origin,
    out float3 out_direction, 
    out float out_reflectivity)
{
    float3 pixelInWorld = UnprojectPixel(pixel.xy, depth);

    float3 viewDir = normalize(pixelInWorld - g_dynamic.worldCameraPosition);
    out_reflectivity = CalculateReflectivity(specular, viewDir, normal);
    GenerateReflectionRay(pixelInWorld, viewDir, normal, 
        out_origin, out_direction);
}

void GenerateSSRRay(float2 pixel, float depth, float3 normal, float specular,
    out float3 out_origin, out float3 out_direction, out float out_reflectivity,
    out float out_primaryRayLength)
{
    float3 pixelInWorld = UnprojectPixel(pixel.xy, depth);

    float3 primaryRay = pixelInWorld - g_dynamic.worldCameraPosition;
    out_primaryRayLength = length(primaryRay);
    float3 viewDir = normalize(primaryRay);
    out_reflectivity = CalculateReflectivity(specular, viewDir, normal);
    GenerateReflectionRay(pixelInWorld, viewDir, normal, out_origin, out_direction);
}

void FireRay(float3 origin, float3 direction, float bounces, float reflectivity)
{
	RayDesc rayDesc;
	rayDesc.Origin = origin;
	rayDesc.Direction = direction;
	rayDesc.TMin= 0;
	rayDesc.TMax = FLT_MAX;

	RayPayload payload;
	payload.SkipShading = false;
	payload.RayHitT = FLT_MAX;
	payload.Bounces = bounces;
	payload.Reflectivity = reflectivity;
	TraceRay(
		g_accel, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 
		~0, 0, 1, 0, rayDesc, payload);
}
#endif
