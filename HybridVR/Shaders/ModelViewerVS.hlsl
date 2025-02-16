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
// Author(s):  James Stanard
//             Alex Nankervis
//

#include "ModelViewerRS.hlsli"

cbuffer VSConstants : register(b0)
{
    float4x4 modelToProjection;
    float4x4 modelToShadow;
    float3 ViewerPos;
	uint curCam;
};

struct VSInput
{
    float3 position : POSITION;
    float2 texcoord0 : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
};

cbuffer StartVertex : register(b1)
{
    uint baseVertex;
    uint materialIdx;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 worldPos : WorldPos;
    float2 uv : TexCoord0;
    float3 viewDir : TexCoord1;
    float3 shadowCoord : TexCoord2;
    float3 normal : Normal;
    float3 tangent : Tangent;
    float3 bitangent : Bitangent;
#if ENABLE_TRIANGLE_ID
    uint vertexID : TexCoord3;
#endif
	uint curCam : Cam;
};

[RootSignature(ModelViewer_RootSig)]
VSOutput main(VSInput vsInput, uint vertexID : SV_VertexID)
{
    VSOutput vsOutput;

    vsOutput.position = mul(modelToProjection, float4(vsInput.position, 1.0));
    vsOutput.worldPos = vsInput.position;
    vsOutput.uv = vsInput.texcoord0;
    vsOutput.viewDir = vsInput.position - ViewerPos;
    vsOutput.shadowCoord = mul(modelToShadow, float4(vsInput.position, 1.0)).xyz;

    vsOutput.normal = vsInput.normal;
    vsOutput.tangent = vsInput.tangent;
    vsOutput.bitangent = vsInput.bitangent;

#if ENABLE_TRIANGLE_ID
    //vsOutput.vertexID = (baseVertex & 0xFFFF) << 16 | (vertexID & 0xFFFF);
    vsOutput.vertexID = materialIdx << 24 | (vertexID & 0xFFFF);
#endif

	vsOutput.curCam = curCam;

    return vsOutput;
}
