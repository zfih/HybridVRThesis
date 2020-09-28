//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author(s):    James Stanard, Christopher Wallis
//

#define HLSL
#include "ModelViewerRaytracing.h"
#include "RayTracingHlslCompat.h"
//#include "LightGrid.hlsli" // dxc does not like this :(

struct LightData
{
	float3 pos;
	float radiusSq;

	float3 color;
	uint type;

	float3 coneDir;
	float2 coneAngles; // x = 1.0f / (cos(coneInner) - cos(coneOuter)), y = cos(coneOuter)

	float4x4 shadowTextureMatrix;
};

cbuffer Material : register(b3)
{
uint MaterialID;
uint Reflective;
}

StructuredBuffer<RayTraceMeshInfo> g_meshInfo : register(t1);
ByteAddressBuffer g_indices : register(t2);
ByteAddressBuffer g_attributes : register(t3);
Texture2D<float> texShadow : register(t4);
Texture2D<float> texSSAO : register(t5);
SamplerState g_s0 : register(s0);
SamplerComparisonState shadowSampler : register(s1);

Texture2D<float4> g_localTexture : register(t6);
Texture2D<float4> g_localNormal : register(t7);
Texture2D<float4> g_localSpecular : register(t8);

Texture2DArray<float4> normals : register(t13);
StructuredBuffer<LightData> lightBuffer : register(t14);

uint3 Load3x16BitIndices(
	uint offsetBytes)
{
	const uint dwordAlignedOffset = offsetBytes & ~3;

	const uint2 four16BitIndices = g_indices.Load2(dwordAlignedOffset);

	uint3 indices;

	if (dwordAlignedOffset == offsetBytes)
	{
		indices.x = four16BitIndices.x & 0xffff;
		indices.y = (four16BitIndices.x >> 16) & 0xffff;
		indices.z = four16BitIndices.y & 0xffff;
	}
	else
	{
		indices.x = (four16BitIndices.x >> 16) & 0xffff;
		indices.y = four16BitIndices.y & 0xffff;
		indices.z = (four16BitIndices.y >> 16) & 0xffff;
	}

	return indices;
}

float GetShadow(float3 ShadowCoord)
{
	const float Dilation = 2.0;
	float d1 = Dilation * ShadowTexelSize.x * 0.125;
	float d2 = Dilation * ShadowTexelSize.x * 0.875;
	float d3 = Dilation * ShadowTexelSize.x * 0.625;
	float d4 = Dilation * ShadowTexelSize.x * 0.375;
	float result = (
		2.0 * texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy, ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(-d2, d1), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(-d1, -d2), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(d2, -d1), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(d1, d2), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(-d4, d3), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(-d3, -d4), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(d4, -d3), ShadowCoord.z) +
		texShadow.SampleCmpLevelZero(shadowSampler, ShadowCoord.xy + float2(d3, d4), ShadowCoord.z)
	) / 10.0;
	return result * result;
}

float2 GetUVAttribute(uint byteOffset)
{
	return asfloat(g_attributes.Load2(byteOffset));
}

void AntiAliasSpecular(inout float3 texNormal, inout float gloss)
{
	float normalLenSq = dot(texNormal, texNormal);
	float invNormalLen = rsqrt(normalLenSq);
	texNormal *= invNormalLen;
	gloss = lerp(1, gloss, rcp(invNormalLen));
}

// Apply fresnel to modulate the specular albedo
void FSchlick(inout float3 specular, inout float3 diffuse, float3 lightDir, float3 halfVec)
{
	float fresnel = pow(1.0 - saturate(dot(lightDir, halfVec)), 5.0);
	specular = lerp(specular, 1, fresnel);
	diffuse = lerp(diffuse, 0, fresnel);
}

float3 ApplyLightCommon(
	float3 diffuseColor,  // Diffuse albedo
	float3 specularColor, // Specular albedo
	float specularMask,   // Where is it shiny or dingy?
	float gloss,          // Specular power
	float3 normal,        // World-space normal
	float3 viewDir,       // World-space vector from eye to point
	float3 lightDir,      // World-space vector from point to light
	float3 lightColor     // Radiance of directional light
)
{
	float3 halfVec = normalize(lightDir - viewDir);
	float nDotH = saturate(dot(halfVec, normal));

	FSchlick(specularColor, diffuseColor, lightDir, halfVec);

	float specularFactor = specularMask * pow(nDotH, gloss) * (gloss + 2) / 8;

	float nDotL = saturate(dot(normal, lightDir));

	return nDotL * lightColor * (diffuseColor + specularFactor * specularColor);
}

