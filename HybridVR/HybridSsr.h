//
// Author: Daniel Gaard Hansen (danh@itu.dk)
//
#pragma once

namespace Math {
	class Camera;
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

void ComputeHybridSsr(
	Math::Camera& Camera,
	const float ScreenWidth, const float ScreenHeight);
}
