//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#define NOMINMAX

#include "stdafx.h"
#include "d3d12.h"
#include "d3d12video.h"
#include <d3d12.h>
#include "dxgi1_3.h"
#include "GameCore.h"
#include "GraphicsCore.h"
#include "CameraController.h"
#include "BufferManager.h"
#include "Camera.h"
#include "Model.h"
#include "GpuBuffer.h"
#include "CommandContext.h"
#include "SamplerManager.h"
#include "TemporalEffects.h"
#include "MotionBlur.h"
#include "DepthOfField.h"
#include "PostEffects.h"
#include "SSAO.h"
#include "FXAA.h"
#include "SystemTime.h"
#include "TextRenderer.h"
#include "ShadowCamera.h"
#include "ParticleEffectManager.h"
#include "GameInput.h"
#include "./ForwardPlusLighting.h"
#include <atlbase.h>
#include <atlbase.h>
#include "DXSampleHelper.h"

#include "CompiledShaders/DepthViewerVS.h"
#include "CompiledShaders/DepthViewerPS.h"
#include "CompiledShaders/ModelViewerVS.h"
#include "CompiledShaders/ModelViewerPS.h"
#include "CompiledShaders/WaveTileCountPS.h"
#include "CompiledShaders/RayGenerationShaderLib.h"
#include "CompiledShaders/RayGenerationShaderSSRLib.h"
#include "CompiledShaders/HitShaderLib.h"
#include "CompiledShaders/MissShaderLib.h"
#include "CompiledShaders/DiffuseHitShaderLib.h"
#include "CompiledShaders/RayGenerationShadowsLib.h"
#include "CompiledShaders/MissShadowsLib.h"
#include "CompiledShaders/AlphaTransparencyAnyHit.h"

#include "CompiledShaders/CombineDepthsCS.h"
#include "CompiledShaders/CombineColourVS.h"
#include "CompiledShaders/CombineColourPS.h"

// Screen texture
// Albert
#include "CompiledShaders/ScreenTextureVS.h"
#include "CompiledShaders/ScreenTexturePS.h"


#include "RaytracingHlslCompat.h"
#include "ModelViewerRayTracing.h"

using namespace GameCore;
using namespace Math;
using namespace Graphics;

// Albert
namespace RS_ScreenTexture
{
enum ScreenTextureRS
{
	kTextureToRender = 0,
	kCount
};
};


extern ByteAddressBuffer g_bvh_bottomLevelAccelerationStructure;
ColorBuffer g_SceneNormalBuffer;

CComPtr<ID3D12Device5> g_pRaytracingDevice;

__declspec(align(16)) struct HitShaderConstants
{
	Vector3 sunDirection;
	Vector3 sunLight;
	Vector3 ambientLight;
	float ShadowTexelSize[4];
	Matrix4 modelToShadow;
	UINT32 IsReflection;
	UINT32 UseShadowRays;
};

ByteAddressBuffer g_hitConstantBuffer;
ByteAddressBuffer g_dynamicConstantBuffer;

D3D12_GPU_DESCRIPTOR_HANDLE g_GpuSceneMaterialSrvs[27];
D3D12_CPU_DESCRIPTOR_HANDLE g_SceneMeshInfo;
D3D12_CPU_DESCRIPTOR_HANDLE g_SceneIndices;

D3D12_GPU_DESCRIPTOR_HANDLE g_OutputUAV;
D3D12_GPU_DESCRIPTOR_HANDLE g_DepthAndNormalsTable;
D3D12_GPU_DESCRIPTOR_HANDLE g_SceneSrvs;

std::vector<CComPtr<ID3D12Resource>> g_bvh_bottomLevelAccelerationStructures;
CComPtr<ID3D12Resource> g_bvh_topLevelAccelerationStructure;

DynamicCB g_dynamicCb;
CComPtr<ID3D12RootSignature> g_GlobalRaytracingRootSignature;
CComPtr<ID3D12RootSignature> g_LocalRaytracingRootSignature;

enum RaytracingTypes
{
	Primarybarycentric = 0,
	Reflectionbarycentric,
	Shadows,
	DiffuseHitShader,
	Reflection,
	NumTypes
};

const static UINT MaxRayRecursion = 2;

const static UINT c_NumCameraPositions = 5;

struct RaytracingDispatchRayInputs
{
	RaytracingDispatchRayInputs()
	{
	}

	RaytracingDispatchRayInputs(
		ID3D12Device5& device,
		ID3D12StateObject* pPSO,
		void* pHitGroupShaderTable,
		UINT HitGroupStride,
		UINT HitGroupTableSize,
		LPCWSTR rayGenExportName,
		LPCWSTR missExportName) : m_pPSO(pPSO)
	{
		const UINT shaderTableSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		ID3D12StateObjectProperties* stateObjectProperties = nullptr;
		ThrowIfFailed(pPSO->QueryInterface(IID_PPV_ARGS(&stateObjectProperties)));
		void* pRayGenShaderData = stateObjectProperties->GetShaderIdentifier(rayGenExportName);
		void* pMissShaderData = stateObjectProperties->GetShaderIdentifier(missExportName);

		m_HitGroupStride = HitGroupStride * 2;

		// MiniEngine requires that all initial data be aligned to 16 bytes
		UINT alignment = 16;
		std::vector<BYTE> alignedShaderTableData(shaderTableSize + alignment - 1);
		BYTE* pAlignedShaderTableData = alignedShaderTableData.data() + ((UINT64)alignedShaderTableData.data() %
			alignment);
		memcpy(pAlignedShaderTableData, pRayGenShaderData, shaderTableSize);
		m_RayGenShaderTable.Create(L"Ray Gen Shader Table", 1, shaderTableSize, alignedShaderTableData.data());

		memcpy(pAlignedShaderTableData, pMissShaderData, shaderTableSize);
		m_MissShaderTable.Create(L"Miss Shader Table", 1, shaderTableSize, alignedShaderTableData.data());

		m_HitShaderTable.Create(L"Hit Shader Table", 1, HitGroupTableSize, pHitGroupShaderTable);
	}

	D3D12_DISPATCH_RAYS_DESC GetDispatchRayDesc(UINT DispatchWidth, UINT DispatchHeight)
	{
		D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = {};

		dispatchRaysDesc.RayGenerationShaderRecord.StartAddress = m_RayGenShaderTable.GetGpuVirtualAddress();
		dispatchRaysDesc.RayGenerationShaderRecord.SizeInBytes = m_RayGenShaderTable.GetBufferSize();
		dispatchRaysDesc.HitGroupTable.StartAddress = m_HitShaderTable.GetGpuVirtualAddress();
		dispatchRaysDesc.HitGroupTable.SizeInBytes = m_HitShaderTable.GetBufferSize();
		dispatchRaysDesc.HitGroupTable.StrideInBytes = m_HitGroupStride;
		dispatchRaysDesc.MissShaderTable.StartAddress = m_MissShaderTable.GetGpuVirtualAddress();
		dispatchRaysDesc.MissShaderTable.SizeInBytes = m_MissShaderTable.GetBufferSize();
		dispatchRaysDesc.MissShaderTable.StrideInBytes = dispatchRaysDesc.MissShaderTable.SizeInBytes; // Only one entry
		dispatchRaysDesc.Width = DispatchWidth;
		dispatchRaysDesc.Height = DispatchHeight;
		dispatchRaysDesc.Depth = 1;
		return dispatchRaysDesc;
	}

	UINT m_HitGroupStride;
	CComPtr<ID3D12StateObject> m_pPSO;
	ByteAddressBuffer m_RayGenShaderTable;
	ByteAddressBuffer m_MissShaderTable;
	ByteAddressBuffer m_HitShaderTable;
};

struct MaterialRootConstant
{
	UINT MaterialID;
};

RaytracingDispatchRayInputs g_RaytracingInputs[RaytracingTypes::NumTypes];
D3D12_CPU_DESCRIPTOR_HANDLE g_bvh_attributeSrvs[34];
bool g_RayTraceSupport = false;

class D3D12RaytracingMiniEngineSample : public GameCore::IGameApp
{
public:

	D3D12RaytracingMiniEngineSample(bool validDeviceFound)
	{
		g_RayTraceSupport = validDeviceFound;
	}

	virtual void Startup(void) override;
	virtual void Cleanup(void) override;

	virtual void Update(float deltaT) override;
	virtual void RenderScene() override;
	virtual void RenderUI(class GraphicsContext&) override;
	virtual void Raytrace(class GraphicsContext&, UINT cam,
	                      DepthBuffer* curDepthBuf);

	void SetCameraToPredefinedPosition(int cameraPosition);

	void CreateQuadVerts();

private:

	void CreateRayTraceAccelerationStructures(UINT numMeshes);

	void RenderLightShadows(GraphicsContext& gfxContext, UINT curCam);

	enum eObjectFilter { kOpaque = 0x1, kCutout = 0x2, kTransparent = 0x4, kAll = 0xF, kNone = 0x0 };
	void RenderObjects(GraphicsContext& Context, const Matrix4& ViewProjMat, UINT curCam, eObjectFilter Filter = kAll);
	void RenderCenterViewToEye(GraphicsContext& Ctx, UINT cam);
	void RaytraceDiffuse(GraphicsContext& context, const Math::Camera& camera, ColorBuffer& colorTarget);
	void RaytraceShadows(GraphicsContext& context, const Math::Camera& camera, ColorBuffer& colorTarget,
	                     DepthBuffer& depth);
	void RaytraceReflections(GraphicsContext& context, const Math::Camera& camera, ColorBuffer& colorTarget,
	                         DepthBuffer& depth, ColorBuffer& normals);
	// Albert
	struct ScreenTextureData
	{
		// Screen Texture quad buffers
		StructuredBuffer m_Buffer;

		// Left and right eye quads
		QuadPos m_Quad;

		// Render to Quad PSO
		GraphicsPSO m_PSO;

		RootSignature m_RootSignature;
	} m_screenTextureData;


	VRCamera m_Camera;
	std::auto_ptr<VRCameraController> m_CameraController;
	//Matrix4 m_ViewProjMatrix;
	D3D12_VIEWPORT m_MainViewport;
	D3D12_RECT m_MainScissor;


	RootSignature m_RootSig;
	GraphicsPSO m_DepthPSO;
	GraphicsPSO m_CutoutDepthPSO;
	GraphicsPSO m_ModelPSO;
	GraphicsPSO m_CutoutModelPSO;
	GraphicsPSO m_ShadowPSO;
	GraphicsPSO m_CutoutShadowPSO;
	GraphicsPSO m_WaveTileCountPSO;

	RootSignature m_ComputeRootSig;
	ComputePSO m_CombineDepthPSO;
	RootSignature m_CombineColourSig;
	GraphicsPSO m_CombineColourPSO;

	D3D12_CPU_DESCRIPTOR_HANDLE m_DefaultSampler;
	D3D12_CPU_DESCRIPTOR_HANDLE m_ShadowSampler;
	D3D12_CPU_DESCRIPTOR_HANDLE m_BiasedDefaultSampler;

	D3D12_CPU_DESCRIPTOR_HANDLE m_ExtraTextures[6];

	// Scene to render
	Model m_Model;

	std::vector<bool> m_pMaterialIsCutout;
	std::vector<bool> m_pMaterialIsReflective;

	Vector3 m_SunDirection;
	ShadowCamera m_SunShadow;

	struct CameraPosition
	{
		Vector3 position;
		float heading;
		float pitch;
	};

	CameraPosition m_CameraPosArray[c_NumCameraPositions];
	UINT m_CameraPosArrayCurrentPosition;
};


// Returns bool whether the device supports DirectX Raytracing tier.
inline bool IsDirectXRaytracingSupported(IDXGIAdapter1* adapter)
{
	ComPtr<ID3D12Device> testDevice;
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 featureSupportData = {};

	return SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&testDevice)))
		&& SUCCEEDED(
			testDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &featureSupportData, sizeof(featureSupportData
			)))
		&& featureSupportData.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
}

int wmain(int argc, wchar_t** argv)
{
#if _DEBUG
	CComPtr<ID3D12Debug> debugInterface;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface))))
	{
		debugInterface->EnableDebugLayer();
	}
#endif

	CComPtr<ID3D12Device> pDevice;
	CComPtr<IDXGIAdapter1> pAdapter;
	CComPtr<IDXGIFactory2> pFactory;
	CreateDXGIFactory2(0, IID_PPV_ARGS(&pFactory));

	bool validDeviceFound = false;
	for (uint32_t Idx = 0; !validDeviceFound && DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(Idx, &pAdapter); ++Idx)
	{
		DXGI_ADAPTER_DESC1 desc;
		pAdapter->GetDesc1(&desc);

		validDeviceFound = IsDirectXRaytracingSupported(pAdapter);

		pAdapter = nullptr;
	}

	s_EnableVSync.Decrement();
	g_DisplayWidth = 1280;
	g_DisplayHeight = 720;
	GameCore::RunApplication(D3D12RaytracingMiniEngineSample(validDeviceFound), L"D3D12RaytracingMiniEngineSample");
	return 0;
}

ExpVar m_SunLightIntensity("Application/Lighting/Sun Light Intensity", 4.0f, 0.0f, 16.0f, 0.1f);
ExpVar m_AmbientIntensity("Application/Lighting/Ambient Intensity", 0.1f, -16.0f, 16.0f, 0.1f);
NumVar m_SunOrientation("Application/Lighting/Sun Orientation", -0.5f, -100.0f, 100.0f, 0.1f);
NumVar m_SunInclination("Application/Lighting/Sun Inclination", 0.75f, 0.0f, 1.0f, 0.01f);
NumVar ShadowDimX("Application/Lighting/Shadow Dim X", 5000, 1000, 10000, 100);
NumVar ShadowDimY("Application/Lighting/Shadow Dim Y", 3000, 1000, 10000, 100);
NumVar ShadowDimZ("Application/Lighting/Shadow Dim Z", 3000, 1000, 10000, 100);

