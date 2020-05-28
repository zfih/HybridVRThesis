/*
  Copyrighted(c) 2020, TH Köln.All rights reserved. Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met :
 
  * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
  * Neither the name of TH Köln nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT OWNER
  OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES(INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  Authors: Niko Wissmann
 */

import Raytracing;

#define M_1_DIVIDE_PI  0.318309886183790671538
#define NUM_LIGHTSOURCES 1

RWTexture2D<float4> gOutput;
Texture2D gShadowMap;
Texture2D gSkybox;

SamplerState defaultSampler;
SamplerComparisonState gPCFCompSampler;

shared cbuffer PerFrameCBRayTrace
{
    float4x4 gInvView;
    float4x4 gInvViewProj;
    float2 gViewportDims;
    float3 gClearColor;
    float gSpreadAngle;
    float4x4 gLightViewProj;
    float gBias;
    uint gKernelSize;
};

struct PrimaryRayData
{
    float4 outputColor;
};

// calculated LOD based on cone foot print
void calcLOD(uint triangleIndex, float hitDistance, float3 vNormalW, float3 rayDirW, out float lod)
{
    uint3 indices = getIndices(triangleIndex);

    float3 p[3];
    p[0] = asfloat(gPositions.Load3((indices[0] * 3) * 4));
    p[1] = asfloat(gPositions.Load3((indices[1] * 3) * 4));
    p[2] = asfloat(gPositions.Load3((indices[2] * 3) * 4));
    float triangleArea = length(cross(p[1] - p[0], p[2] - p[0]));

    float2 tc[3];
    tc[0] = asfloat(gTexCrds.Load3((indices[0] * 3) * 4)).xy;
    tc[1] = asfloat(gTexCrds.Load3((indices[1] * 3) * 4)).xy;
    tc[2] = asfloat(gTexCrds.Load3((indices[2] * 3) * 4)).xy;
    float w, h;
    gMaterial.resources.baseColor.GetDimensions(w, h);
    float texelSpaceArea = w * h * abs((tc[1].x - tc[0].x) * (tc[2].y - tc[0].y) - (tc[2].x - tc[0].x) * (tc[1].y - tc[0].y));

    float baseLOD = 0.5 * log2(texelSpaceArea / triangleArea);
    float footprint = gSpreadAngle * hitDistance * (1 / abs(dot(vNormalW, rayDirW)));
    lod = baseLOD + log2(footprint);
}

// PCF shadow factor
float getShadowFactor(float3 posW)
{
    float4 pixelLight = mul(float4(posW, 1), gLightViewProj);
    pixelLight /= pixelLight.w;

    float2 texC = float2(pixelLight.x, -pixelLight.y) * 0.5 + 0.5;
    pixelLight.z -= gBias;

    float w, h;
    gShadowMap.GetDimensions(w, h);
    float xOffset = 1.0 / w;
    float yOffset = 1.0 / h;

    float kernelSize = gKernelSize - 1;
    float halfKernelSize = kernelSize / 2.0;

    float factor = 0.0;
    for (float y = -halfKernelSize; y <= halfKernelSize; y += 1.0)
    {
        for (float x = -halfKernelSize; x <= halfKernelSize; x += 1.0)
        {
            float2 offsetTexC = texC + float2(x * xOffset, y * yOffset);
            factor += gShadowMap.SampleCmpLevelZero(gPCFCompSampler, offsetTexC, pixelLight.z);
        }
    }

    return saturate(factor / (kernelSize * kernelSize));
}

float2 wsVectorToLatLong(float3 dir)
{
    float3 p = normalize(dir);
    float u = (1.f + atan2(p.x, -p.z) * M_1_DIVIDE_PI) * 0.5f;
    float v = acos(p.y) * M_1_DIVIDE_PI;
    return float2(-u+0.25, v);
}

[shader("miss")]
void primaryMiss(inout PrimaryRayData hitData)
{
#ifdef _RENDER_ENV_MAP
    float2 texDims;
    gSkybox.GetDimensions(texDims.x, texDims.y);
    float2 uv = wsVectorToLatLong(WorldRayDirection());
    hitData.outputColor = gSkybox.SampleLevel(defaultSampler, uv, 0);
#else
    hitData.outputColor = float4(0, 0, 0, 1);
#endif
}

[shader("closesthit")]
void primaryClosestHit(inout PrimaryRayData hitData, in BuiltInTriangleIntersectionAttributes attribs)
{
    // Get the hit-point data
    float3 rayOrigW = WorldRayOrigin();
    float3 rayDirW = WorldRayDirection();
    float hitT = RayTCurrent();
    uint triangleIndex = PrimitiveIndex();

    VertexOut v = getVertexAttributes(triangleIndex, attribs);

    float lod;
    calcLOD(triangleIndex, hitT, v.normalW, rayDirW, lod);

    ShadingData sd = prepareShadingData(v, gMaterial, rayOrigW, lod);

    float3 shadingColor = 0;
    for (uint l = 0; l < gLightsCount; l++)
    {
        float shadowFactor = 1;
        if (l == 0)
        {
            shadowFactor = getShadowFactor(v.posW);
        }
        shadingColor += evalMaterial(sd, gLights[l], shadowFactor).color.rgb;
    }

    shadingColor += evalMaterial(sd, gLightProbe).color.rgb;

    hitData.outputColor.rgb = shadingColor;
    hitData.outputColor.a = 1;
}

[shader("raygeneration")]
void rayGen()
{
    uint3 launchIndex = DispatchRaysIndex();

     // check if already shaded by pixel shader
    if (gOutput[launchIndex.xy].a != 0)
    {
        return;
    }

    // Ray generation with correct stereo projection matrix
    RayDesc ray;
    ray.Origin = gInvView[3].xyz;
    float2 pixelNDC = (launchIndex.xy + 0.5) / gViewportDims;
    float2 pixelScreen = 2 * pixelNDC - 1;
    float3 pixelScreenMapped = float3(pixelScreen.x, -pixelScreen.y, -1);
    float4 pixelWorld = mul(float4(pixelScreenMapped, 1), gInvViewProj);
    pixelWorld /= pixelWorld.w;
    ray.Direction = normalize(pixelWorld.xyz - ray.Origin);

    ray.TMin = gCamera.nearZ;
    ray.TMax = gCamera.farZ;

    PrimaryRayData hitData;
    TraceRay(gRtScene, 0 /*rayFlags*/, 0xFF, 0 /* ray index*/, hitProgramCount, 0, ray, hitData);
    gOutput[launchIndex.xy] = hitData.outputColor;
}
