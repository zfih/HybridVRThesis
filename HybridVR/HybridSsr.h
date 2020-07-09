//
// Author: Daniel Gaard Hansen (danh@itu.dk)
//
#pragma once
#include "CameraType.h"

class GraphicsContext;
class ColorBuffer;
class DepthBuffer;

namespace Math {
	class Camera;
}

namespace HybridSsr
{
enum class RootParam
{
	kConstants,
	kTextures,
	kRenderTarget,
	
	kCount,
};

enum class TextureType
{
	kMainBuffer,
	kDepthBuffer,
	kNormalBuffer,
	kAlbedoBuffer,
	
	kCount
};

void InitializeResources(
	float NearPlaneZ,
	float FarPlaneZ);

void ComputeHybridSsr(
	GraphicsContext& Ctx,
	Math::Camera& Camera,
	Cam::CameraType Type,
	ColorBuffer& Color, DepthBuffer& Depth, ColorBuffer& Normal);
}
