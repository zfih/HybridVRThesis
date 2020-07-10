//
// Author: Daniel Gaard Hansen (danh@itu.dk)
//
#include "HybridSsr.h"

// Engine
#include "Camera.h"
#include "PipelineState.h"
#include "RootSignature.h"
#include "ColorBuffer.h"
#include "DepthBuffer.h"
#include "TextureManager.h"
#include "CommandContext.h"

// Shaders
#include "Shaders/HybridSSRCSCompat.h"
#include <CompiledShaders/HybridSsrCS.h>

namespace HybridSsr
{
static HybridSsrConstantBuffer g_ConstantBufferData;
static StructuredBuffer g_ConstantBuffer;
static ComputePSO g_PSO;
static RootSignature g_RS;

void CopyVectorToFloat3(float3 &Dst, Math::Vector3 Src);
void CopyVectorToFloat4(float4 &Dst, Math::Vector4 Src);
void CopyMatrixToFloat4x4(float4x4 &Dst, Math::Matrix4 &Src);

HybridSsrConstantBuffer CreateCBConstants(
	float NearPlaneZ,
	float FarPlaneZ);

void CreateCBDynamics(
	HybridSsrConstantBuffer &Data,
	Math::Matrix4 &View,
	Math::Matrix4 &InvView,
	Math::Matrix4 &Projection,
	Math::Matrix4 &InvProjection,
	Math::Matrix4 &ViewProjection,
	Math::Matrix4 &InvViewProjection,
	Math::Vector3 &CameraPos,
	float ScreenWidth,
	float ScreenHeight);

void InitializeResources(
	float NearPlaneZ,
	float FarPlaneZ)
{
	// Root signature
	g_RS.Reset((int)RootParam::kCount, 0);
	g_RS[(int)RootParam::kConstants].InitAsConstantBuffer(
		(int)RootParam::kConstants);
	g_RS[(int)RootParam::kTextures].InitAsDescriptorRange(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, (int)TextureType::kCount);
	g_RS[(int)RootParam::kRenderTarget].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
	g_RS.Finalize(L"HybridSsr Root Signature");
	
	g_ConstantBufferData = CreateCBConstants(
		NearPlaneZ,
		FarPlaneZ);
	
	// Create PSO
	g_PSO.SetRootSignature(g_RS);
	g_PSO.SetComputeShader(g_pHybridSsrCS, sizeof(g_pHybridSsrCS));
	g_PSO.Finalize();
}

HybridSsrConstantBuffer CreateCBConstants(
	float NearPlaneZ,
	float FarPlaneZ)
{
	g_ConstantBuffer.Create(L"HybridSsr Constant Buffer", 1, sizeof(HybridSsrConstantBuffer));

	HybridSsrConstantBuffer result{};

	// Defaults -
	// Copied from FeaxRenderer: FeaxRenderer.cpp around lines 2000.
	result.SSRScale = 1;
	result.ZThickness = 0.05;
	result.Stride = 1;
	result.MaxDistance = 200;
	result.MaxSteps = 400;
	result.StrideZCutoff = 0;
	result.ReflectionsMode = 0;
	result.NearPlaneZ = NearPlaneZ;
	result.FarPlaneZ = FarPlaneZ;

	return result;
}

void ComputeHybridSsr(
	GraphicsContext &GraphicsCtx,
	Math::Camera &Camera,
	Cam::CameraType CamType,
	ColorBuffer &Color, DepthBuffer &Depth, ColorBuffer &Normal)
{
	// Setup context
	ComputeContext& ctx = GraphicsCtx.GetComputeContext();
	ScopedTimer timer(L"HybridSSR", ctx);

	int width = Color.GetWidth();
	int height = Color.GetHeight();
	
	// Setup resources
	Math::Matrix4 v = Camera.GetViewMatrix();
	Math::Matrix4 invV = Math::Invert(v);

	Math::Matrix4 p = Camera.GetProjMatrix();
	Math::Matrix4 invP = Math::Invert(p);

	Math::Matrix4 vp = Camera.GetViewProjMatrix();
	Math::Matrix4 invVp = Math::Invert(vp);

	Math::Vector3 position = Camera.GetPosition();

	CreateCBDynamics(g_ConstantBufferData,
	                 v,
	                 invV,
	                 p,
	                 invP,
	                 vp,
	                 invVp,
	                 position,
	                 static_cast<float>(width),
	                 static_cast<float>(height));

	ctx.WriteBuffer(g_ConstantBuffer, 0,
	                &g_ConstantBufferData,
	                sizeof(HybridSsrConstantBuffer));


	// Transition resources
	ctx.TransitionResource(Color, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(Depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(Normal, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	// Setup pipeline
	ctx.SetRootSignature(g_RS);
	ctx.SetPipelineState(g_PSO);
	ctx.SetConstantBuffer(
		(int)RootParam::kConstants,
		g_ConstantBuffer.GetGpuVirtualAddress());
	ctx.SetDynamicDescriptor(
		(int)RootParam::kTextures,
		(int)TextureType::kAlbedoBuffer, Color.GetSubSRV(CamType));
	ctx.SetDynamicDescriptor(
		(int)RootParam::kTextures,
		(int)TextureType::kDepthBuffer, Depth.GetSubSRV(CamType));
	ctx.SetDynamicDescriptor(
		(int)RootParam::kTextures,
		(int)TextureType::kNormalBuffer, Normal.GetSubSRV(CamType));
	ctx.SetDynamicDescriptor(
		(int)RootParam::kTextures,
		(int)TextureType::kMainBuffer, Color.GetSubSRV(CamType));
	ctx.SetDynamicDescriptor(
		(int)RootParam::kTextures,
		(int)TextureType::kMainBuffer, Color.GetSubSRV(CamType));
	ctx.SetDynamicDescriptor(
		(int)RootParam::kRenderTarget, 0, Color.GetSubUAV(CamType));

	const int numThreadsX = width / 8 + 1;
	const int numThreadsY = height / 8 + 1;
	ctx.Dispatch(numThreadsX, numThreadsY, 1);

	ctx.TransitionResource(Color, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

void CreateCBDynamics(
	HybridSsrConstantBuffer &Data,
	Math::Matrix4 &View,
	Math::Matrix4 &InvView,
	Math::Matrix4 &Projection,
	Math::Matrix4 &InvProjection,
	Math::Matrix4 &ViewProjection,
	Math::Matrix4 &InvViewProjection,
	Math::Vector3 &CameraPos,
	float ScreenWidth,
	float ScreenHeight)
{
	CopyMatrixToFloat4x4(Data.View, View);
	CopyMatrixToFloat4x4(Data.InvView, InvView);
	CopyMatrixToFloat4x4(Data.Projection, Projection);
	CopyMatrixToFloat4x4(Data.InvProjection, InvProjection);
	CopyMatrixToFloat4x4(Data.ViewProjection, ViewProjection);
	CopyMatrixToFloat4x4(Data.InvViewProjection, InvViewProjection);

	const Math::Vector4 rtSize = {
		ScreenWidth, ScreenHeight,
		1.0f / ScreenWidth, 1.0f / ScreenHeight
	};
	CopyVectorToFloat4(Data.RTSize, rtSize);

	CopyVectorToFloat3(Data.CameraPos, CameraPos);
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
