//
// Author: Daniel Gaard Hansen (danh@itu.dk)
//
#include "HybridSsr.h"

// Engine
#include "Camera.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "DepthBuffer.h"
#include "ColorBuffer.h"
#include "TextureManager.h"
#include "CommandContext.h"

// Shaders
#include "Shaders/HybridSSRCSCompat.h"
#include <CompiledShaders/HybridSsrCS.h>

namespace HybridSsr
{
static StructuredBuffer g_ConstantBuffer;
static ComputePSO g_PSO;
static RootSignature g_RS;

float HybridSsr::g_SsrScale = 1;
float HybridSsr::g_ZThickness = 0.05f;
float HybridSsr::g_Stride = 1;
float HybridSsr::g_MaxSteps = 400;
float HybridSsr::g_MaxDistance = 200;
float HybridSsr::g_StrideZCutoff = 0;


void CopyVectorToFloat3(float3 &Dst, Math::Vector3 Src);
void CopyVectorToFloat4(float4 &Dst, Math::Vector4 Src);
void CopyMatrixToFloat4x4(float4x4 &Dst, Math::Matrix4 &Src);

void CreateConstantBuffer(
	ComputeContext &Ctx,
	Math::Camera &Camera,
	float ScreenWidth,
	float ScreenHeight);

void InitializeResources()
{
	// Root signature
	g_RS.Reset((int)RootParam::kCount, 0);
	g_RS[(int)RootParam::kConstants].InitAsConstantBuffer((int)RootParam::kConstants);
	g_RS[(int)RootParam::kTextures].InitAsDescriptorRange(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, (int)TextureType::kCount);
	g_RS[(int)RootParam::kRenderTarget].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
	g_RS.Finalize(L"HybridSsr Root Signature");

	g_ConstantBuffer.Create(L"HybridSsr Constant Buffer", 1, sizeof(HybridSsrConstantBuffer));

	// Create PSO
	g_PSO.SetRootSignature(g_RS);
	g_PSO.SetComputeShader(g_pHybridSsrCS, sizeof(g_pHybridSsrCS));
	g_PSO.Finalize();
}

void ComputeHybridSsr(
	GraphicsContext &GraphicsCtx,
	Math::VRCamera &VRCamera,
	Cam::CameraType CamType,
	ColorBuffer &Color, DepthBuffer &Depth, ColorBuffer &Normal)
{
	// Setup context
	ComputeContext &ctx = GraphicsCtx.GetComputeContext();

	ScopedTimer timer(L"HybridSSR", ctx);

	const float width = static_cast<float>(Color.GetWidth());
	const float height = static_cast<float>(Color.GetHeight());

	Math::Camera &camera = *VRCamera[CamType];

	CreateConstantBuffer(
		ctx,
		camera,
		width,
		height);

	// Transition resources
	ctx.TransitionResource(Color, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(Depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(Normal, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	// Setup pipeline
	ctx.SetRootSignature(g_RS);
	ctx.SetPipelineState(g_PSO);

	// Constants 
	ctx.SetConstantBuffer(
		(int)RootParam::kConstants,
		g_ConstantBuffer.GetGpuVirtualAddress());

	// Textures
	ctx.SetDynamicDescriptor(
		(int)RootParam::kTextures,
		(int)TextureType::kMainBuffer, Color.GetSubSRV(CamType));
	ctx.SetDynamicDescriptor(
		(int)RootParam::kTextures,
		(int)TextureType::kDepthBuffer, Depth.GetSubSRV(CamType));
	ctx.SetDynamicDescriptor(
		(int)RootParam::kTextures,
		(int)TextureType::kNormalBuffer, Normal.GetSubSRV(CamType));
	ctx.SetDynamicDescriptor(
		(int)RootParam::kTextures,
		(int)TextureType::kAlbedoBuffer, Color.GetSubSRV(CamType));
	// Render target UAV
	ctx.SetDynamicDescriptor(
		(int)RootParam::kRenderTarget, 0, Color.GetSubUAV(CamType));

	const int numThreadsX = width / 8 + 1;
	const int numThreadsY = height / 8 + 1;
	ctx.Dispatch(numThreadsX, numThreadsY, 1);

	ctx.TransitionResource(Color, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void CreateConstantBuffer(
	ComputeContext &Ctx,
	Math::Camera &Camera,
	float ScreenWidth,
	float ScreenHeight)
{
	HybridSsrConstantBuffer data{};

	const Math::Vector4 rtSize = {
		ScreenWidth, ScreenHeight,
		1.0f / ScreenWidth, 1.0f / ScreenHeight
	};
	CopyVectorToFloat4(data.RenderTargetSize, rtSize);

	Math::Matrix4 v = Camera.GetViewMatrix();
	CopyMatrixToFloat4x4(data.View, v);

	Math::Matrix4 p = Camera.GetProjMatrix();
	CopyMatrixToFloat4x4(data.Projection, p);

	Math::Matrix4 invP = Math::Invert(p);
	CopyMatrixToFloat4x4(data.InvProjection, invP);

	data.NearPlaneZ = Camera.GetNearClip();
	data.FarPlaneZ = Camera.GetFarClip();

	data.SSRScale = g_SsrScale;
	data.ZThickness = g_ZThickness;
	data.Stride = g_Stride;
	data.MaxSteps = g_MaxSteps;
	data.MaxDistance = g_MaxDistance;
	data.StrideZCutoff = g_StrideZCutoff;

	Ctx.WriteBuffer(
		g_ConstantBuffer, 0,
		&data,
		sizeof(data));
}

void CopyVectorToFloat3(float3 &Dst, Math::Vector3 Src)
{
	XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(&Dst), Src);
}

void CopyVectorToFloat4(float4 &Dst, Math::Vector4 Src)
{
	XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(&Dst), Src);
}

void CopyMatrixToFloat4x4(float4x4 &Dst, Math::Matrix4 &Src)
{
	XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(Dst.mat), Src);
}
}