IntVar m_TestValueSuperDuper("Test/Test/Shadow Dim Z", 5, 0, 10, 1);


BoolVar ShowWaveTileCounts("Application/Forward+/Show Wave Tile Counts", false);

const char* rayTracingModes[] = {
	"Off",
	"Bary Rays",
	"Refl Bary",
	"Shadow Rays",
	"Diffuse&ShadowMaps",
	"Diffuse&ShadowRays",
	"Reflection Rays"
};
enum RaytracingMode
{
	RTM_OFF,
	RTM_TRAVERSAL,
	RTM_SSR,
	RTM_SHADOWS,
	RTM_DIFFUSE_WITH_SHADOWMAPS,
	RTM_DIFFUSE_WITH_SHADOWRAYS,
	RTM_REFLECTIONS,
};

EnumVar rayTracingMode("Application/Raytracing/RayTraceMode", RTM_DIFFUSE_WITH_SHADOWMAPS, _countof(rayTracingModes),
                       rayTracingModes);

class DescriptorHeapStack
{
public:
	DescriptorHeapStack(ID3D12Device& device, UINT numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT NodeMask) :
		m_device(device)
	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.NumDescriptors = numDescriptors;
		desc.Type = type;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		desc.NodeMask = NodeMask;
		device.CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_pDescriptorHeap));

		m_descriptorSize = device.GetDescriptorHandleIncrementSize(type);
		m_descriptorHeapCpuBase = m_pDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
	}

	ID3D12DescriptorHeap& GetDescriptorHeap() { return *m_pDescriptorHeap; }

	void AllocateDescriptor(_Out_ D3D12_CPU_DESCRIPTOR_HANDLE& cpuHandle, _Out_ UINT& descriptorHeapIndex)
	{
		descriptorHeapIndex = m_descriptorsAllocated;
		cpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_descriptorHeapCpuBase, descriptorHeapIndex, m_descriptorSize);
		m_descriptorsAllocated++;
	}

	UINT AllocateBufferSrv(_In_ ID3D12Resource& resource)
	{
		UINT descriptorHeapIndex;
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
		AllocateDescriptor(cpuHandle, descriptorHeapIndex);
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.NumElements = (UINT)(resource.GetDesc().Width / sizeof(UINT32));
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
		srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		m_device.CreateShaderResourceView(&resource, &srvDesc, cpuHandle);
		return descriptorHeapIndex;
	}

	UINT AllocateBufferUav(_In_ ID3D12Resource& resource)
	{
		UINT descriptorHeapIndex;
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
		AllocateDescriptor(cpuHandle, descriptorHeapIndex);
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uavDesc.Buffer.NumElements = (UINT)(resource.GetDesc().Width / sizeof(UINT32));
		uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
		uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;

		m_device.CreateUnorderedAccessView(&resource, nullptr, &uavDesc, cpuHandle);
		return descriptorHeapIndex;
	}

	D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(UINT descriptorIndex)
	{
		return CD3DX12_GPU_DESCRIPTOR_HANDLE(m_pDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex,
		                                     m_descriptorSize);
	}

private:
	ID3D12Device& m_device;
	CComPtr<ID3D12DescriptorHeap> m_pDescriptorHeap;
	UINT m_descriptorsAllocated = 0;
	UINT m_descriptorSize;
	D3D12_CPU_DESCRIPTOR_HANDLE m_descriptorHeapCpuBase;
};

std::unique_ptr<DescriptorHeapStack> g_pRaytracingDescriptorHeap;

StructuredBuffer g_hitShaderMeshInfoBuffer;

static
void InitializeSceneInfo(
	const Model& model)
{
	//
	// Mesh info
	//
	std::vector<RayTraceMeshInfo> meshInfoData(model.m_Header.meshCount);
	for (UINT i = 0; i < model.m_Header.meshCount; ++i)
	{
		meshInfoData[i].m_indexOffsetBytes = model.m_pMesh[i].indexDataByteOffset;
		meshInfoData[i].m_uvAttributeOffsetBytes = model.m_pMesh[i].vertexDataByteOffset + model.m_pMesh[i].attrib[Model
			::attrib_texcoord0].offset;
		meshInfoData[i].m_normalAttributeOffsetBytes = model.m_pMesh[i].vertexDataByteOffset + model.m_pMesh[i].attrib[
			Model::attrib_normal].offset;
		meshInfoData[i].m_positionAttributeOffsetBytes = model.m_pMesh[i].vertexDataByteOffset + model.m_pMesh[i].attrib
			[Model::attrib_position].offset;
		meshInfoData[i].m_tangentAttributeOffsetBytes = model.m_pMesh[i].vertexDataByteOffset + model.m_pMesh[i].attrib[
			Model::attrib_tangent].offset;
		meshInfoData[i].m_bitangentAttributeOffsetBytes = model.m_pMesh[i].vertexDataByteOffset + model.m_pMesh[i].
			attrib[Model::attrib_bitangent].offset;
		meshInfoData[i].m_attributeStrideBytes = model.m_pMesh[i].vertexStride;
		meshInfoData[i].m_materialInstanceId = model.m_pMesh[i].materialIndex;
		ASSERT(meshInfoData[i].m_materialInstanceId < 27);
	}

	g_hitShaderMeshInfoBuffer.Create(L"RayTraceMeshInfo",
	                                 (UINT)meshInfoData.size(),
	                                 sizeof(meshInfoData[0]),
	                                 meshInfoData.data());

	g_SceneIndices = model.m_IndexBuffer.GetSRV();
	g_SceneMeshInfo = g_hitShaderMeshInfoBuffer.GetSRV();
}

static
void InitializeViews(const Model& model)
{
	D3D12_CPU_DESCRIPTOR_HANDLE uavHandle;
	UINT uavDescriptorIndex;
	g_pRaytracingDescriptorHeap->AllocateDescriptor(uavHandle, uavDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, uavHandle, g_SceneColorBuffer.GetUAV(),
	                                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	g_OutputUAV = g_pRaytracingDescriptorHeap->GetGpuHandle(uavDescriptorIndex);

	{
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
		UINT srvDescriptorIndex;
		g_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);

		// TODO(freemedude 14:51 24-04): Verify that GetDepthSRV does not need to be PER buffer.
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, g_SceneDepthBuffer.GetDepthSRV(),
		                                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		g_DepthAndNormalsTable = g_pRaytracingDescriptorHeap->GetGpuHandle(srvDescriptorIndex);

		UINT unused;
		g_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, unused); // Should this be unused?
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle,
		                                          g_SceneDepthBuffer.GetDepthSRV(),
		                                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		g_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, unused);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, g_SceneNormalBuffer.GetSRV(),
		                                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	{
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
		UINT srvDescriptorIndex;
		g_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, g_SceneMeshInfo,
		                                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		g_SceneSrvs = g_pRaytracingDescriptorHeap->GetGpuHandle(srvDescriptorIndex);

		UINT unused;
		g_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, unused);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, g_SceneIndices, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		g_pRaytracingDescriptorHeap->
			AllocateBufferSrv(*const_cast<ID3D12Resource*>(model.m_VertexBuffer.GetResource()));

		g_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, unused);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, g_ShadowBuffer.GetSRV(),
		                                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		g_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, unused);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, g_SSAOFullScreen.GetSRV(),
		                                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		for (UINT i = 0; i < model.m_Header.materialCount; i++)
		{
			UINT slot;
			g_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, slot);
			Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, *model.GetSRVs(i),
			                                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			g_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, unused);
			Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, model.GetSRVs(i)[3],
			                                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			g_GpuSceneMaterialSrvs[i] = g_pRaytracingDescriptorHeap->GetGpuHandle(slot);
		}
	}
}

D3D12_STATE_SUBOBJECT CreateDxilLibrary(LPCWSTR entrypoint, const void* pShaderByteCode, SIZE_T bytecodeLength,
                                        D3D12_DXIL_LIBRARY_DESC& dxilLibDesc, D3D12_EXPORT_DESC& exportDesc)
{
	exportDesc = {entrypoint, nullptr, D3D12_EXPORT_FLAG_NONE};
	dxilLibDesc.DXILLibrary.pShaderBytecode = pShaderByteCode;
	dxilLibDesc.DXILLibrary.BytecodeLength = bytecodeLength;
	dxilLibDesc.NumExports = 1;
	dxilLibDesc.pExports = &exportDesc;
	D3D12_STATE_SUBOBJECT dxilLibSubObject = {};
	dxilLibSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
	dxilLibSubObject.pDesc = &dxilLibDesc;
	return dxilLibSubObject;
}

void SetPipelineStateStackSize(
	LPCWSTR raygen,
	LPCWSTR closestHit,
	LPCWSTR anyHit,
	LPCWSTR miss,
	UINT maxRecursion,
	ID3D12StateObject* pStateObject)
{
	ID3D12StateObjectProperties* stateObjectProperties = nullptr;
	ThrowIfFailed(pStateObject->QueryInterface(IID_PPV_ARGS(&stateObjectProperties)));
	UINT64 closestHitStackSize = stateObjectProperties->GetShaderStackSize(closestHit);
	UINT64 anyHitStackSize = stateObjectProperties->GetShaderStackSize(anyHit);
	UINT64 missStackSize = stateObjectProperties->GetShaderStackSize(miss);
	UINT64 raygenStackSize = stateObjectProperties->GetShaderStackSize(raygen);

	UINT64 shaderStackSize = std::max(
		{missStackSize, closestHitStackSize, anyHitStackSize}
	);

	UINT64 totalStackSize = raygenStackSize + shaderStackSize * maxRecursion;
	stateObjectProperties->SetPipelineStackSize(totalStackSize);
}

