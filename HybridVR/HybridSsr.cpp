//
// Author: Daniel Gaard Hansen (danh@itu.dk)
//
#include "HybridSsr.h"

// Engine
#include "CommandContext.h"
#include "Camera.h"
#include "CameraType.h"

// Shaders
#include <CompiledShaders/HybridSsrCS.h>

namespace HybridSsr
{
static Texture g_Textures[TextureTypes_kCount];
static HybridSsrConstantBuffer g_ConstantBufferData;
static StructuredBuffer g_ConstantBuffer;
static ComputePSO g_PSO;
static RootSignature g_RS;

HybridSsrConstantBuffer CreateConstantBufferData(
	float NearPlaneZ,
	float FarPlaneZ,
	float ZThickness,
	float Stride,
	float MaxSteps,
	float MaxDistance,
	float StrideZCutoff,
	float ReflectionsMode,
	float SSRScale,
	Math::Vector4 &RTSize);

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
	Math::Vector4 rtSize = {};

	g_ConstantBufferData = CreateConstantBufferData(
		NearPlaneZ,
		FarPlaneZ,
		zThickness,
		stride,
		maxSteps,
		maxDistance,
		strideZCutoff,
		reflectionsMode,
		ssrScale,
		rtSize
	);

	g_PSO.SetRootSignature(g_RS);
	g_PSO.SetComputeShader(g_pHybridSsrCS, sizeof(g_pHybridSsrCS));
	g_PSO.Finalize();
}


void CopyVectorToFloat3(float3 &dst, Math::Vector3 src)
{
	memcpy(&dst, &src, sizeof(float3));
}

void CopyVectorToFloat4(float4 &dst, Math::Vector4 src)
{
	memcpy(&dst, &src, sizeof(float4));
}

void CopyMatrixToFloat4x4(float4x4 &dst, Math::Matrix4& src)
{
	XMStoreFloat4x4(reinterpret_cast<XMFLOAT4X4*>(dst.mat), src);
}

HybridSsrConstantBuffer CreateConstantBufferData(
	float NearPlaneZ,
	float FarPlaneZ,
	float ZThickness,
	float Stride,
	float MaxSteps,
	float MaxDistance,
	float StrideZCutoff,
	float ReflectionsMode,
	float SSRScale,
	Math::Vector4 &RTSize)
{
	HybridSsrConstantBuffer result{};

	CopyVectorToFloat4(result.RTSize, RTSize);

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

void UpdateAndUploadConstantBufferData(
	ComputeContext &Ctx,
	StructuredBuffer &Buffer,
	HybridSsrConstantBuffer &Data,
	Math::Matrix4 &View,
	Math::Matrix4 &InvView,
	Math::Matrix4 &Projection,
	Math::Matrix4 &InvProjection,
	Math::Matrix4 &ViewProjection,
	Math::Matrix4 &InvViewProjection,
	Math::Vector3 &CameraPos)
{
	CopyMatrixToFloat4x4(Data.View, View);
	CopyMatrixToFloat4x4(Data.InvView, InvView);
	CopyMatrixToFloat4x4(Data.Projection, Projection);
	CopyMatrixToFloat4x4(Data.InvProjection, InvProjection);
	CopyMatrixToFloat4x4(Data.ViewProjection, ViewProjection);
	CopyMatrixToFloat4x4(Data.InvViewProjection, InvViewProjection);

	CopyVectorToFloat3(Data.CameraPos, CameraPos);

	Ctx.WriteBuffer(Buffer, 0, &Data, sizeof(HybridSsrConstantBuffer));
}

void ComputeHybridSsr(
	Math::Camera &Camera)
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

	UpdateAndUploadConstantBufferData(
		Ctx,
		g_ConstantBuffer,
		g_ConstantBufferData,
		v,
		invV,
		p,
		invP,
		vp,
		invVp,
		position);

	// Setup pipeline
	Ctx.SetRootSignature(g_RS);
	Ctx.SetPipelineState(g_PSO);
	Ctx.SetConstantBuffer(
		RootParam_kConstants, g_ConstantBuffer.GetGpuVirtualAddress());

	// Use compute shader.
	// todo(Danh) 13:20 06/07: Find values for this
	Ctx.Dispatch(1, 1, 0);
	Ctx.Finish();
}
}
