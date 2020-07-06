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
#include "Camera.h"
#include "CameraType.h"
#include "Shaders/HybridSSRCSCompat.h"

namespace Math {
class VRCamera;
}

namespace HybridSsr
{
enum RootParam
{
	RootParam_kConstants,
	RootParam_kTextures,
	RootParam_kRenderTarget,
	RootParam_kCount,
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

void InitializeResources(
	float NearPlaneZ,
	float FarPlaneZ);

void ComputeHybridSsr(Math::Camera& Camera);
}