// BOOKMARK JOHN
void InitializeRaytracingStateObjects(const Model& model, UINT numMeshes)
{
	// Initialize subobject list
	// ----------------------------------------------------------------//
	std::vector<D3D12_STATE_SUBOBJECT> subObjects;
	std::vector<LPCWSTR> shadersToAssociate;

	D3D12_STATE_SUBOBJECT nodeMaskSubObject;
	UINT nodeMask = 1;
	nodeMaskSubObject.pDesc = &nodeMask;
	nodeMaskSubObject.Type = D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK;
	subObjects.push_back(nodeMaskSubObject);
	// -----------------//

	// Global Root Signature
	// ----------------------------------------------------------------//
	D3D12_STATE_SUBOBJECT rootSignatureSubObject;
	rootSignatureSubObject.pDesc = &g_GlobalRaytracingRootSignature.p;
	rootSignatureSubObject.Type =
		D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
	subObjects.push_back(rootSignatureSubObject);
	// -----------------//

	// Configuration
	// ----------------------------------------------------------------//
	D3D12_STATE_SUBOBJECT configurationSubObject;
	D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig;
	pipelineConfig.MaxTraceRecursionDepth = MaxRayRecursion;
	configurationSubObject.pDesc = &pipelineConfig;
	configurationSubObject.Type =
		D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
	subObjects.push_back(configurationSubObject);
	// -----------------//


	// Ray Gen shader
	// ----------------------------------------------------------------//
	LPCWSTR rayGenShaderExportName = L"RayGen";
	shadersToAssociate.push_back(rayGenShaderExportName);

	subObjects.push_back(D3D12_STATE_SUBOBJECT{});
	D3D12_STATE_SUBOBJECT& rayGenDxilLibSubobject = subObjects.back();
	D3D12_EXPORT_DESC rayGenExportDesc;
	D3D12_DXIL_LIBRARY_DESC rayGenDxilLibDesc = {};
	rayGenDxilLibSubobject = CreateDxilLibrary(
		rayGenShaderExportName, g_pRayGenerationShaderLib,
		sizeof(g_pRayGenerationShaderLib), rayGenDxilLibDesc, rayGenExportDesc);
	// -----------------//

	// Hit Group shader stuff
	// ----------------------------------------------------------------//
	// Closest hit shader
	LPCWSTR closestHitExportName = L"Hit";
	shadersToAssociate.push_back(closestHitExportName);

	D3D12_EXPORT_DESC closestHitExportDesc;
	D3D12_DXIL_LIBRARY_DESC closestHitDxilLibDesc = {};
	D3D12_STATE_SUBOBJECT closestHitLibSubobject = CreateDxilLibrary(
		closestHitExportName, g_phitShaderLib,
		sizeof(g_phitShaderLib), closestHitDxilLibDesc,
		closestHitExportDesc);
	subObjects.push_back(closestHitLibSubobject);
	// -----------------//

	// Any hit shader
	// -----------------//
	LPCWSTR anyHitExportName = L"AnyHit";
	shadersToAssociate.push_back(anyHitExportName);

	D3D12_EXPORT_DESC anyHitExportDesc;
	D3D12_DXIL_LIBRARY_DESC anyHitDxilLibDesc = {};
	D3D12_STATE_SUBOBJECT anyHitLibSubobject = CreateDxilLibrary(
		anyHitExportName, g_pAlphaTransparencyAnyHit,
		sizeof(g_pAlphaTransparencyAnyHit), anyHitDxilLibDesc,
		anyHitExportDesc);
	subObjects.push_back(anyHitLibSubobject);
	// -----------------//

	// Miss shader
	// -----------------//
	LPCWSTR missExportName = L"Miss";
	shadersToAssociate.push_back(missExportName);

	D3D12_EXPORT_DESC missExportDesc;
	D3D12_DXIL_LIBRARY_DESC missDxilLibDesc = {};
	D3D12_STATE_SUBOBJECT missDxilLibSubobject = CreateDxilLibrary(
		missExportName, g_pmissShaderLib,
		sizeof(g_pmissShaderLib), missDxilLibDesc,
		missExportDesc);

	subObjects.push_back(missDxilLibSubobject);
	// -----------------//

	// Hit Group
	// ----------------------------------------------------------------//
	LPCWSTR hitGroupExportName = L"HitGroup";

	D3D12_HIT_GROUP_DESC hitGroupDesc = {};
	hitGroupDesc.ClosestHitShaderImport = closestHitExportName;
	hitGroupDesc.AnyHitShaderImport = anyHitExportName;
	hitGroupDesc.HitGroupExport = hitGroupExportName;

	D3D12_STATE_SUBOBJECT hitGroupSubobject = {};
	hitGroupSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
	hitGroupSubobject.pDesc = &hitGroupDesc;
	subObjects.push_back(hitGroupSubobject);
	// -----------------//

	// Shader config
	// ----------------------------------------------------------------//
	D3D12_STATE_SUBOBJECT shaderConfigStateObject;

	D3D12_RAYTRACING_SHADER_CONFIG shaderConfig;
	shaderConfig.MaxAttributeSizeInBytes = 8;
	shaderConfig.MaxPayloadSizeInBytes = 8;
	shaderConfigStateObject.pDesc = &shaderConfig;
	shaderConfigStateObject.Type =
		D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
	subObjects.push_back(shaderConfigStateObject);
	// -----------------//

	// Local root signature
	// ----------------------------------------------------------------//
	D3D12_STATE_SUBOBJECT localRootSignatureSubObject;
	localRootSignatureSubObject.pDesc = &g_LocalRaytracingRootSignature.p;
	localRootSignatureSubObject.Type =
		D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
	subObjects.push_back(localRootSignatureSubObject);
	// -----------------//

	// State Object
	// ----------------------------------------------------------------//
	D3D12_STATE_OBJECT_DESC stateObject;
	stateObject.NumSubobjects = (UINT)subObjects.size();
	stateObject.pSubobjects = subObjects.data();
	stateObject.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	// -----------------//

	// Shader table creation
	// ----------------------------------------------------------------//
	const UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

#define ALIGN(alignment, num) ((((num) + alignment - 1) / alignment) * alignment)
	const UINT offsetToDescriptorHandle = ALIGN(
		sizeof(D3D12_GPU_DESCRIPTOR_HANDLE),
		shaderIdentifierSize);
	const UINT offsetToMaterialConstants = ALIGN(
		sizeof(UINT32),
		offsetToDescriptorHandle + sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
	const UINT shaderRecordSizeInBytes = ALIGN(
		D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT,
		offsetToMaterialConstants + sizeof(MaterialRootConstant));
	// -----------------//

	std::vector<byte> pHitShaderTable(shaderRecordSizeInBytes * numMeshes);

	auto GetShaderTable = [=](const Model& model,
	                          ID3D12StateObject* pPSO,
	                          byte* pShaderTable)
	{
		ID3D12StateObjectProperties* stateObjectProperties = nullptr;
		ThrowIfFailed(pPSO->QueryInterface(IID_PPV_ARGS(&stateObjectProperties)));
		void* pHitGroupIdentifierData = stateObjectProperties->
			GetShaderIdentifier(hitGroupExportName);
		for (UINT i = 0; i < numMeshes; i++)
		{
			byte* pShaderRecord = i * shaderRecordSizeInBytes + pShaderTable;
			memcpy(pShaderRecord, pHitGroupIdentifierData, shaderRecordSizeInBytes);

			UINT materialIndex = model.m_pMesh[i].materialIndex;
			memcpy(pShaderRecord + offsetToDescriptorHandle,
			       &g_GpuSceneMaterialSrvs[materialIndex].ptr,
			       sizeof(g_GpuSceneMaterialSrvs[materialIndex].ptr));

			MaterialRootConstant material;
			material.MaterialID = i;
			memcpy(pShaderRecord + offsetToMaterialConstants,
			       &material,
			       sizeof(material));
		}
	};

	// Rendering pipelines
	// ----------------------------------------------------------------//

	// Baricentric
	{
		CComPtr<ID3D12StateObject> pbarycentricPSO;
		g_pRaytracingDevice->CreateStateObject(&stateObject,
		                                       IID_PPV_ARGS(&pbarycentricPSO));
		GetShaderTable(model, pbarycentricPSO, pHitShaderTable.data());
		g_RaytracingInputs[Primarybarycentric] = RaytracingDispatchRayInputs(
			*g_pRaytracingDevice, pbarycentricPSO,
			pHitShaderTable.data(), shaderIdentifierSize, (UINT)pHitShaderTable.size(),
			rayGenShaderExportName, missExportName);
	}

	// Reflections Barycentric
	{
		rayGenDxilLibSubobject = CreateDxilLibrary(
			rayGenShaderExportName, g_pRayGenerationShaderSSRLib,
			sizeof(g_pRayGenerationShaderSSRLib), rayGenDxilLibDesc,
			rayGenExportDesc);

		CComPtr<ID3D12StateObject> pReflectionbarycentricPSO;
		g_pRaytracingDevice->CreateStateObject(&stateObject, IID_PPV_ARGS(&pReflectionbarycentricPSO));
		GetShaderTable(model, pReflectionbarycentricPSO, pHitShaderTable.data());
		g_RaytracingInputs[Reflectionbarycentric] = RaytracingDispatchRayInputs(
			*g_pRaytracingDevice, pReflectionbarycentricPSO,
			pHitShaderTable.data(), shaderIdentifierSize, (UINT)pHitShaderTable.size(),
			rayGenShaderExportName, missExportName);
	}

	// Shadows
	{
		rayGenDxilLibSubobject = CreateDxilLibrary(
			rayGenShaderExportName, g_pRayGenerationShadowsLib,
			sizeof(g_pRayGenerationShadowsLib), rayGenDxilLibDesc,
			rayGenExportDesc);
		missDxilLibSubobject = CreateDxilLibrary(
			missExportName, g_pmissShadowsLib, sizeof(g_pmissShadowsLib),
			missDxilLibDesc, missExportDesc);

		CComPtr<ID3D12StateObject> pShadowsPSO;
		g_pRaytracingDevice->CreateStateObject(&stateObject,
		                                       IID_PPV_ARGS(&pShadowsPSO));
		GetShaderTable(model, pShadowsPSO, pHitShaderTable.data());
		g_RaytracingInputs[Shadows] = RaytracingDispatchRayInputs(
			*g_pRaytracingDevice, pShadowsPSO,
			pHitShaderTable.data(), shaderIdentifierSize, (UINT)pHitShaderTable.size(),
			rayGenShaderExportName,
			missExportName);
	}

	// Diffuse PSO
	{
		rayGenDxilLibSubobject = CreateDxilLibrary(
			rayGenShaderExportName, g_pRayGenerationShaderLib,
			sizeof(g_pRayGenerationShaderLib), rayGenDxilLibDesc,
			rayGenExportDesc);

		closestHitLibSubobject = CreateDxilLibrary(
			closestHitExportName, g_pDiffuseHitShaderLib,
			sizeof(g_pDiffuseHitShaderLib), closestHitDxilLibDesc,
			closestHitExportDesc);

		missDxilLibSubobject = CreateDxilLibrary(
			missExportName, g_pmissShaderLib, sizeof(g_pmissShaderLib),
			missDxilLibDesc, missExportDesc);

		CComPtr<ID3D12StateObject> pDiffusePSO;
		g_pRaytracingDevice->CreateStateObject(&stateObject, IID_PPV_ARGS(&pDiffusePSO));
		GetShaderTable(model, pDiffusePSO, pHitShaderTable.data());
		g_RaytracingInputs[DiffuseHitShader] = RaytracingDispatchRayInputs(
			*g_pRaytracingDevice, pDiffusePSO,
			pHitShaderTable.data(), shaderIdentifierSize, (UINT)pHitShaderTable.size(),
			rayGenShaderExportName, missExportName);
	}

	// SSR
	{
		rayGenDxilLibSubobject = CreateDxilLibrary(
			rayGenShaderExportName, g_pRayGenerationShaderSSRLib,
			sizeof(g_pRayGenerationShaderSSRLib), rayGenDxilLibDesc,
			rayGenExportDesc);
		closestHitLibSubobject = CreateDxilLibrary(
			closestHitExportName, g_pDiffuseHitShaderLib,
			sizeof(g_pDiffuseHitShaderLib), closestHitDxilLibDesc,
			closestHitExportDesc);
		missDxilLibSubobject = CreateDxilLibrary(
			missExportName, g_pmissShaderLib, sizeof(g_pmissShaderLib),
			missDxilLibDesc, missExportDesc);

		CComPtr<ID3D12StateObject> pReflectionPSO;
		g_pRaytracingDevice->CreateStateObject(&stateObject,
		                                       IID_PPV_ARGS(&pReflectionPSO));
		GetShaderTable(model, pReflectionPSO, pHitShaderTable.data());
		g_RaytracingInputs[Reflection] = RaytracingDispatchRayInputs(
			*g_pRaytracingDevice, pReflectionPSO,
			pHitShaderTable.data(), shaderIdentifierSize, (UINT)pHitShaderTable.size(),
			rayGenShaderExportName, missExportName);
	}
	// -----------------//

	WCHAR hitGroupExportNameAnyHitType[64];
	swprintf_s(hitGroupExportNameAnyHitType, L"%s::anyhit", hitGroupExportName);

	WCHAR hitGroupExportNameClosestHitType[64];
	swprintf_s(hitGroupExportNameClosestHitType, L"%s::closesthit", hitGroupExportName);

	// Set pipeline stack size for all ray tracing pipelines.
	// ----------------------------------------------------------------//
	for (auto& raytracingPipelineState : g_RaytracingInputs)
	{
		SetPipelineStateStackSize(rayGenShaderExportName, hitGroupExportNameClosestHitType,
		                          hitGroupExportNameAnyHitType, missExportName,
		                          MaxRayRecursion, raytracingPipelineState.m_pPSO);
	}
}

void InitializeStateObjects(const Model& model, UINT numMeshes)
{
	ZeroMemory(&g_dynamicCb, sizeof(g_dynamicCb));

	D3D12_STATIC_SAMPLER_DESC staticSamplerDescs[2] = {};
	D3D12_STATIC_SAMPLER_DESC& defaultSampler = staticSamplerDescs[0];
	defaultSampler.Filter = D3D12_FILTER_ANISOTROPIC;
	defaultSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	defaultSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	defaultSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	defaultSampler.MipLODBias = 0.0f;
	defaultSampler.MaxAnisotropy = 16;
	defaultSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	defaultSampler.MinLOD = 0.0f;
	defaultSampler.MaxLOD = D3D12_FLOAT32_MAX;
	defaultSampler.MaxAnisotropy = 8;
	defaultSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
	defaultSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	defaultSampler.ShaderRegister = 0;

	D3D12_STATIC_SAMPLER_DESC& shadowSampler = staticSamplerDescs[1];
	shadowSampler = staticSamplerDescs[0];
	shadowSampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	shadowSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
	shadowSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	shadowSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	shadowSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	shadowSampler.ShaderRegister = 1;

	D3D12_DESCRIPTOR_RANGE1 sceneBuffersDescriptorRange = {};
	sceneBuffersDescriptorRange.BaseShaderRegister = 1;
	sceneBuffersDescriptorRange.NumDescriptors = 5;
	sceneBuffersDescriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	sceneBuffersDescriptorRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

	D3D12_DESCRIPTOR_RANGE1 srvDescriptorRange = {};
	srvDescriptorRange.BaseShaderRegister = 12;
	srvDescriptorRange.NumDescriptors = 2;
	srvDescriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvDescriptorRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

	D3D12_DESCRIPTOR_RANGE1 uavDescriptorRange = {};
	uavDescriptorRange.BaseShaderRegister = 2;
	uavDescriptorRange.NumDescriptors = 10;
	uavDescriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
	uavDescriptorRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

	/*D3D12_DESCRIPTOR_RANGE1 centerSrvDescriptorRange = {};
	srvDescriptorRange.BaseShaderRegister = 1;
	srvDescriptorRange.NumDescriptors = 1;
	srvDescriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvDescriptorRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;*/

	CD3DX12_ROOT_PARAMETER1 globalRootSignatureParameters[8];
	globalRootSignatureParameters[0].InitAsDescriptorTable(1, &sceneBuffersDescriptorRange);
	globalRootSignatureParameters[1].InitAsConstantBufferView(0);
	globalRootSignatureParameters[2].InitAsConstantBufferView(1);
	globalRootSignatureParameters[3].InitAsDescriptorTable(1, &srvDescriptorRange);
	globalRootSignatureParameters[4].InitAsDescriptorTable(1, &uavDescriptorRange);
	globalRootSignatureParameters[5].InitAsUnorderedAccessView(0);
	globalRootSignatureParameters[6].InitAsUnorderedAccessView(1);
	globalRootSignatureParameters[7].InitAsShaderResourceView(0);
	//globalRootSignatureParameters[8].InitAsDescriptorTable(1, &centerSrvDescriptorRange);
	auto globalRootSignatureDesc = CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC(
		ARRAYSIZE(globalRootSignatureParameters), globalRootSignatureParameters, ARRAYSIZE(staticSamplerDescs),
		staticSamplerDescs);

	CComPtr<ID3DBlob> pGlobalRootSignatureBlob;
	CComPtr<ID3DBlob> pErrorBlob;
	if (FAILED(D3D12SerializeVersionedRootSignature(&globalRootSignatureDesc, &pGlobalRootSignatureBlob, &pErrorBlob)))
	{
		OutputDebugStringA((LPCSTR)pErrorBlob->GetBufferPointer());
	}
	g_pRaytracingDevice->CreateRootSignature(0, pGlobalRootSignatureBlob->GetBufferPointer(),
	                                         pGlobalRootSignatureBlob->GetBufferSize(),
	                                         IID_PPV_ARGS(&g_GlobalRaytracingRootSignature));

	D3D12_DESCRIPTOR_RANGE1 localTextureDescriptorRange = {};
	localTextureDescriptorRange.BaseShaderRegister = 6;
	localTextureDescriptorRange.NumDescriptors = 2;
	localTextureDescriptorRange.RegisterSpace = 0;
	localTextureDescriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	localTextureDescriptorRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;

	CD3DX12_ROOT_PARAMETER1 localRootSignatureParameters[2];
	UINT sizeOfRootConstantInDwords = (sizeof(MaterialRootConstant) - 1) / sizeof(DWORD) + 1;
	localRootSignatureParameters[0].InitAsDescriptorTable(1, &localTextureDescriptorRange);
	localRootSignatureParameters[1].InitAsConstants(sizeOfRootConstantInDwords, 3);
	auto localRootSignatureDesc = CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC(
		ARRAYSIZE(localRootSignatureParameters), localRootSignatureParameters, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

	CComPtr<ID3DBlob> pLocalRootSignatureBlob;
	D3D12SerializeVersionedRootSignature(&localRootSignatureDesc, &pLocalRootSignatureBlob, nullptr);
	g_pRaytracingDevice->CreateRootSignature(0, pLocalRootSignatureBlob->GetBufferPointer(),
	                                         pLocalRootSignatureBlob->GetBufferSize(),
	                                         IID_PPV_ARGS(&g_LocalRaytracingRootSignature));

	if (g_RayTraceSupport)
	{
		InitializeRaytracingStateObjects(model, numMeshes);
	}
}


void D3D12RaytracingMiniEngineSample::Startup(void)
{
	//m_Camera = m_VRCamera[VRCamera::CENTER];

	rayTracingMode = RTM_OFF;

	ThrowIfFailed(g_Device->QueryInterface(IID_PPV_ARGS(&g_pRaytracingDevice)),
	              L"Couldn't get DirectX Raytracing interface for the device.\n");

	g_SceneNormalBuffer.CreateArray(L"Main Normal Buffer", g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight(), 2,
	                           DXGI_FORMAT_R11G11B10_FLOAT);

	g_pRaytracingDescriptorHeap = std::unique_ptr<DescriptorHeapStack>(
		new DescriptorHeapStack(*g_Device, 200, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 0));

	D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1;
	HRESULT hr = g_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1));

	SamplerDesc DefaultSamplerDesc;
	DefaultSamplerDesc.MaxAnisotropy = 8;

	m_RootSig.Reset(7, 2);
	m_RootSig.InitStaticSampler(0, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.InitStaticSampler(1, SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[1].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 6, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 64, 6, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[4].InitAsConstants(1, 2, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[5].InitAsConstants(1, 1, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[6].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 6, 1, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.Finalize(L"D3D12RaytracingMiniEngineSample",
	                   D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	DXGI_FORMAT ColorFormat = g_SceneColorBuffer.GetFormat();
	DXGI_FORMAT NormalFormat = g_SceneNormalBuffer.GetFormat();
	DXGI_FORMAT DepthFormat = g_SceneDepthBuffer.GetFormat();
	DXGI_FORMAT ShadowFormat = g_ShadowBuffer.GetFormat();
	auto makeVertexInputElement = [](const char* name, DXGI_FORMAT format)
	{
		return D3D12_INPUT_ELEMENT_DESC
		{
			name, 0, format, 0, D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		};
	};
	D3D12_INPUT_ELEMENT_DESC vertElem[] =
	{
		makeVertexInputElement("POSITION", DXGI_FORMAT_R32G32B32_FLOAT),
		makeVertexInputElement("TEXCOORD", DXGI_FORMAT_R32G32_FLOAT),
		makeVertexInputElement("NORMAL", DXGI_FORMAT_R32G32B32_FLOAT),
		makeVertexInputElement("TANGENT", DXGI_FORMAT_R32G32B32_FLOAT),
		makeVertexInputElement("BITANGENT", DXGI_FORMAT_R32G32B32_FLOAT)
	};

	// Depth-only (2x rate)
	m_DepthPSO.SetRootSignature(m_RootSig);
	m_DepthPSO.SetRasterizerState(RasterizerDefault);
	m_DepthPSO.SetBlendState(BlendNoColorWrite);
	m_DepthPSO.SetDepthStencilState(DepthStateReadWrite);
	m_DepthPSO.SetInputLayout(_countof(vertElem), vertElem);
	m_DepthPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_DepthPSO.SetRenderTargetFormats(0, nullptr, DepthFormat);
	m_DepthPSO.SetVertexShader(g_pDepthViewerVS, sizeof(g_pDepthViewerVS));

	// Make a copy of the desc before we mess with it
	m_CutoutDepthPSO = m_DepthPSO;
	m_ShadowPSO = m_DepthPSO;

	m_DepthPSO.Finalize();

	// Depth-only shading but with alpha testing

	m_CutoutDepthPSO.SetPixelShader(g_pDepthViewerPS, sizeof(g_pDepthViewerPS));
	m_CutoutDepthPSO.SetRasterizerState(RasterizerTwoSided);
	m_CutoutDepthPSO.Finalize();

	// Depth-only but with a depth bias and/or render only backfaces

	m_ShadowPSO.SetRasterizerState(RasterizerShadow);
	m_ShadowPSO.SetRenderTargetFormats(0, nullptr, g_ShadowBuffer.GetFormat());
	m_ShadowPSO.Finalize();

	// Shadows with alpha testing
	m_CutoutShadowPSO = m_ShadowPSO;
	m_CutoutShadowPSO.SetPixelShader(g_pDepthViewerPS, sizeof(g_pDepthViewerPS));
	m_CutoutShadowPSO.SetRasterizerState(RasterizerShadowTwoSided);
	m_CutoutShadowPSO.Finalize();

	// Full color pass
	m_ModelPSO = m_DepthPSO;
	m_ModelPSO.SetBlendState(BlendTraditional);
	m_ModelPSO.SetDepthStencilState(DepthStateTestEqual);
	DXGI_FORMAT formats[]{ColorFormat, NormalFormat};
	m_ModelPSO.SetRenderTargetFormats(_countof(formats), formats, DepthFormat);
	m_ModelPSO.SetVertexShader(g_pModelViewerVS, sizeof(g_pModelViewerVS));
	m_ModelPSO.SetPixelShader(g_pModelViewerPS, sizeof(g_pModelViewerPS));
	m_ModelPSO.Finalize();

	m_CutoutModelPSO = m_ModelPSO;
	m_CutoutModelPSO.SetRasterizerState(RasterizerTwoSided);
	m_CutoutModelPSO.Finalize();

	// A debug shader for counting lights in a tile
	m_WaveTileCountPSO = m_ModelPSO;
	m_WaveTileCountPSO.SetPixelShader(g_pWaveTileCountPS, sizeof(g_pWaveTileCountPS));
	m_WaveTileCountPSO.Finalize();

	m_ComputeRootSig.Reset(3, 0);
	m_ComputeRootSig[0].InitAsDescriptorRange(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_ALL);
	m_ComputeRootSig[1].InitAsDescriptorRange(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, D3D12_SHADER_VISIBILITY_ALL);
	m_ComputeRootSig[2].InitAsDescriptorRange(
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 1, D3D12_SHADER_VISIBILITY_ALL);
	m_ComputeRootSig.Finalize(L"D3D12RaytracingMiniEngineSampleCompute",
	                          D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	m_CombineDepthPSO.SetRootSignature(m_ComputeRootSig);
	m_CombineDepthPSO.SetComputeShader(g_pCombineDepthsCS,
	                                   sizeof(g_pCombineDepthsCS));
	m_CombineDepthPSO.Finalize();

	m_CombineColourSig.Reset(2, 0);
	m_CombineColourSig[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
	m_CombineColourSig[1].InitAsDescriptorRange(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
	m_CombineColourSig.Finalize(L"D3D12RaytracingMiniEngineCombineColour",
	                            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	m_CombineColourPSO.SetRootSignature(m_CombineColourSig);
	m_CombineColourPSO.SetRasterizerState(RasterizerDefault);
	m_CombineColourPSO.SetBlendState(BlendTraditional);
	m_CombineColourPSO.SetDepthStencilState(DepthStateReadOnly);
	//m_CombineColourPSO.SetInputLayout(_countof(vertElem), vertElem);
	m_CombineColourPSO.SetInputLayout(0, nullptr);
	m_CombineColourPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_CombineColourPSO.SetRenderTargetFormat(ColorFormat, DXGI_FORMAT_UNKNOWN);
	m_CombineColourPSO.SetVertexShader(g_pCombineColourVS, sizeof(g_pCombineColourVS));
	m_CombineColourPSO.SetPixelShader(g_pCombineColourPS, sizeof(g_pCombineColourPS));
	m_CombineColourPSO.Finalize();

	Lighting::InitializeResources();

	m_ExtraTextures[0] = g_SSAOFullScreen.GetSRV();
	m_ExtraTextures[1] = g_ShadowBuffer.GetSRV();

	// Screen Texture PSO
	// Albert
	{
		D3D12_INPUT_ELEMENT_DESC screenVertElem[] =
		{
			{
				"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
			},
			{
				"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
				D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
			}
		};


		RootSignature& rs = m_screenTextureData.m_RootSignature;
		rs.Reset(RS_ScreenTexture::kCount, 1);
		rs[RS_ScreenTexture::kTextureToRender].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		                                                             RS_ScreenTexture::kTextureToRender, 1,
		                                                             D3D12_SHADER_VISIBILITY_PIXEL);
		rs.InitStaticSampler(0, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
		rs.Finalize(L"D3D12RaytracingMiniEngineSample AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
		            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
		// SubAlbert
		GraphicsPSO& pso = m_screenTextureData.m_PSO;
		pso.SetRootSignature(rs);
		pso.SetRasterizerState(RasterizerDefault);
		pso.SetBlendState(BlendNoColorWrite);
		pso.SetDepthStencilState(DepthStateReadWrite);
		pso.SetInputLayout(_countof(screenVertElem), screenVertElem);
		pso.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
		DXGI_FORMAT formats[]{ColorFormat, NormalFormat};
		pso.SetRenderTargetFormats(_countof(formats), formats, DepthFormat);
		pso.SetVertexShader(g_pScreenTextureVS, sizeof(g_pScreenTextureVS));
		pso.SetPixelShader(g_pScreenTexturePS, sizeof(g_pScreenTexturePS));
		pso.Finalize();
	}

#define ASSET_DIRECTORY "../MiniEngine/ModelViewer/"
	TextureManager::Initialize(ASSET_DIRECTORY L"Textures/");
	bool bModelLoadSuccess = m_Model.Load(ASSET_DIRECTORY "Models/sponza.h3d");
	ASSERT(bModelLoadSuccess, "Failed to load model");
	ASSERT(m_Model.m_Header.meshCount > 0, "Model contains no meshes");

	// The caller of this function can override which materials are considered cutouts
	m_pMaterialIsCutout.resize(m_Model.m_Header.materialCount);
	m_pMaterialIsReflective.resize(m_Model.m_Header.materialCount);
	for (uint32_t i = 0; i < m_Model.m_Header.materialCount; ++i)
	{
		const Model::Material& mat = m_Model.m_pMaterial[i];
		if (std::string(mat.texDiffusePath).find("thorn") != std::string::npos ||
			std::string(mat.texDiffusePath).find("plant") != std::string::npos ||
			std::string(mat.texDiffusePath).find("chain") != std::string::npos)
		{
			m_pMaterialIsCutout[i] = true;
		}
		else
		{
			m_pMaterialIsCutout[i] = false;
		}

		if (std::string(mat.texDiffusePath).find("floor") != std::string::npos)
		{
			m_pMaterialIsReflective[i] = true;
		}
		else
		{
			m_pMaterialIsReflective[i] = false;
		}
	}

	g_hitConstantBuffer.Create(L"Hit Constant Buffer", 1, sizeof(HitShaderConstants));
	g_dynamicConstantBuffer.Create(L"Dynamic Constant Buffer", 1, sizeof(DynamicCB));

	InitializeSceneInfo(m_Model);
	InitializeViews(m_Model);
	UINT numMeshes = m_Model.m_Header.meshCount;


	if (g_RayTraceSupport)
	{
		CreateRayTraceAccelerationStructures(numMeshes);
		OutputDebugStringW(L"------------------------------\nDXR support present on Device\n------------------------------\n");
	}
	else
	{
		rayTracingMode = RTM_OFF;
		OutputDebugStringW(L"------------------------------\nDXR support not present on Device\n------------------------------\n");
	}

	InitializeStateObjects(m_Model, numMeshes);

	float modelRadius = Length(m_Model.m_Header.boundingBox.max - m_Model.m_Header.boundingBox.min) * .5f;
	const Vector3 eye = (m_Model.m_Header.boundingBox.min + m_Model.m_Header.boundingBox.max) * .5f + Vector3(
		modelRadius * .5f, 0.0f, 0.0f);
	m_Camera.SetEyeAtUp(eye, Vector3(kZero), Vector3(kYUnitVector));

	m_CameraPosArrayCurrentPosition = 0;

	// Lion's head
	m_CameraPosArray[0].position = Vector3(-1100.0f, 170.0f, -30.0f);
	m_CameraPosArray[0].heading = 1.5707f;
	m_CameraPosArray[0].pitch = 0.0f;

	// View of columns
	m_CameraPosArray[1].position = Vector3(299.0f, 208.0f, -202.0f);
	m_CameraPosArray[1].heading = -3.1111f;
	m_CameraPosArray[1].pitch = 0.5953f;

	// Bottom-up view from the floor
	m_CameraPosArray[2].position = Vector3(-1237.61f, 80.60f, -26.02f);
	m_CameraPosArray[2].heading = -1.5707f;
	m_CameraPosArray[2].pitch = 0.268f;

	// Top-down view from the second floor
	m_CameraPosArray[3].position = Vector3(-977.90f, 595.05f, -194.97f);
	m_CameraPosArray[3].heading = -2.077f;
	m_CameraPosArray[3].pitch = - 0.450f;

	// View of corridors on the second floor
	m_CameraPosArray[4].position = Vector3(-1463.0f, 600.0f, 394.52f);
	m_CameraPosArray[4].heading = -1.236f;
	m_CameraPosArray[4].pitch = 0.0f;

	m_Camera.Setup(1.0f, 500.0f, 3000.0f, false, m_screenTextureData.m_Quad);
	//m_Camera.SetZRange(1.0f, 10000.0f);

	m_CameraController.reset(new VRCameraController(m_Camera, Vector3(kYUnitVector)));

	MotionBlur::Enable = false;         //true;
	TemporalEffects::EnableTAA = false; //true;
	FXAA::Enable = false;
	PostEffects::EnableHDR = false;        //true;
	PostEffects::EnableAdaptation = false; //true;
	SSAO::Enable = true;

	Lighting::CreateRandomLights(m_Model.GetBoundingBox().min, m_Model.GetBoundingBox().max);

	m_ExtraTextures[2] = Lighting::m_LightBuffer.GetSRV();
	m_ExtraTextures[3] = Lighting::m_LightShadowArray.GetSRV();
	m_ExtraTextures[4] = Lighting::m_LightGrid.GetSRV();
	m_ExtraTextures[5] = Lighting::m_LightGridBitMask.GetSRV();

	CreateQuadVerts();
}

void D3D12RaytracingMiniEngineSample::Cleanup(void)
{
	m_Model.Clear();
}


namespace Graphics
{
extern EnumVar DebugZoom;
}


void D3D12RaytracingMiniEngineSample::Update(float deltaT)
{
	ScopedTimer _prof(L"Update State");

	if (GameInput::IsFirstPressed(GameInput::kLShoulder))
		DebugZoom.Decrement();
	else if (GameInput::IsFirstPressed(GameInput::kRShoulder))
		DebugZoom.Increment();
	if (g_RayTraceSupport)
	{
		if (GameInput::IsFirstPressed(GameInput::kKey_1))
			rayTracingMode = RTM_OFF;
		else if (GameInput::IsFirstPressed(GameInput::kKey_2))
			rayTracingMode = RTM_TRAVERSAL;
		else if (GameInput::IsFirstPressed(GameInput::kKey_3))
			rayTracingMode = RTM_SSR;
		else if (GameInput::IsFirstPressed(GameInput::kKey_4))
			rayTracingMode = RTM_SHADOWS;
		else if (GameInput::IsFirstPressed(GameInput::kKey_5))
			rayTracingMode = RTM_DIFFUSE_WITH_SHADOWMAPS;
		else if (GameInput::IsFirstPressed(GameInput::kKey_6))
			rayTracingMode = RTM_DIFFUSE_WITH_SHADOWRAYS;
		else if (GameInput::IsFirstPressed(GameInput::kKey_7))
			rayTracingMode = RTM_REFLECTIONS;
	}

	static bool freezeCamera = false;

	if (GameInput::IsFirstPressed(GameInput::kKey_f))
	{
		freezeCamera = !freezeCamera;
	}

	if (GameInput::IsFirstPressed(GameInput::kKey_f1))
	{
		m_CameraPosArrayCurrentPosition = (m_CameraPosArrayCurrentPosition + c_NumCameraPositions - 1) %
			c_NumCameraPositions;
		SetCameraToPredefinedPosition(m_CameraPosArrayCurrentPosition);
	}
	else if (GameInput::IsFirstPressed(GameInput::kKey_f2))
	{
		m_CameraPosArrayCurrentPosition = (m_CameraPosArrayCurrentPosition + 1) % c_NumCameraPositions;
		SetCameraToPredefinedPosition(m_CameraPosArrayCurrentPosition);
	}

	if (!freezeCamera)
	{
		m_CameraController->Update(deltaT);
	}

	//m_ViewProjMatrix = m_Camera.GetViewProjMatrix();

	float costheta = cosf(m_SunOrientation);
	float sintheta = sinf(m_SunOrientation);
	float cosphi = cosf(m_SunInclination * XM_PIDIV2);
	float sinphi = sinf(m_SunInclination * XM_PIDIV2);
	m_SunDirection = Normalize(Vector3(costheta * cosphi, sinphi, sintheta * cosphi));

	// We use viewport offsets to jitter sample positions from frame to frame (for TAA.)
	// D3D has a design quirk with fractional offsets such that the implicit scissor
	// region of a viewport is floor(TopLeftXY) and floor(TopLeftXY + WidthHeight), so
	// having a negative fractional top left, e.g. (-0.25, -0.25) would also shift the
	// BottomRight corner up by a whole integer.  One solution is to pad your viewport
	// dimensions with an extra pixel.  My solution is to only use positive fractional offsets,
	// but that means that the average sample position is +0.5, which I use when I disable
	// temporal AA.
	TemporalEffects::GetJitterOffset(m_MainViewport.TopLeftX, m_MainViewport.TopLeftY);

	m_MainViewport.Width = (float)g_SceneColorBuffer.GetWidth();
	m_MainViewport.Height = (float)g_SceneColorBuffer.GetHeight();
	m_MainViewport.MinDepth = 0.0f;
	m_MainViewport.MaxDepth = 1.0f;

	m_MainScissor.left = 0;
	m_MainScissor.top = 0;
	m_MainScissor.right = (LONG)g_SceneColorBuffer.GetWidth();
	m_MainScissor.bottom = (LONG)g_SceneColorBuffer.GetHeight();
}

void D3D12RaytracingMiniEngineSample::CreateQuadVerts()
{
	auto makeQuad = [](QuadPos& Quad, StructuredBuffer& Buffer, LPCWSTR Name)
	{
		__declspec(align(16)) const float vertices[] =
		{
			Quad.topLeft.GetX(), Quad.topLeft.GetY(), Quad.topLeft.GetZ(),
			// Position // TODO: Does the Quad need to be divided by w?
			0, 0, // UV

			Quad.bottomLeft.GetX(), Quad.bottomLeft.GetY(), Quad.bottomLeft.GetZ(), // Position
			0, 1,                                                                   // UV

			Quad.topRight.GetX(), Quad.topRight.GetY(), Quad.topRight.GetZ(), // Position
			1, 0,                                                             // UV

			Quad.topRight.GetX(), Quad.topRight.GetY(), Quad.topRight.GetZ(), // Position
			1, 0,                                                             // UV

			Quad.bottomLeft.GetX(), Quad.bottomLeft.GetY(), Quad.bottomLeft.GetZ(), // Position
			0, 1,                                                                   // UV

			Quad.bottomRight.GetX(), Quad.bottomRight.GetY(), Quad.bottomRight.GetZ(), // Position
			1, 1,                                                                      // UV
		};

		Buffer.Create(Name, 6, sizeof(float) * 14, vertices);
	};
	ScreenTextureData& data = m_screenTextureData;
	makeQuad(data.m_Quad, data.m_Buffer, L"Quad Vertex Buffer");
}

void D3D12RaytracingMiniEngineSample::RenderObjects(
	GraphicsContext& gfxContext,
	const Matrix4& ViewProjMat,
	UINT curCam, eObjectFilter Filter)
{
	struct VSConstants
	{
		Matrix4 modelToProjection;
		Matrix4 modelToShadow;
		XMFLOAT3 viewerPos;
		UINT curCam;
	} vsConstants;
	vsConstants.curCam = curCam;
	vsConstants.modelToProjection = ViewProjMat;
	vsConstants.modelToShadow = m_SunShadow.GetShadowMatrix();
	XMStoreFloat3(&vsConstants.viewerPos, m_Camera[curCam]->GetPosition());

	gfxContext.SetDynamicConstantBufferView(0, sizeof(vsConstants), &vsConstants);

	uint32_t materialIdx = 0xFFFFFFFFul;

	uint32_t VertexStride = m_Model.m_VertexStride;

	for (uint32_t meshIndex = 0; meshIndex < m_Model.m_Header.meshCount; meshIndex++)
	{
		const Model::Mesh& mesh = m_Model.m_pMesh[meshIndex];

		uint32_t indexCount = mesh.indexCount;
		uint32_t startIndex = mesh.indexDataByteOffset / sizeof(uint16_t);
		uint32_t baseVertex = mesh.vertexDataByteOffset / VertexStride;

		if (mesh.materialIndex != materialIdx)
		{
			if (m_pMaterialIsCutout[mesh.materialIndex] && !(Filter & kCutout) ||
				!m_pMaterialIsCutout[mesh.materialIndex] && !(Filter & kOpaque))
				continue;

			materialIdx = mesh.materialIndex;
			gfxContext.SetDynamicDescriptors(2, 0, 6, m_Model.GetSRVs(materialIdx));
		}
		uint32_t areNormalsNeeded = 1;
		// (rayTracingMode != RTM_REFLECTIONS) || m_pMaterialIsReflective[mesh.materialIndex];
		gfxContext.SetConstants(4, baseVertex, materialIdx);
		gfxContext.SetConstants(5, areNormalsNeeded);

		gfxContext.DrawIndexed(indexCount, startIndex, baseVertex);
	}

	vsConstants.modelToProjection = Matrix4(XMMatrixScaling(2000, 2000, 1));
}

void D3D12RaytracingMiniEngineSample::RenderCenterViewToEye(GraphicsContext& Ctx, UINT CurCam)
{
	// Render Quad
	Ctx.SetPipelineState(m_screenTextureData.m_PSO);
	Ctx.SetRootSignature(m_screenTextureData.m_RootSignature);
	Ctx.SetRenderTarget(g_SceneColorBuffer.GetSubRTV(CurCam));
	Ctx.SetVertexBuffer(0, m_screenTextureData.m_Buffer.VertexBufferView());
	Ctx.SetDynamicDescriptor(RS_ScreenTexture::kTextureToRender, 0, g_SceneColorBuffer.GetSubSRV(2));
	Ctx.Draw(6);
}

void D3D12RaytracingMiniEngineSample::SetCameraToPredefinedPosition(int cameraPosition)
{
	if (cameraPosition < 0 || cameraPosition >= c_NumCameraPositions)
		return;

	m_CameraController->SetCurrentHeading(m_CameraPosArray[m_CameraPosArrayCurrentPosition].heading);
	m_CameraController->SetCurrentPitch(m_CameraPosArray[m_CameraPosArrayCurrentPosition].pitch);

	Matrix3 neworientation = Matrix3(m_CameraController->GetWorldEast(), m_CameraController->GetWorldUp(),
	                                 -m_CameraController->GetWorldNorth())
		* Matrix3::MakeYRotation(m_CameraController->GetCurrentHeading())
		* Matrix3::MakeXRotation(m_CameraController->GetCurrentPitch());
	m_Camera.SetTransform(AffineTransform(neworientation, m_CameraPosArray[m_CameraPosArrayCurrentPosition].position));
	m_Camera.Update();
}

void D3D12RaytracingMiniEngineSample::CreateRayTraceAccelerationStructures(UINT numMeshes)
{
	const UINT numBottomLevels = 1;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo;
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelAccelerationStructureDesc = {};
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& topLevelInputs = topLevelAccelerationStructureDesc.Inputs;
	topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	topLevelInputs.NumDescs = numBottomLevels;
	topLevelInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	topLevelInputs.pGeometryDescs = nullptr;
	topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	g_pRaytracingDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);

	const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlag =
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs(m_Model.m_Header.meshCount);
	UINT64 scratchBufferSizeNeeded = topLevelPrebuildInfo.ScratchDataSizeInBytes;
	for (UINT i = 0; i < numMeshes; i++)
	{
		auto& mesh = m_Model.m_pMesh[i];

		D3D12_RAYTRACING_GEOMETRY_DESC& desc = geometryDescs[i];
		desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

		D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& trianglesDesc = desc.Triangles;
		trianglesDesc.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		trianglesDesc.VertexCount = mesh.vertexCount;
		trianglesDesc.VertexBuffer.StartAddress = m_Model.m_VertexBuffer.GetGpuVirtualAddress() + (mesh.
			vertexDataByteOffset + mesh.attrib[Model::attrib_position].offset);
		trianglesDesc.IndexBuffer = m_Model.m_IndexBuffer.GetGpuVirtualAddress() + mesh.indexDataByteOffset;
		trianglesDesc.VertexBuffer.StrideInBytes = mesh.vertexStride;
		trianglesDesc.IndexCount = mesh.indexCount;
		trianglesDesc.IndexFormat = DXGI_FORMAT_R16_UINT;
		trianglesDesc.Transform3x4 = 0;
	}

	std::vector<UINT64> bottomLevelAccelerationStructureSize(numBottomLevels);
	std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC> bottomLevelAccelerationStructureDescs(
		numBottomLevels);
	for (UINT i = 0; i < numBottomLevels; i++)
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC& bottomLevelAccelerationStructureDesc =
			bottomLevelAccelerationStructureDescs[i];
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& bottomLevelInputs = bottomLevelAccelerationStructureDesc.
			Inputs;
		bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		bottomLevelInputs.NumDescs = numMeshes;
		bottomLevelInputs.pGeometryDescs = &geometryDescs[i];
		bottomLevelInputs.Flags = buildFlag;
		bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelprebuildInfo;
		g_pRaytracingDevice->GetRaytracingAccelerationStructurePrebuildInfo(
			&bottomLevelInputs, &bottomLevelprebuildInfo);

		bottomLevelAccelerationStructureSize[i] = bottomLevelprebuildInfo.ResultDataMaxSizeInBytes;
		scratchBufferSizeNeeded = std::max(bottomLevelprebuildInfo.ScratchDataSizeInBytes, scratchBufferSizeNeeded);
	}

	ByteAddressBuffer scratchBuffer;
	scratchBuffer.Create(L"Acceleration Structure Scratch Buffer", (UINT)scratchBufferSizeNeeded, 1);

	D3D12_HEAP_PROPERTIES defaultHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto topLevelDesc = CD3DX12_RESOURCE_DESC::Buffer(topLevelPrebuildInfo.ResultDataMaxSizeInBytes,
	                                                  D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	g_Device->CreateCommittedResource(
		&defaultHeapDesc,
		D3D12_HEAP_FLAG_NONE,
		&topLevelDesc,
		D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
		nullptr,
		IID_PPV_ARGS(&g_bvh_topLevelAccelerationStructure));

	topLevelAccelerationStructureDesc.DestAccelerationStructureData = g_bvh_topLevelAccelerationStructure->
		GetGPUVirtualAddress();
	topLevelAccelerationStructureDesc.ScratchAccelerationStructureData = scratchBuffer.GetGpuVirtualAddress();

	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs(numBottomLevels);
	g_bvh_bottomLevelAccelerationStructures.resize(numBottomLevels);
	for (UINT i = 0; i < bottomLevelAccelerationStructureDescs.size(); i++)
	{
		auto& bottomLevelStructure = g_bvh_bottomLevelAccelerationStructures[i];

		auto bottomLevelDesc = CD3DX12_RESOURCE_DESC::Buffer(bottomLevelAccelerationStructureSize[i],
		                                                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		g_Device->CreateCommittedResource(
			&defaultHeapDesc,
			D3D12_HEAP_FLAG_NONE,
			&bottomLevelDesc,
			D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
			nullptr,
			IID_PPV_ARGS(&bottomLevelStructure));

		bottomLevelAccelerationStructureDescs[i].DestAccelerationStructureData = bottomLevelStructure->
			GetGPUVirtualAddress();
		bottomLevelAccelerationStructureDescs[i].ScratchAccelerationStructureData = scratchBuffer.
			GetGpuVirtualAddress();

		D3D12_RAYTRACING_INSTANCE_DESC& instanceDesc = instanceDescs[i];
		UINT descriptorIndex = g_pRaytracingDescriptorHeap->AllocateBufferUav(*bottomLevelStructure);

		// Identity matrix
		ZeroMemory(instanceDesc.Transform, sizeof(instanceDesc.Transform));
		instanceDesc.Transform[0][0] = 1.0f;
		instanceDesc.Transform[1][1] = 1.0f;
		instanceDesc.Transform[2][2] = 1.0f;

		instanceDesc.AccelerationStructure = g_bvh_bottomLevelAccelerationStructures[i]->GetGPUVirtualAddress();
		instanceDesc.Flags = 0;
		instanceDesc.InstanceID = 0;
		instanceDesc.InstanceMask = 1;
		instanceDesc.InstanceContributionToHitGroupIndex = i;
	}

	ByteAddressBuffer instanceDataBuffer;
	instanceDataBuffer.Create(L"Instance m_Quads Buffer", numBottomLevels, sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
	                          instanceDescs.data());

	topLevelInputs.InstanceDescs = instanceDataBuffer.GetGpuVirtualAddress();
	topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;


	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Create Acceleration Structure");
	ID3D12GraphicsCommandList* pCommandList = gfxContext.GetCommandList();

	CComPtr<ID3D12GraphicsCommandList4> pRaytracingCommandList;
	pCommandList->QueryInterface(IID_PPV_ARGS(&pRaytracingCommandList));

	ID3D12DescriptorHeap* descriptorHeaps[] = {&g_pRaytracingDescriptorHeap->GetDescriptorHeap()};
	pRaytracingCommandList->SetDescriptorHeaps(ARRAYSIZE(descriptorHeaps), descriptorHeaps);

	auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
	for (UINT i = 0; i < bottomLevelAccelerationStructureDescs.size(); i++)
	{
		pRaytracingCommandList->BuildRaytracingAccelerationStructure(&bottomLevelAccelerationStructureDescs[i], 0,
		                                                             nullptr);
	}
	pCommandList->ResourceBarrier(1, &uavBarrier);

	pRaytracingCommandList->BuildRaytracingAccelerationStructure(&topLevelAccelerationStructureDesc, 0, nullptr);

	gfxContext.Finish(true);
}

void D3D12RaytracingMiniEngineSample::RenderLightShadows(GraphicsContext& gfxContext, UINT curCam)
{
	using namespace Lighting;

	ScopedTimer _prof(L"RenderLightShadows", gfxContext);

	static uint32_t LightIndex = 0;
	if (LightIndex >= MaxLights)
		return;

	m_LightShadowTempBuffer.BeginRendering(gfxContext);
	{
		gfxContext.SetPipelineState(m_ShadowPSO);
		RenderObjects(gfxContext, m_LightShadowMatrix[LightIndex], curCam, kOpaque);
		gfxContext.SetPipelineState(m_CutoutShadowPSO);
		RenderObjects(gfxContext, m_LightShadowMatrix[LightIndex], curCam, kCutout);
	}
	m_LightShadowTempBuffer.EndRendering(gfxContext);

	gfxContext.TransitionResource(m_LightShadowTempBuffer, D3D12_RESOURCE_STATE_GENERIC_READ);
	gfxContext.TransitionResource(m_LightShadowArray, D3D12_RESOURCE_STATE_COPY_DEST);

	gfxContext.CopySubresource(m_LightShadowArray, LightIndex, m_LightShadowTempBuffer, 0);

	gfxContext.TransitionResource(m_LightShadowArray, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	++LightIndex;
}

void D3D12RaytracingMiniEngineSample::RenderScene()
{
	const bool skipDiffusePass =
		rayTracingMode == RTM_DIFFUSE_WITH_SHADOWMAPS ||
		rayTracingMode == RTM_DIFFUSE_WITH_SHADOWRAYS ||
		rayTracingMode == RTM_TRAVERSAL;

	const bool skipShadowMap =
		rayTracingMode == RTM_DIFFUSE_WITH_SHADOWRAYS ||
		rayTracingMode == RTM_TRAVERSAL ||
		rayTracingMode == RTM_SSR;

	static bool s_ShowLightCounts = false;
	if (ShowWaveTileCounts != s_ShowLightCounts)
	{
		static bool EnableHDR;
		if (ShowWaveTileCounts)
		{
			EnableHDR = PostEffects::EnableHDR;
			PostEffects::EnableHDR = false;
		}
		else
		{
			PostEffects::EnableHDR = EnableHDR;
		}
		s_ShowLightCounts = ShowWaveTileCounts;
	}

	GraphicsContext& leftGfxContext = GraphicsContext::Begin(L"Scene Render Left");
	GraphicsContext& rightGfxContext = GraphicsContext::Begin(L"Scene Render Right");
	GraphicsContext& centerGfxContext = GraphicsContext::Begin(L"Scene Render Center");

	ParticleEffects::Update(leftGfxContext.GetComputeContext(), Graphics::GetFrameTime());
	ParticleEffects::Update(rightGfxContext.GetComputeContext(), Graphics::GetFrameTime());
	ParticleEffects::Update(centerGfxContext.GetComputeContext(), Graphics::GetFrameTime());

	uint32_t FrameIndex = TemporalEffects::GetFrameIndexMod2();

	__declspec(align(16)) struct
	{
		Vector3 sunDirection;
		Vector3 sunLight;
		Vector3 ambientLight;
		float ShadowTexelSize[4];

		float InvTileDim[4];
		uint32_t TileCount[4];
		uint32_t FirstLightIndex[4];
		uint32_t FrameIndexMod2;
	} psConstants;

	psConstants.sunDirection = m_SunDirection;
	psConstants.sunLight = Vector3(1.0f, 1.0f, 1.0f) * m_SunLightIntensity;
	psConstants.ambientLight = Vector3(1.0f, 1.0f, 1.0f) * m_AmbientIntensity;
	psConstants.ShadowTexelSize[0] = 1.0f / g_ShadowBuffer.GetWidth();
	psConstants.InvTileDim[0] = 1.0f / Lighting::LightGridDim;
	psConstants.InvTileDim[1] = 1.0f / Lighting::LightGridDim;
	psConstants.TileCount[0] = Math::DivideByMultiple(g_SceneColorBuffer.GetWidth(), Lighting::LightGridDim);
	psConstants.TileCount[1] = Math::DivideByMultiple(g_SceneColorBuffer.GetHeight(), Lighting::LightGridDim);
	psConstants.FirstLightIndex[0] = Lighting::m_FirstConeLight;
	psConstants.FirstLightIndex[1] = Lighting::m_FirstConeShadowedLight;
	psConstants.FrameIndexMod2 = FrameIndex;

	// Set the default state for command lists
	auto& pfnSetupGraphicsState = [&](GraphicsContext* gfxContext)
	{
		gfxContext->SetRootSignature(m_RootSig);
		gfxContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		gfxContext->SetIndexBuffer(m_Model.m_IndexBuffer.IndexBufferView());
		gfxContext->SetVertexBuffer(0, m_Model.m_VertexBuffer.VertexBufferView());
	};

	pfnSetupGraphicsState(&leftGfxContext);
	pfnSetupGraphicsState(&rightGfxContext);
	pfnSetupGraphicsState(&centerGfxContext);

	RenderLightShadows(leftGfxContext, 0);
	RenderLightShadows(rightGfxContext, 1);
	RenderLightShadows(centerGfxContext, 2);

	{
		ScopedTimer _prof(L"Z PrePass", leftGfxContext);

		leftGfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);
		rightGfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);
		centerGfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);

		{
			ScopedTimer _prof(L"Opaque", leftGfxContext);
			{
				leftGfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
				rightGfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
				centerGfxContext.TransitionResource(g_SceneCenterDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
				leftGfxContext.ClearDepth(g_SceneDepthBuffer);
				rightGfxContext.ClearDepth(g_SceneDepthBuffer);
				centerGfxContext.ClearDepth(g_SceneCenterDepthBuffer);

				leftGfxContext.SetPipelineState(m_DepthPSO);
				rightGfxContext.SetPipelineState(m_DepthPSO);
				centerGfxContext.SetPipelineState(m_DepthPSO);

				leftGfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);
				rightGfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);
				centerGfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);
			}

			leftGfxContext.SetDepthStencilTarget(g_SceneDepthBuffer.GetDSV());
			RenderObjects(leftGfxContext, m_Camera[0]->GetViewProjMatrix(), 0, kOpaque);

			rightGfxContext.SetDepthStencilTarget(g_SceneDepthBuffer.GetDSV());
			RenderObjects(rightGfxContext, m_Camera[1]->GetViewProjMatrix(), 1, kOpaque);

			centerGfxContext.SetDepthStencilTarget(g_SceneCenterDepthBuffer.GetDSV());
			RenderObjects(centerGfxContext, m_Camera[2]->GetViewProjMatrix(), 2, kOpaque);
		}

		{
			ScopedTimer _prof(L"Cutout", leftGfxContext);
			{
				leftGfxContext.SetPipelineState(m_CutoutDepthPSO);
				rightGfxContext.SetPipelineState(m_CutoutDepthPSO);
				centerGfxContext.SetPipelineState(m_CutoutDepthPSO);
			}
			RenderObjects(leftGfxContext, m_Camera[0]->GetViewProjMatrix(), 0, kCutout);
			RenderObjects(rightGfxContext, m_Camera[1]->GetViewProjMatrix(), 1, kCutout);
			RenderObjects(centerGfxContext, m_Camera[2]->GetViewProjMatrix(), 2, kCutout);
		}
	}

	leftGfxContext.TransitionResource(g_SceneDepthBuffer,
	                                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	rightGfxContext.TransitionResource(g_SceneDepthBuffer,
	                                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	centerGfxContext.TransitionResource(g_SceneCenterDepthBuffer,
	                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	centerGfxContext.TransitionResource(g_SceneCenterColourDepthBuffer,
	                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

	{
		ComputeContext& cmpContext =
			ComputeContext::Begin(L"Combine Depth m_Buffers", true);

		cmpContext.SetRootSignature(m_ComputeRootSig);
		cmpContext.SetPipelineState(m_CombineDepthPSO);

		/*cmpContext.TransitionResource(g_SceneLeftDepthBuffer,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cmpContext.TransitionResource(g_SceneRightDepthBuffer,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cmpContext.TransitionResource(g_SceneCenterColourDepthBuffer,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);*/

		cmpContext.SetDynamicDescriptor(
			0, 0, g_SceneDepthBuffer.GetDepthSRV());
		cmpContext.SetDynamicDescriptor(
			1, 0, g_SceneDepthBuffer.GetDepthSRV());
		cmpContext.SetDynamicDescriptor(
			2, 0, g_SceneCenterColourDepthBuffer.GetUAV());

		cmpContext.Dispatch2D(g_SceneDepthBuffer.GetWidth(),
		                      g_SceneDepthBuffer.GetHeight());

		cmpContext.Finish();
	}

	SSAO::Render(leftGfxContext, *m_Camera[0], &g_SceneDepthBuffer);
	SSAO::Render(rightGfxContext, *m_Camera[1], &g_SceneDepthBuffer);
	SSAO::Render(centerGfxContext, *m_Camera[2], &g_SceneCenterDepthBuffer);

	if (!skipDiffusePass)
	{
		Lighting::FillLightGrid(leftGfxContext, *m_Camera[0], &g_SceneDepthBuffer);
		Lighting::FillLightGrid(rightGfxContext, *m_Camera[1], &g_SceneDepthBuffer);
		Lighting::FillLightGrid(centerGfxContext, *m_Camera[2], &g_SceneCenterDepthBuffer);

		if (!SSAO::DebugDraw)
		{
			ScopedTimer _prof(L"Main Render", leftGfxContext);
			{
				leftGfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
				leftGfxContext.TransitionResource(g_SceneNormalBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
				leftGfxContext.ClearColor(g_SceneColorBuffer);
			}
		}
	}

	if (!skipShadowMap)
	{
		if (!SSAO::DebugDraw)
		{
			pfnSetupGraphicsState(&leftGfxContext);
			pfnSetupGraphicsState(&rightGfxContext);
			pfnSetupGraphicsState(&centerGfxContext);
			{
				ScopedTimer _prof(L"Render Shadow Map", leftGfxContext);

				m_SunShadow.UpdateMatrix(-m_SunDirection, Vector3(0, -500.0f, 0),
				                         Vector3(ShadowDimX, ShadowDimY, ShadowDimZ),
				                         (uint32_t)g_ShadowBuffer.GetWidth(), (uint32_t)g_ShadowBuffer.GetHeight(), 16);


				g_ShadowBuffer.BeginRendering(leftGfxContext);
				g_ShadowBuffer.BeginRendering(rightGfxContext);
				g_ShadowBuffer.BeginRendering(centerGfxContext);
				leftGfxContext.SetPipelineState(m_ShadowPSO);
				rightGfxContext.SetPipelineState(m_ShadowPSO);
				centerGfxContext.SetPipelineState(m_ShadowPSO);
				RenderObjects(leftGfxContext, m_SunShadow.GetViewProjMatrix(), 0, kOpaque);
				RenderObjects(rightGfxContext, m_SunShadow.GetViewProjMatrix(), 1, kOpaque);
				RenderObjects(centerGfxContext, m_SunShadow.GetViewProjMatrix(), 2, kOpaque);
				leftGfxContext.SetPipelineState(m_CutoutShadowPSO);
				rightGfxContext.SetPipelineState(m_CutoutShadowPSO);
				centerGfxContext.SetPipelineState(m_CutoutShadowPSO);
				RenderObjects(leftGfxContext, m_SunShadow.GetViewProjMatrix(), 0, kCutout);
				RenderObjects(rightGfxContext, m_SunShadow.GetViewProjMatrix(), 1, kCutout);
				RenderObjects(centerGfxContext, m_SunShadow.GetViewProjMatrix(), 2, kCutout);
				g_ShadowBuffer.EndRendering(leftGfxContext);
				g_ShadowBuffer.EndRendering(rightGfxContext);
				g_ShadowBuffer.EndRendering(centerGfxContext);
			}
		}
	}

	if (!skipDiffusePass)
	{
		if (!SSAO::DebugDraw)
		{
			if (SSAO::AsyncCompute)
			{
				leftGfxContext.Flush();
				rightGfxContext.Flush();
				centerGfxContext.Flush();
				pfnSetupGraphicsState(&leftGfxContext);
				pfnSetupGraphicsState(&rightGfxContext);
				pfnSetupGraphicsState(&centerGfxContext);

				// Make the 3D queue wait for the Compute queue to finish SSAO
				g_CommandManager.GetGraphicsQueue().StallForProducer(g_CommandManager.GetComputeQueue());
			}

			auto renderColour = [=](GraphicsContext* gfxContext, int cam, DepthBuffer* curDepthBuf)
			{
				ScopedTimer _prof(L"Render Color", *gfxContext);

				gfxContext->TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

				gfxContext->SetDynamicConstantBufferView(
					1, sizeof(psConstants), &psConstants);
				gfxContext->SetDynamicDescriptors(
					3, 0, ARRAYSIZE(m_ExtraTextures), m_ExtraTextures);

				gfxContext->TransitionResource(g_SceneCenterColourDepthBuffer,
				                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, true);
				gfxContext->SetDynamicDescriptor(6, 0,
				                                 g_SceneCenterColourDepthBuffer.GetSRV());

				/*gfxContext->TransitionResource(g_SceneColorBuffer,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, true);
				gfxContext->SetDynamicDescriptor(7, 0,
					g_SceneColorBuffer.GetSRV());*/

				bool RenderIDs = !TemporalEffects::EnableTAA;


				{
					gfxContext->SetPipelineState(ShowWaveTileCounts ? m_WaveTileCountPSO : m_ModelPSO);

					gfxContext->TransitionResource(*curDepthBuf,
					                               D3D12_RESOURCE_STATE_DEPTH_READ);

					D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2];
					rtvs[0] = g_SceneColorBuffer.GetSubRTV(cam);
					rtvs[1] = g_SceneNormalBuffer.GetRTV();

					gfxContext->SetRenderTargets(ARRAYSIZE(rtvs), rtvs,
					                             curDepthBuf->GetDSV_DepthReadOnly());


					gfxContext->SetViewportAndScissor(
						m_MainViewport, m_MainScissor);

					RenderObjects(*gfxContext, m_Camera[cam]->GetViewProjMatrix(), cam, kOpaque);
				};


				if (!ShowWaveTileCounts)
				{
					gfxContext->SetPipelineState(m_CutoutModelPSO);
					RenderObjects(*gfxContext, m_Camera[0]->GetViewProjMatrix(), 0, kCutout);
					RenderObjects(*gfxContext, m_Camera[1]->GetViewProjMatrix(), 1, kCutout);
					RenderObjects(*gfxContext, m_Camera[2]->GetViewProjMatrix(), 2, kCutout);
				}
			};
			renderColour(&centerGfxContext, 2, &g_SceneCenterDepthBuffer);
			renderColour(&leftGfxContext, 0, &g_SceneDepthBuffer);
			renderColour(&rightGfxContext, 1, &g_SceneDepthBuffer);
		}

		// Some systems generate a per-pixel velocity buffer to better track dynamic and skinned meshes.  Everything
		// is static in our scene, so we generate velocity from camera motion and the depth buffer.  A velocity buffer
		// is necessary for all temporal effects (and motion blur).
		MotionBlur::GenerateCameraVelocityBuffer(leftGfxContext, *m_Camera[0], true);
		MotionBlur::GenerateCameraVelocityBuffer(rightGfxContext, *m_Camera[1], true);
		MotionBlur::GenerateCameraVelocityBuffer(centerGfxContext, *m_Camera[2], true);

		TemporalEffects::ResolveImage(leftGfxContext);
		TemporalEffects::ResolveImage(rightGfxContext);
		TemporalEffects::ResolveImage(centerGfxContext);

		ParticleEffects::Render(leftGfxContext, *m_Camera[0], g_SceneColorBuffer, g_SceneDepthBuffer,
		                        g_LinearDepth[FrameIndex]);
		ParticleEffects::Render(rightGfxContext, *m_Camera[1], g_SceneColorBuffer, g_SceneDepthBuffer,
		                        g_LinearDepth[FrameIndex]);
		ParticleEffects::Render(centerGfxContext, *m_Camera[2], g_SceneColorBuffer, g_SceneCenterDepthBuffer,
		                        g_LinearDepth[FrameIndex]);

		// Until I work out how to couple these two, it's "either-or".
		if (DepthOfField::Enable)
		{
			DepthOfField::Render(leftGfxContext, m_Camera[0]->GetNearClip(), m_Camera[0]->GetFarClip());
			DepthOfField::Render(rightGfxContext, m_Camera[1]->GetNearClip(), m_Camera[1]->GetFarClip());
			DepthOfField::Render(centerGfxContext, m_Camera[2]->GetNearClip(), m_Camera[2]->GetFarClip());
		}
		else
		{
			MotionBlur::RenderObjectBlur(leftGfxContext, g_VelocityBuffer);
			MotionBlur::RenderObjectBlur(rightGfxContext, g_VelocityBuffer);
			MotionBlur::RenderObjectBlur(centerGfxContext, g_VelocityBuffer);
		}
	}

	if (g_RayTraceSupport && rayTracingMode != RTM_OFF)
	{
		Raytrace(leftGfxContext, 0, &g_SceneDepthBuffer);
		Raytrace(rightGfxContext, 1, &g_SceneDepthBuffer);
		Raytrace(centerGfxContext, 2, &g_SceneCenterDepthBuffer);
	}

	// Albert
	RenderCenterViewToEye(leftGfxContext, 0);
	RenderCenterViewToEye(rightGfxContext, 1);

	leftGfxContext.Finish();
	rightGfxContext.Finish();
	centerGfxContext.Finish();
}

//
// Tests traversal
//

void Raytracebarycentrics(
	CommandContext& context,
	const Math::Camera& camera,
	ColorBuffer& colorTarget)
{
	ScopedTimer _p0(L"Raytracing barycentrics", context);

	// Prepare constants
	DynamicCB inputs = g_dynamicCb;
	auto m0 = camera.GetViewProjMatrix();
	auto m1 = Transpose(Invert(m0));
	memcpy(&inputs.cameraToWorld, &m1, sizeof(inputs.cameraToWorld));
	memcpy(&inputs.worldCameraPosition, &camera.GetPosition(), sizeof(inputs.worldCameraPosition));
	inputs.resolution.x = (float)colorTarget.GetWidth();
	inputs.resolution.y = (float)colorTarget.GetHeight();

	HitShaderConstants hitShaderConstants = {};
	hitShaderConstants.IsReflection = false;
	context.WriteBuffer(g_hitConstantBuffer, 0, &hitShaderConstants, sizeof(hitShaderConstants));

	context.WriteBuffer(g_dynamicConstantBuffer, 0, &inputs, sizeof(inputs));

	context.TransitionResource(g_hitConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	context.TransitionResource(g_dynamicConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	context.TransitionResource(colorTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.FlushResourceBarriers();

	ID3D12GraphicsCommandList* pCommandList = context.GetCommandList();

	CComPtr<ID3D12GraphicsCommandList4> pRaytracingCommandList;
	pCommandList->QueryInterface(IID_PPV_ARGS(&pRaytracingCommandList));

	ID3D12DescriptorHeap* pDescriptorHeaps[] = {&g_pRaytracingDescriptorHeap->GetDescriptorHeap()};
	pRaytracingCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);

	pCommandList->SetComputeRootSignature(g_GlobalRaytracingRootSignature);
	pCommandList->SetComputeRootDescriptorTable(0, g_SceneSrvs);
	pCommandList->SetComputeRootConstantBufferView(1, g_hitConstantBuffer.GetGpuVirtualAddress());
	pCommandList->SetComputeRootConstantBufferView(2, g_dynamicConstantBuffer.GetGpuVirtualAddress());
	pCommandList->SetComputeRootDescriptorTable(4, g_OutputUAV);
	pRaytracingCommandList->SetComputeRootShaderResourceView(
		7, g_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress());

	D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = g_RaytracingInputs[Primarybarycentric].GetDispatchRayDesc(
		colorTarget.GetWidth(), colorTarget.GetHeight());
	pRaytracingCommandList->SetPipelineState1(g_RaytracingInputs[Primarybarycentric].m_pPSO);
	pRaytracingCommandList->DispatchRays(&dispatchRaysDesc);
}

void RaytracebarycentricsSSR(
	CommandContext& context,
	const Math::Camera& camera,
	ColorBuffer& colorTarget,
	DepthBuffer& depth,
	ColorBuffer& normals)
{
	ScopedTimer _p0(L"Raytracing SSR barycentrics", context);

	DynamicCB inputs = g_dynamicCb;
	auto m0 = camera.GetViewProjMatrix();
	auto m1 = Transpose(Invert(m0));
	memcpy(&inputs.cameraToWorld, &m1, sizeof(inputs.cameraToWorld));
	memcpy(&inputs.worldCameraPosition, &camera.GetPosition(), sizeof(inputs.worldCameraPosition));
	inputs.resolution.x = (float)colorTarget.GetWidth();
	inputs.resolution.y = (float)colorTarget.GetHeight();

	HitShaderConstants hitShaderConstants = {};
	hitShaderConstants.IsReflection = false;
	context.WriteBuffer(g_hitConstantBuffer, 0, &hitShaderConstants, sizeof(hitShaderConstants));

	ComputeContext& ctx = context.GetComputeContext();
	ID3D12GraphicsCommandList* pCommandList = context.GetCommandList();

	ctx.WriteBuffer(g_dynamicConstantBuffer, 0, &inputs, sizeof(inputs));
	ctx.TransitionResource(g_dynamicConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	ctx.TransitionResource(g_hitConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	ctx.TransitionResource(normals, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(g_ShadowBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(colorTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	ctx.FlushResourceBarriers();

	CComPtr<ID3D12GraphicsCommandList4> pRaytracingCommandList;
	pCommandList->QueryInterface(IID_PPV_ARGS(&pRaytracingCommandList));

	ID3D12DescriptorHeap* pDescriptorHeaps[] = {&g_pRaytracingDescriptorHeap->GetDescriptorHeap()};
	pRaytracingCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);

	pCommandList->SetComputeRootSignature(g_GlobalRaytracingRootSignature);
	pCommandList->SetComputeRootConstantBufferView(1, g_hitConstantBuffer.GetGpuVirtualAddress());
	pCommandList->SetComputeRootConstantBufferView(2, g_dynamicConstantBuffer.GetGpuVirtualAddress());
	pCommandList->SetComputeRootDescriptorTable(4, g_OutputUAV);
	pCommandList->SetComputeRootDescriptorTable(3, g_DepthAndNormalsTable);
	pRaytracingCommandList->SetComputeRootShaderResourceView(
		7, g_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress());

	D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = g_RaytracingInputs[Reflectionbarycentric].GetDispatchRayDesc(
		colorTarget.GetWidth(), colorTarget.GetHeight());
	pRaytracingCommandList->SetPipelineState1(g_RaytracingInputs[Reflectionbarycentric].m_pPSO);
	pRaytracingCommandList->DispatchRays(&dispatchRaysDesc);
}

void D3D12RaytracingMiniEngineSample::RaytraceShadows(
	GraphicsContext& context,
	const Math::Camera& camera,
	ColorBuffer& colorTarget,
	DepthBuffer& depth)
{
	ScopedTimer _p0(L"Raytracing Shadows", context);

	DynamicCB inputs = g_dynamicCb;
	auto m0 = camera.GetViewProjMatrix();
	auto m1 = Transpose(Invert(m0));
	memcpy(&inputs.cameraToWorld, &m1, sizeof(inputs.cameraToWorld));
	memcpy(&inputs.worldCameraPosition, &camera.GetPosition(), sizeof(inputs.worldCameraPosition));
	inputs.resolution.x = (float)colorTarget.GetWidth();
	inputs.resolution.y = (float)colorTarget.GetHeight();

	HitShaderConstants hitShaderConstants = {};
	hitShaderConstants.sunDirection = m_SunDirection;
	hitShaderConstants.sunLight = Vector3(1.0f, 1.0f, 1.0f) * m_SunLightIntensity;
	hitShaderConstants.ambientLight = Vector3(1.0f, 1.0f, 1.0f) * m_AmbientIntensity;
	hitShaderConstants.ShadowTexelSize[0] = 1.0f / g_ShadowBuffer.GetWidth();
	hitShaderConstants.modelToShadow = m_SunShadow.GetShadowMatrix();
	hitShaderConstants.IsReflection = false;
	hitShaderConstants.UseShadowRays = false;
	context.WriteBuffer(g_hitConstantBuffer, 0, &hitShaderConstants, sizeof(hitShaderConstants));

	ComputeContext& ctx = context.GetComputeContext();
	ID3D12GraphicsCommandList* pCommandList = context.GetCommandList();

	ctx.WriteBuffer(g_dynamicConstantBuffer, 0, &inputs, sizeof(inputs));
	ctx.TransitionResource(g_dynamicConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	ctx.TransitionResource(g_SceneNormalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(g_hitConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	ctx.TransitionResource(depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(g_ShadowBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(colorTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	ctx.FlushResourceBarriers();

	CComPtr<ID3D12GraphicsCommandList4> pRaytracingCommandList;
	pCommandList->QueryInterface(IID_PPV_ARGS(&pRaytracingCommandList));

	ID3D12DescriptorHeap* pDescriptorHeaps[] = {&g_pRaytracingDescriptorHeap->GetDescriptorHeap()};
	pRaytracingCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);

	pCommandList->SetComputeRootSignature(g_GlobalRaytracingRootSignature);
	pCommandList->SetComputeRootConstantBufferView(1, g_hitConstantBuffer.GetGpuVirtualAddress());
	pCommandList->SetComputeRootConstantBufferView(2, g_dynamicConstantBuffer->GetGPUVirtualAddress());
	pCommandList->SetComputeRootDescriptorTable(4, g_OutputUAV);
	pCommandList->SetComputeRootDescriptorTable(3, g_DepthAndNormalsTable);
	pRaytracingCommandList->SetComputeRootShaderResourceView(
		7, g_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress());

	D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = g_RaytracingInputs[Shadows].GetDispatchRayDesc(
		colorTarget.GetWidth(), colorTarget.GetHeight());
	pRaytracingCommandList->SetPipelineState1(g_RaytracingInputs[Shadows].m_pPSO);
	pRaytracingCommandList->DispatchRays(&dispatchRaysDesc);
}

void D3D12RaytracingMiniEngineSample::RaytraceDiffuse(
	GraphicsContext& context,
	const Math::Camera& camera,
	ColorBuffer& colorTarget)
{
	ScopedTimer _p0(L"RaytracingWithHitShader", context);

	// Prepare constants
	DynamicCB inputs = g_dynamicCb;
	auto m0 = camera.GetViewProjMatrix();
	auto m1 = Transpose(Invert(m0));
	memcpy(&inputs.cameraToWorld, &m1, sizeof(inputs.cameraToWorld));
	memcpy(&inputs.worldCameraPosition, &camera.GetPosition(), sizeof(inputs.worldCameraPosition));
	inputs.resolution.x = (float)colorTarget.GetWidth();
	inputs.resolution.y = (float)colorTarget.GetHeight();

	HitShaderConstants hitShaderConstants = {};
	hitShaderConstants.sunDirection = m_SunDirection;
	hitShaderConstants.sunLight = Vector3(1.0f, 1.0f, 1.0f) * m_SunLightIntensity;
	hitShaderConstants.ambientLight = Vector3(1.0f, 1.0f, 1.0f) * m_AmbientIntensity;
	hitShaderConstants.ShadowTexelSize[0] = 1.0f / g_ShadowBuffer.GetWidth();
	hitShaderConstants.modelToShadow = Transpose(m_SunShadow.GetShadowMatrix());
	hitShaderConstants.IsReflection = false;
	hitShaderConstants.UseShadowRays = rayTracingMode == RTM_DIFFUSE_WITH_SHADOWRAYS;
	context.WriteBuffer(g_hitConstantBuffer, 0, &hitShaderConstants, sizeof(hitShaderConstants));
	context.WriteBuffer(g_dynamicConstantBuffer, 0, &inputs, sizeof(inputs));

	context.TransitionResource(g_dynamicConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	context.TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(g_hitConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	context.TransitionResource(g_ShadowBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(colorTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.FlushResourceBarriers();

	ID3D12GraphicsCommandList* pCommandList = context.GetCommandList();

	CComPtr<ID3D12GraphicsCommandList4> pRaytracingCommandList;
	pCommandList->QueryInterface(IID_PPV_ARGS(&pRaytracingCommandList));

	ID3D12DescriptorHeap* pDescriptorHeaps[] = {&g_pRaytracingDescriptorHeap->GetDescriptorHeap()};
	pRaytracingCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);

	pCommandList->SetComputeRootSignature(g_GlobalRaytracingRootSignature);
	pCommandList->SetComputeRootDescriptorTable(0, g_SceneSrvs);
	pCommandList->SetComputeRootConstantBufferView(1, g_hitConstantBuffer.GetGpuVirtualAddress());
	pCommandList->SetComputeRootConstantBufferView(2, g_dynamicConstantBuffer.GetGpuVirtualAddress());
	pCommandList->SetComputeRootDescriptorTable(4, g_OutputUAV);
	pRaytracingCommandList->SetComputeRootShaderResourceView(
		7, g_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress());

	D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = g_RaytracingInputs[DiffuseHitShader].GetDispatchRayDesc(
		colorTarget.GetWidth(), colorTarget.GetHeight());
	pRaytracingCommandList->SetPipelineState1(g_RaytracingInputs[DiffuseHitShader].m_pPSO);
	pRaytracingCommandList->DispatchRays(&dispatchRaysDesc);
}

void D3D12RaytracingMiniEngineSample::RaytraceReflections(
	GraphicsContext& context,
	const Math::Camera& camera,
	ColorBuffer& colorTarget,
	DepthBuffer& depth,
	ColorBuffer& normals)
{
	ScopedTimer _p0(L"RaytracingWithHitShader", context);

	// Prepare constants
	DynamicCB inputs = g_dynamicCb;
	auto m0 = camera.GetViewProjMatrix();
	auto m1 = Transpose(Invert(m0));
	memcpy(&inputs.cameraToWorld, &m1, sizeof(inputs.cameraToWorld));
	memcpy(&inputs.worldCameraPosition, &camera.GetPosition(), sizeof(inputs.worldCameraPosition));
	inputs.resolution.x = (float)colorTarget.GetWidth();
	inputs.resolution.y = (float)colorTarget.GetHeight();

	HitShaderConstants hitShaderConstants = {};
	hitShaderConstants.sunDirection = m_SunDirection;
	hitShaderConstants.sunLight = Vector3(1.0f, 1.0f, 1.0f) * m_SunLightIntensity;
	hitShaderConstants.ambientLight = Vector3(1.0f, 1.0f, 1.0f) * m_AmbientIntensity;
	hitShaderConstants.ShadowTexelSize[0] = 1.0f / g_ShadowBuffer.GetWidth();
	hitShaderConstants.modelToShadow = Transpose(m_SunShadow.GetShadowMatrix());
	hitShaderConstants.IsReflection = true;
	hitShaderConstants.UseShadowRays = false;
	context.WriteBuffer(g_hitConstantBuffer, 0, &hitShaderConstants, sizeof(hitShaderConstants));
	context.WriteBuffer(g_dynamicConstantBuffer, 0, &inputs, sizeof(inputs));

	context.TransitionResource(g_dynamicConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	context.TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(g_ShadowBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(normals, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(g_hitConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	context.TransitionResource(colorTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.FlushResourceBarriers();

	ID3D12GraphicsCommandList* pCommandList = context.GetCommandList();

	CComPtr<ID3D12GraphicsCommandList4> pRaytracingCommandList;
	pCommandList->QueryInterface(IID_PPV_ARGS(&pRaytracingCommandList));

	ID3D12DescriptorHeap* pDescriptorHeaps[] = {&g_pRaytracingDescriptorHeap->GetDescriptorHeap()};
	pRaytracingCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);

	pCommandList->SetComputeRootSignature(g_GlobalRaytracingRootSignature);
	pCommandList->SetComputeRootDescriptorTable(0, g_SceneSrvs);
	pCommandList->SetComputeRootConstantBufferView(1, g_hitConstantBuffer.GetGpuVirtualAddress());
	pCommandList->SetComputeRootConstantBufferView(2, g_dynamicConstantBuffer.GetGpuVirtualAddress());
	pCommandList->SetComputeRootDescriptorTable(3, g_DepthAndNormalsTable);
	pCommandList->SetComputeRootDescriptorTable(4, g_OutputUAV);
	pRaytracingCommandList->SetComputeRootShaderResourceView(
		7, g_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress());

	D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = g_RaytracingInputs[Reflection].GetDispatchRayDesc(
		colorTarget.GetWidth(), colorTarget.GetHeight());
	pRaytracingCommandList->SetPipelineState1(g_RaytracingInputs[Reflection].m_pPSO);
	pRaytracingCommandList->DispatchRays(&dispatchRaysDesc);
}

void D3D12RaytracingMiniEngineSample::RenderUI(class GraphicsContext& gfxContext)
{
	const UINT framesToAverage = 20;
	static float frameRates[framesToAverage] = {};
	frameRates[Graphics::GetFrameCount() % framesToAverage] = Graphics::GetFrameRate();
	float rollingAverageFrameRate = 0.0;
	for (auto frameRate : frameRates)
	{
		rollingAverageFrameRate += frameRate / framesToAverage;
	}

	float primaryRaysPerSec = g_SceneColorBuffer.GetWidth() * g_SceneColorBuffer.GetHeight() * rollingAverageFrameRate /
		(1000000.0f);
	TextContext text(gfxContext);
	text.Begin();
	text.DrawFormattedString("\nMillion Primary Rays/s: %7.3f", primaryRaysPerSec);
	text.End();
}

void D3D12RaytracingMiniEngineSample::Raytrace(
	class GraphicsContext& gfxContext, UINT cam, DepthBuffer* curDepthBuf)
{
	ScopedTimer _prof(L"Raytrace", gfxContext);

	g_dynamicCb.curCam = cam;

	gfxContext.TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	uint32_t FrameIndex = TemporalEffects::GetFrameIndexMod2();

	switch (rayTracingMode)
	{
	case RTM_TRAVERSAL:
		Raytracebarycentrics(gfxContext, *m_Camera[cam], g_SceneColorBuffer);
		break;

	case RTM_SSR:
		RaytracebarycentricsSSR(gfxContext, *m_Camera[cam], g_SceneColorBuffer, *curDepthBuf,
		                        g_SceneNormalBuffer);
		break;

	case RTM_SHADOWS:
		RaytraceShadows(gfxContext, *m_Camera[cam], g_SceneColorBuffer, *curDepthBuf);
		break;

	case RTM_DIFFUSE_WITH_SHADOWMAPS:
	case RTM_DIFFUSE_WITH_SHADOWRAYS:
		RaytraceDiffuse(gfxContext, *m_Camera[cam], g_SceneColorBuffer);
		break;

	case RTM_REFLECTIONS:
		RaytraceReflections(gfxContext, *m_Camera[cam], g_SceneColorBuffer, *curDepthBuf, g_SceneNormalBuffer);
		break;
	}

	// Clear the gfxContext's descriptor heap since ray tracing changes this underneath the sheets
	gfxContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, nullptr);
}