float3 ApplyPointLight(
	float3 diffuseColor, // Diffuse albedo
	float3 specularColor, // Specular albedo
	float specularMask, // Where is it shiny or dingy?
	float gloss, // Specular power
	float3 normal, // World-space normal
	float3 viewDir, // World-space vector from eye to point
	float3 worldPos, // World-space fragment position
	float3 lightPos, // World-space light position
	float lightRadiusSq,
	float3 lightColor // Radiance of directional light
)
{
	float3 lightDir = lightPos - worldPos;
	float lightDistSq = dot(lightDir, lightDir);
	float invLightDist = rsqrt(lightDistSq);
	lightDir *= invLightDist;

	// modify 1/d^2 * R^2 to fall off at a fixed radius
	// (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
	float distanceFalloff = lightRadiusSq * (invLightDist * invLightDist);
	distanceFalloff = max(0, distanceFalloff - rsqrt(distanceFalloff));

	return distanceFalloff * ApplyLightCommon(
		diffuseColor,
		specularColor,
		specularMask,
		gloss,
		normal,
		viewDir,
		lightDir,
		lightColor
	);
}

float3 ApplyConeLight(
	float3 diffuseColor, // Diffuse albedo
	float3 specularColor, // Specular albedo
	float specularMask, // Where is it shiny or dingy?
	float gloss, // Specular power
	float3 normal, // World-space normal
	float3 viewDir, // World-space vector from eye to point
	float3 worldPos, // World-space fragment position
	float3 lightPos, // World-space light position
	float lightRadiusSq,
	float3 lightColor, // Radiance of directional light
	float3 coneDir,
	float2 coneAngles
)
{
	float3 lightDir = lightPos - worldPos;
	float lightDistSq = dot(lightDir, lightDir);
	float invLightDist = rsqrt(lightDistSq);
	lightDir *= invLightDist;

	// modify 1/d^2 * R^2 to fall off at a fixed radius
	// (R/d)^2 - d/R = [(1/d^2) - (1/R^2)*(d/R)] * R^2
	float distanceFalloff = lightRadiusSq * (invLightDist * invLightDist);
	distanceFalloff = max(0, distanceFalloff - rsqrt(distanceFalloff));

	float coneFalloff = dot(-lightDir, coneDir);
	coneFalloff = saturate((coneFalloff - coneAngles.y) * coneAngles.x);

	return (coneFalloff * distanceFalloff) * ApplyLightCommon(
		diffuseColor,
		specularColor,
		specularMask,
		gloss,
		normal,
		viewDir,
		lightDir,
		lightColor
	);
}

float3 ApplySceneLights(
	float3 diffuse, float3 specular, float specularMask, float gloss,
	float3 normal, float3 viewDir, float3 pos)
{
	float3 colorSum;
	for (int pointLightIndex = 0; pointLightIndex < 128; pointLightIndex++)
	{
		LightData lightData = lightBuffer[pointLightIndex];
		if (lightData.type == 0)
		{
			colorSum += ApplyPointLight(
				diffuse,
				specular,
				specularMask,
				gloss,
				normal,
				viewDir,
				pos,
				lightData.pos,
				lightData.radiusSq,
				lightData.color);
		}
		// WONTFIX We don't know what this 'lightIndex' is, so we're just leaving it out. We could
		//   perhaps read it as a point light or a cone light instead.
		else if (lightData.type == 1 || lightData.type == 2)
		{
			colorSum += ApplyConeLight(
				diffuse, specular, specularMask, gloss, normal, viewDir, pos, lightData.pos,
				lightData.radiusSq, lightData.color, lightData.coneDir, lightData.coneAngles);
		}
		else if (lightData.type == 2)
		{
			/*colorSum += ApplyConeShadowedLight(
				diffuse, specular, specularMask, gloss, normal, viewDir, pos,
				lightData.pos, lightData.radiusSq, lightData.color, lightData.coneDir,
				lightData.coneAngles, lightData.shadowTextureMatrix, lightIndex);
				*/
		}
	}
	return colorSum;
}

float3 RayPlaneIntersection(float3 planeOrigin, float3 planeNormal, float3 rayOrigin, float3 rayDirection)
{
	float t = dot(-planeNormal, rayOrigin - planeOrigin) / dot(planeNormal, rayDirection);
	return rayOrigin + rayDirection * t;
}

