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
extern float g_FadeStart;
extern float g_FadeEnd;
extern bool g_DoFading;

enum class TextureType
{
	kMainBuffer,
	kDepthBuffer,
	kAlbedoBuffer,
	
	kCount
};

void InitializeResources();

void ComputeHybridSsr(
	GraphicsContext& GraphicsCtx,
	Math::VRCamera& VRCamera,
	Cam::CameraType CamType,
	ColorBuffer& Color, DepthBuffer& Depth, ColorBuffer& Normal);
}
