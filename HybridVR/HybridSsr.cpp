//
// Author: Daniel Gaard Hansen (danh@itu.dk)
//
#include "HybridSsr.h"

// Engine
#include "Camera.h"
#include "CommandContext.h"
#include "Raytracing.h"

// Shaders
//#include <CompiledShaders/HybridSsrCS.h>
#include "Shaders/HybridSsrCSCompat.h"

namespace HybridSsr
{
static Texture g_Textures[TextureTypes_kCount];
static HybridSsrConstantBuffer g_ConstantBufferData;
static StructuredBuffer g_ConstantBuffer;
static ComputePSO g_PSO;
static RootSignature g_RS;

void CopyVectorToFloat3(float3 &Dst, Math::Vector3 Src);
void CopyVectorToFloat4(float4 &Dst, Math::Vector4 Src);
void CopyMatrixToFloat4x4(float4x4 &Dst, Math::Matrix4 &Src);

HybridSsrConstantBuffer CreateCBConstants(
	float NearPlaneZ,
	float FarPlaneZ,
	float ZThickness,
	float Stride,
	float MaxSteps,
	float MaxDistance,
	float StrideZCutoff,
	float ReflectionsMode,
	float SSRScale);

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
	g_RS.Reset(RootParam_kCount, 0);
	g_RS[RootParam_kConstants].InitAsConstantBuffer(RootParam_kConstants);
	g_RS[RootParam_kTextures].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, TextureTypes_kCount);
	g_RS[RootParam_kRenderTarget].InitAsBufferUAV(RootParam_kRenderTarget);
	g_RS.Finalize(L"HybridSsr Root Signature");

	// Constant buffer
	g_ConstantBuffer.Create(L"HybridSsr Constant Buffer", 1, sizeof(HybridSsrConstantBuffer));

	// Defaults
	float zThickness = 1.0;
	float stride = 1.0;
	float maxSteps = 1.0;
	float maxDistance = 1.0;
	float strideZCutoff = 1.0;
	float reflectionsMode = 1.0;
	float ssrScale = 1.0;

	g_ConstantBufferData = CreateCBConstants(
		NearPlaneZ,
		FarPlaneZ,
		zThickness,
		stride,
		maxSteps,
		maxDistance,
		strideZCutoff,
		reflectionsMode,
		ssrScale
	);

	g_PSO.SetRootSignature(g_RS);
	//g_PSO.SetComputeShader(g_pHybridSsrCS, sizeof(g_pHybridSsrCS));
	g_PSO.Finalize();
}

HybridSsrConstantBuffer CreateCBConstants(
	float NearPlaneZ,
	float FarPlaneZ,
	float ZThickness,
	float Stride,
	float MaxSteps,
	float MaxDistance,
	float StrideZCutoff,
	float ReflectionsMode,
	float SSRScale)
{
	HybridSsrConstantBuffer result{};

	result.SSRScale = SSRScale;
	result.ZThickness = ZThickness;
	result.NearPlaneZ = NearPlaneZ;
	result.FarPlaneZ = FarPlaneZ;
	result.Stride = Stride;
	result.MaxDistance = MaxDistance;
	result.MaxSteps = MaxSteps;
	result.StrideZCutoff = StrideZCutoff;
	result.ReflectionsMode = ReflectionsMode;

	return result;
}

void ComputeHybridSsr(
	Math::Camera &Camera,
	const float ScreenWidth, const float ScreenHeight)
{
	// Setup context
	ComputeContext &Ctx = ComputeContext::Begin(L"Hybrid SSR");
	ScopedTimer profiler(L"HybridSSR", Ctx);

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
	                 static_cast<float>(ScreenWidth),
	                 static_cast<float>(ScreenHeight));

	Ctx.WriteBuffer(g_ConstantBuffer, 0,
	                &g_ConstantBufferData,
	                sizeof(HybridSsrConstantBuffer));

	// Setup pipeline
	Ctx.SetRootSignature(g_RS);
	Ctx.SetPipelineState(g_PSO);
	Ctx.SetConstantBuffer(RootParam_kConstants,
	                      g_ConstantBuffer.GetGpuVirtualAddress());

	// todo(Danh) 13:20 06/07: Find values for this
	Ctx.Dispatch(1, 1, 0);

	Ctx.Finish();
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
