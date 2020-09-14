//
// Author: Daniel Gaard Hansen (danh@itu.dk)
//
#pragma once
#include "CameraType.h"

class GraphicsContext;
class ColorBuffer;
class DepthBuffer;

namespace Math {
class VRCamera;
class Camera;
}

namespace HybridSsr
{
enum class RootParam
{
	kConstants,
	kTextures,
	kRenderTarget,
	kNormals,
	
	kCount,
};

extern float g_SsrScale;
extern float g_ZThickness;
extern float g_Stride;
extern float g_MaxSteps;
extern float g_MaxDistance;
extern float g_StrideZCutoff;

enum class TextureType
{
	kMainBuffer,
	kDepthBuffer,
	kAlbedoBuffer,
	
	kCount,
	kNormalBuffer //This is used as a UAV, so we don't count it
};

void InitializeResources();

void ComputeHybridSsr(
	GraphicsContext& GraphicsCtx,
	Math::VRCamera& VRCamera,
	Cam::CameraType CamType,
	ColorBuffer& Color, DepthBuffer& Depth, ColorBuffer& Normal);
}
