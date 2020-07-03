//
// Author: Daniel Gaard Hansen (danh@itu.dk)
//
#pragma once

// Engine
#include "GpuBuffer.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "TextureManager.h"

// Shaders
#include "Shaders/HybridSSRCSCompat.h"

namespace HybridSsr
{
enum RootParam
{
	RootParam_kConstants,
	RootParam_kTextures,
	RootParam_kCount
};

enum TextureTypes
{
	TextureTypes_kMainBuffer,
	TextureTypes_kDepthBuffer,
	TextureTypes_kNormalBuffer,
	TextureTypes_kAlbedoBuffer,
	TextureTypes_kDiffuse,
	TextureTypes_kCount
};

extern Texture g_Textures[TextureTypes_kCount];
extern HybridSsrConstantBuffer g_ConstantBufferData;
extern StructuredBuffer g_ConstantBuffer;
extern ComputePSO g_PSO;
extern RootSignature g_RS;

void InitializeResources(
	float NearPlaneZ,
	float FarPlaneZ);

void UpdateAndUploadConstantBufferData(
	ComputeContext& Ctx,
	StructuredBuffer& Buffer,
	HybridSsrConstantBuffer& Data,
	Math::Matrix4& ViewProjection,
	Math::Matrix4& InvViewProjection,
	Math::Matrix4& Projection,
	Math::Matrix4& InvProjection,
	Math::Matrix4& View,
	Math::Matrix4& InvView);
}