bool Inverse2x2(float2x2 mat, out float2x2 inverse)
{
	float determinant = mat[0][0] * mat[1][1] - mat[1][0] * mat[0][1];

	float rcpDeterminant = rcp(determinant);
	inverse[0][0] = mat[1][1];
	inverse[1][1] = mat[0][0];
	inverse[1][0] = -mat[0][1];
	inverse[0][1] = -mat[1][0];
	inverse = rcpDeterminant * inverse;

	return abs(determinant) > 0.00000001;
}


/* TODO: Could be precalculated per triangle
 Using implementation described in PBRT, finding the partial derivative of the (change in position)/(change in UV coordinates) 
 a.k.a dp/du and dp/dv
 
 Given the 3 UV and 3 triangle points, this can be represented as a linear equation:

 (uv0.u - uv2.u, uv0.v - uv2.v)   (dp/du)   =     (p0 - p2)
 (uv1.u - uv2.u, uv1.v - uv2.v)   (dp/dv)   =     (p1 - p2)

 To solve for dp/du, we invert the 2x2 matrix on the left side to get

 (dp/du)   = (uv0.u - uv2.u, uv0.v - uv2.v)^-1  (p0 - p2)
 (dp/dv)   = (uv1.u - uv2.u, uv1.v - uv2.v)     (p1 - p2)
*/
void CalculateTrianglePartialDerivatives(float2 uv0, float2 uv1, float2 uv2, float3 p0, float3 p1, float3 p2,
                                         out float3 dpdu, out float3 dpdv)
{
	float2x2 linearEquation;
	linearEquation[0] = uv0 - uv2;
	linearEquation[1] = uv1 - uv2;

	float2x3 pointVector;
	pointVector[0] = p0 - p2;
	pointVector[1] = p1 - p2;
	float2x2 inverse;
	Inverse2x2(linearEquation, inverse);
	dpdu = pointVector[0] * inverse[0][0] + pointVector[1] * inverse[0][1];
	dpdv = pointVector[0] * inverse[1][0] + pointVector[1] * inverse[1][1];
}

/*
Using implementation described in PBRT, finding the derivative for the UVs (dU, dV)  in both the x and y directions

Given the original point and the offset points (pX and pY) + the partial derivatives, the linear equation can be formed:
Note described only with pX, but the same is also applied to pY

( dpdu.x, dpdv.x)          =   (pX.x - p.x)
( dpdu.y, dpdv.y)   (dU)   =   (pX.y - p.y)
( dpdu.z, dpdv.z)   (dV)   =   (pX.z - p.z)

Because the problem is over-constrained (3 equations and only 2 unknowns), we pick 2 channels, and solve for dU, dV by inverting the matrix

dU    =   ( dpdu.x, dpdv.x)^-1  (pX.x - p.x)
dV    =   ( dpdu.y, dpdv.y)     (pX.y - p.y)
*/

void CalculateUVDerivatives(float3 normal, float3 dpdu, float3 dpdv, float3 p, float3 pX, float3 pY, float bounces,
                            out float2 ddX, out float2 ddY)
{
	int2 indices;
	float3 absNormal = abs(normal);
	if (absNormal.x > absNormal.y && absNormal.x > absNormal.z)
	{
		indices = int2(1, 2);
	}
	else if (absNormal.y > absNormal.z)
	{
		indices = int2(0, 2);
	}
	else
	{
		indices = int2(0, 1);
	}

	float2x2 linearEquation;
	linearEquation[0] = float2(dpdu[indices.x], dpdv[indices.x]);
	linearEquation[1] = float2(dpdu[indices.y], dpdv[indices.y]);

	float dampen = (IsReflection || bounces == 1) ? 50.0f : 2.0f;

	float2x2 inverse;
	Inverse2x2(linearEquation, inverse);
	float2 pointOffset = float2(pX[indices.x] - p[indices.x], pX[indices.y] - p[indices.y]);
	ddX = abs(mul(inverse, pointOffset)) / dampen;

	pointOffset = float2(pY[indices.x] - p[indices.x], pY[indices.y] - p[indices.y]);
	ddY = abs(mul(inverse, pointOffset)) / dampen;
}

