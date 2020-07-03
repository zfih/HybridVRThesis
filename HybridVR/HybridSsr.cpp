//
// Author: Daniel Gaard Hansen (danh@itu.dk)
//
#include "HybridSsr.h"

// Engine
#include "CommandContext.h"

// Shaders
#include <CompiledShaders/HybridSsrCS.h>

namespace HybridSsr
{
Texture g_Textures[TextureTypes_kCount];
HybridSsrConstantBuffer g_ConstantBufferData;
StructuredBuffer g_ConstantBuffer;
ComputePSO g_PSO;
 RootSignature g_RS;

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
	Math::Vector4 RTSize);

void InitializeResources(
	float NearPlaneZ,
	float FarPlaneZ)
{
	// Root signature
	g_RS.Reset(RootParam_kCount, 0);
	g_RS[RootParam_kConstants].InitAsConstantBuffer(RootParam_kConstants);
	g_RS[RootParam_kTextures].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, TextureTypes_kCount);
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
}

template <typename T>
void CopyToFloatPtr(T &From, float *To, int Size = sizeof(T))
{
	memcpy(&To, &From, Size);
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
	Math::Vector4 RTSize)
{
	HybridSsrConstantBuffer result{};

	CopyToFloatPtr(RTSize, &result.RTSize.x);

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
	Math::Matrix4 &ViewProjection,
	Math::Matrix4 &InvViewProjection,
	Math::Matrix4 &Projection,
	Math::Matrix4 &InvProjection,
	Math::Matrix4 &View,
	Math::Matrix4 &InvView,
	Math::Vector3 &CameraPos,
	Math::Vector4 &LightDirection)
{
	CopyToFloatPtr(ViewProjection, Data.ViewProjection.mat);
	CopyToFloatPtr(InvViewProjection, Data.InvViewProjection.mat);
	CopyToFloatPtr(Projection, Data.Projection.mat);
	CopyToFloatPtr(InvProjection, Data.InvProjection.mat);
	CopyToFloatPtr(View, Data.View.mat);
	CopyToFloatPtr(InvView, Data.InvView.mat);
	CopyToFloatPtr(CameraPos, &Data.CameraPos.x);

	// todo(Danh) 13:34 03/07: This might not copy correct bytes, because SIMD
	CopyToFloatPtr(LightDirection, &Data.LightDirection.x, sizeof(float3));

	Ctx.WriteBuffer(Buffer, 0, &Data, sizeof(HybridSsrConstantBuffer));
}
}