float3 ApplySRGBCurve(float3 x)
{
	// Approximately pow(x, 1.0 / 2.2)
	return x < 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

#define USE_SAMPLE_GRAD

#ifdef USE_SAMPLE_GRAD
#define SAMPLE_TEX(texName) (texName.SampleGrad(g_s0, uv, ddx, ddy))
#else
#define SAMPLE_TEX(texName) (texName.SampleLevel(g_s0, uv, 0))
#endif

float3 GetNormal(
	float2 uv, float2 ddx, float2 ddy,
	float3 vsNormal, float3 vsTangent, float3 vsBitangent,
	inout float inout_gloss, out bool out_success)
{
	float3 result = SAMPLE_TEX(g_localNormal).rgb * 2.0 - 1.0;

	AntiAliasSpecular(result, inout_gloss);
	float3x3 tbn = float3x3(vsTangent, vsBitangent, vsNormal);
	result = mul(result, tbn);

	// Normalize result...
	float lenSq = dot(result, result);

	// Detect degenerate normals
	out_success = !(!isfinite(lenSq) || lenSq < 1e-6);

	result *= rsqrt(lenSq);
	return result;
}

float GetShadowMultiplier(float3 position, int bounces)
{
	float shadow = 1.0;
	if (UseShadowRays)
	{
		float3 shadowDirection = SunDirection;
		float3 shadowOrigin = position;
		RayDesc rayDesc =
		{
			shadowOrigin,
			0.1f,
			shadowDirection,
			FLT_MAX
		};
		RayPayload shadowPayload;
		shadowPayload.SkipShading = true;
		shadowPayload.RayHitT = FLT_MAX;
		shadowPayload.Bounces = bounces + 1;
		TraceRay(g_accel, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, ~0, 0, 1, 0, rayDesc, shadowPayload);
		if (shadowPayload.RayHitT < FLT_MAX)
		{
			shadow = 0.0;
		}
	}
	else
	{
		// TODO: This could be pre-calculated once per vertex if this mul per pixel was a concern
		float4 shadowCoord = mul(ModelToShadow, float4(position, 1.0f));
		shadow = GetShadow(shadowCoord.xyz);
	}

	return shadow;
}

[shader("closesthit")]
void Hit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
	int3 pixel = int3(DispatchRaysIndex().xy, g_dynamic.curCam);


	payload.RayHitT = RayTCurrent();
	if (payload.SkipShading)
	{
		return;
	}

	uint materialID = MaterialID;
	uint triangleID = PrimitiveIndex();

	RayTraceMeshInfo info = g_meshInfo[materialID];

	const uint3 ii = Load3x16BitIndices(info.m_indexOffsetBytes + PrimitiveIndex() * 3 * 2);
	const float2 uv0 = GetUVAttribute(info.m_uvAttributeOffsetBytes + ii.x * info.m_attributeStrideBytes);
	const float2 uv1 = GetUVAttribute(info.m_uvAttributeOffsetBytes + ii.y * info.m_attributeStrideBytes);
	const float2 uv2 = GetUVAttribute(info.m_uvAttributeOffsetBytes + ii.z * info.m_attributeStrideBytes);

	float3 bary = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
	float2 uv = bary.x * uv0 + bary.y * uv1 + bary.z * uv2;

	const float3 normal0 = asfloat(
		g_attributes.Load3(info.m_normalAttributeOffsetBytes + ii.x * info.m_attributeStrideBytes));
	const float3 normal1 = asfloat(
		g_attributes.Load3(info.m_normalAttributeOffsetBytes + ii.y * info.m_attributeStrideBytes));
	const float3 normal2 = asfloat(
		g_attributes.Load3(info.m_normalAttributeOffsetBytes + ii.z * info.m_attributeStrideBytes));
	float3 vsNormal = normalize(normal0 * bary.x + normal1 * bary.y + normal2 * bary.z);

	const float3 tangent0 = asfloat(
		g_attributes.Load3(info.m_tangentAttributeOffsetBytes + ii.x * info.m_attributeStrideBytes));
	const float3 tangent1 = asfloat(
		g_attributes.Load3(info.m_tangentAttributeOffsetBytes + ii.y * info.m_attributeStrideBytes));
	const float3 tangent2 = asfloat(
		g_attributes.Load3(info.m_tangentAttributeOffsetBytes + ii.z * info.m_attributeStrideBytes));
	float3 vsTangent = normalize(tangent0 * bary.x + tangent1 * bary.y + tangent2 * bary.z);

	// Reintroduced the bitangent because we aren't storing the handedness of the tangent frame anywhere.  Assuming the space
	// is right-handed causes normal maps to invert for some surfaces.  The Sponza mesh has all three axes of the tangent frame.
	//float3 vsBitangent = normalize(cross(vsNormal, vsTangent)) * (isRightHanded ? 1.0 : -1.0);
	const float3 bitangent0 = asfloat(
		g_attributes.Load3(info.m_bitangentAttributeOffsetBytes + ii.x * info.m_attributeStrideBytes));
	const float3 bitangent1 = asfloat(
		g_attributes.Load3(info.m_bitangentAttributeOffsetBytes + ii.y * info.m_attributeStrideBytes));
	const float3 bitangent2 = asfloat(
		g_attributes.Load3(info.m_bitangentAttributeOffsetBytes + ii.z * info.m_attributeStrideBytes));
	float3 vsBitangent = normalize(bitangent0 * bary.x + bitangent1 * bary.y + bitangent2 * bary.z);

	// TODO: Should just store uv partial derivatives in here rather than loading position and caculating it per pixel
	const float3 p0 = asfloat(
		g_attributes.Load3(info.m_positionAttributeOffsetBytes + ii.x * info.m_attributeStrideBytes));
	const float3 p1 = asfloat(
		g_attributes.Load3(info.m_positionAttributeOffsetBytes + ii.y * info.m_attributeStrideBytes));
	const float3 p2 = asfloat(
		g_attributes.Load3(info.m_positionAttributeOffsetBytes + ii.z * info.m_attributeStrideBytes));

	float3 worldPosition = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();

	uint2 threadID = DispatchRaysIndex().xy;
	float3 ddxOrigin, ddxDir, ddyOrigin, ddyDir;
	GenerateCameraRay(uint2(threadID.x + 1, threadID.y), ddxOrigin, ddxDir);
	GenerateCameraRay(uint2(threadID.x, threadID.y + 1), ddyOrigin, ddyDir);

	float3 triangleNormal = normalize(cross(p2 - p0, p1 - p0));
	float3 xOffsetPoint = RayPlaneIntersection(worldPosition, triangleNormal, ddxOrigin, ddxDir);
	float3 yOffsetPoint = RayPlaneIntersection(worldPosition, triangleNormal, ddyOrigin, ddyDir);

	float3 dpdu, dpdv;
	CalculateTrianglePartialDerivatives(uv0, uv1, uv2, p0, p1, p2, dpdu, dpdv);
	float2 ddx, ddy;
	CalculateUVDerivatives(triangleNormal, dpdu, dpdv, worldPosition, xOffsetPoint, yOffsetPoint, payload.Bounces, ddx,
	                       ddy);

	//// ACTUAL SHADING

	float gloss = 128.0;
	bool hasValidNormal;
	float3 normal = GetNormal(
		uv, ddx, ddy, vsNormal, vsTangent, vsBitangent, gloss, hasValidNormal);

	if (!hasValidNormal)
	{
		return;
	}

	const float3 specularAlbedo = float3(0.56, 0.56, 0.56);
	const float specularMask = SAMPLE_TEX(g_localSpecular).g;
	const float3 viewDir = WorldRayDirection();

	const float3 diffuseColor = SAMPLE_TEX(g_localTexture).rgb;

	float3 colorSum = 0;

	// ApplyAmbientLight
	colorSum += AmbientColor * diffuseColor.rgb;

	// ApplyDirectionalLight.GetShadow
	float shadow = GetShadowMultiplier(worldPosition, payload.Bounces);

	// ApplyDirectionalLight.(Shadow * Light Common)
	colorSum += shadow * ApplyLightCommon(
		diffuseColor.rgb,
		specularAlbedo,
		specularMask,
		gloss,
		normal,
		viewDir,
		SunDirection,
		SunColor);

#if 0
	//if (g_dynamic.useSceneLighting) // Wontfix: This causes reflections to be purple
	{
		colorSum += ApplySceneLights(
			diffuseColor.rgb, specularAlbedo, specularMask, gloss, normal,
			viewDir, worldPosition);
	}
#endif

	colorSum = ApplySRGBCurve(colorSum);

	if (payload.Bounces > 0)
	{
		colorSum = g_screenOutput[pixel].rgb * (1 - payload.Reflectivity) + payload.Reflectivity * colorSum;
	}

	g_screenOutput[pixel] = float4(colorSum, 1);

	float reflectivity = CalculateReflectivity(specularMask, viewDir, normal);

	if (Reflective && payload.Bounces < 3)
	{
		float3 origin;
		float3 direction;
		GenerateReflectionRay(
			worldPosition, viewDir, normal, origin, direction);
		FireRay(worldPosition, direction, payload.Bounces + 1, payload.Reflectivity * reflectivity);
	}
}
