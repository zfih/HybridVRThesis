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
#include "Settings.h"

#include "Raytracing.h"
#include "DescriptorHeapStack.h"

// Shaders
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

#include "CompiledShaders/LowPassCS.h"
#include "CompiledShaders/DownsampleCS.h"
#include "CompiledShaders/FrameIntegrationCS.h"

#include "RaytracingHlslCompat.h"
#include "ModelViewerRayTracing.h"

#include <iostream>
#include <fstream>
#include <iso646.h>


#include "GlobalState.h"

using namespace GameCore;
using namespace Math;
using namespace Graphics;

#define ASSET_DIRECTORY "../MiniEngine/ModelViewer/"


extern ByteAddressBuffer g_bvh_bottomLevelAccelerationStructure;
ColorBuffer g_SceneNormalBufferFullRes;
ColorBuffer g_SceneNormalBufferLowRes;

ColorBuffer& SceneNormalBuffer()
{
	if (Graphics::GetFrameCount() % 2 == 1)
	{
		return g_SceneNormalBufferLowRes;
	}
	else
	{
		return g_SceneNormalBufferFullRes;
	}
}

Camera* LODGlobal::g_camera = nullptr;
CameraController* LODGlobal::g_cameraController = nullptr;

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

D3D12_GPU_DESCRIPTOR_HANDLE *g_GpuSceneMaterialSrvs;
D3D12_CPU_DESCRIPTOR_HANDLE g_SceneMeshInfo;
D3D12_CPU_DESCRIPTOR_HANDLE g_SceneIndices;

D3D12_GPU_DESCRIPTOR_HANDLE g_OutputUAV;
D3D12_GPU_DESCRIPTOR_HANDLE g_DepthAndNormalsTable;
D3D12_GPU_DESCRIPTOR_HANDLE g_SceneSrvs;

std::vector<CComPtr<ID3D12Resource>> g_bvh_bottomLevelAccelerationStructures;
CComPtr<ID3D12Resource> g_bvh_topLevelAccelerationStructure;

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

//// SCENE

enum class Scene
{
	kBistro = 0,
	kSponza,
	
	kCount,
	kUnknown
};

struct SceneData
{
	Scene Scene;
	Matrix4 Matrix;
	std::string ModelPath;
	std::wstring TextureFolderPath;
	std::vector<std::string> Reflective;
	std::vector<std::string> CutOuts;
};

SceneData g_Scene {};

void g_CreateScene(Scene Scene)
{
	g_Scene.Scene = Scene;

	switch (Scene)
	{
	case Scene::kBistro: {
		g_Scene.Matrix = Matrix4::MakeRotationX(-XM_PIDIV2);
		g_Scene.ModelPath = ASSET_DIRECTORY "Models/bistro.h3d";
		g_Scene.TextureFolderPath = ASSET_DIRECTORY L"Textures/bistro/";
		g_Scene.Reflective = { "floor", "glass", "metal" };
		g_Scene.CutOuts = {  };
	} break;
	case Scene::kSponza:
	{
		g_Scene.Matrix = Matrix4(XMMatrixIdentity());
		g_Scene.ModelPath = ASSET_DIRECTORY "Models/sponza.h3d";
		g_Scene.TextureFolderPath = ASSET_DIRECTORY L"Textures/sponza/";
		g_Scene.Reflective = { "floor" };
		g_Scene.CutOuts = { "thorn", "plant", "chain" };
	} break;
	default:
		g_CreateScene(Scene::kSponza);
		break;

	}
}

// SCENE END

static const int MAX_RT_DESCRIPTORS = 200;

struct MaterialRootConstant
{
	UINT MaterialID;
};

RaytracingDispatchRayInputs g_RaytracingInputs[RaytracingTypes::NumTypes];

// TODO: Oh boi
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
	virtual void RenderShadowMap() override;
	virtual void RenderScene(UINT cam) override;
	virtual void FrameIntegration() override;
	virtual void RenderUI(class GraphicsContext&) override;
	virtual void Raytrace(class GraphicsContext&, UINT cam);

	void SetCameraToPredefinedPosition(int cameraPosition);

private:

	void CreateRayTraceAccelerationStructures(UINT numMeshes);

	void RenderLightShadows(GraphicsContext& gfxContext, UINT curCam);

	enum eObjectFilter { kOpaque = 0x1, kCutout = 0x2, kTransparent = 0x4, kAll = 0xF, kNone = 0x0 };
	void RenderObjects(GraphicsContext& Context, UINT CurCam, const Matrix4& ViewProjMat, eObjectFilter Filter = kAll);
	void RaytraceDiffuse(GraphicsContext& context, UINT CurCam, ColorBuffer& colorTarget);
	void RaytraceShadows(GraphicsContext& context, UINT CurCam, ColorBuffer& colorTarget,
	                     DepthBuffer& depth);
	void RaytraceReflections(GraphicsContext& context, UINT CurCam, ColorBuffer& colorTarget,
	                         DepthBuffer& depth, ColorBuffer& normals);

	void SaveCamPos();
	void LoadCamPos();
	const char* m_CamPosFilename = "SavedCamPos.txt";
	const int m_CamPosCount = 5;

	VRCamera m_Camera;
	std::auto_ptr<VRCameraController> m_CameraController;
	D3D12_VIEWPORT m_MainViewport;
	D3D12_RECT m_MainScissor;

	RootSignature m_RootSig;
	GraphicsPSO m_DepthPSO[1];
	GraphicsPSO m_CutoutDepthPSO[1];
	GraphicsPSO m_ModelPSO[1];
	GraphicsPSO m_CutoutModelPSO[1];
	GraphicsPSO m_ShadowPSO;
	GraphicsPSO m_CutoutShadowPSO;
	GraphicsPSO m_WaveTileCountPSO;

	RootSignature m_LowPassSig;
	ComputePSO m_LowPassPSO;
	ComputePSO m_DownsamplePSO;
	RootSignature m_FrameIntegrationSig;
	ComputePSO m_FrameIntegrationPSO;

	D3D12_CPU_DESCRIPTOR_HANDLE m_DefaultSampler;
	D3D12_CPU_DESCRIPTOR_HANDLE m_ShadowSampler;
	D3D12_CPU_DESCRIPTOR_HANDLE m_BiasedDefaultSampler;

	D3D12_CPU_DESCRIPTOR_HANDLE m_ExtraTextures[6];
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


int wmain(int argc, wchar_t** argv)
{
	g_CreateScene(Scene::kSponza);
	
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


	Settings::EnableVSync.Decrement();

	g_DisplayWidth = 1280;
	g_DisplayHeight = 720;
	GameCore::RunApplication(D3D12RaytracingMiniEngineSample(validDeviceFound), L"D3D12RaytracingMiniEngineSample");
	return 0;
}

namespace Settings
{
	const char* rayTracingModes[] = {
	"Off",
	"Bary Rays",
	"Refl Bary",
	"Shadow Rays",
	"Diffuse & ShadowMaps",
	"Diffuse & ShadowRays",
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
	
	ExpVar SunLightIntensity("Application/Lighting/Sun Light Intensity", 4.0f, 0.0f, 16.0f, 0.1f);
	ExpVar AmbientIntensity("Application/Lighting/Ambient Intensity", 0.1f, -16.0f, 16.0f, 0.1f);
	NumVar SunOrientation("Application/Lighting/Sun Orientation", -0.5f, -100.0f, 100.0f, 0.1f);
	NumVar SunInclination("Application/Lighting/Sun Inclination", 0.75f, 0.0f, 1.0f, 0.01f);
	NumVar ShadowDimX("Application/Lighting/Shadow Dim X", 5000, 1000, 10000, 100);
	NumVar ShadowDimY("Application/Lighting/Shadow Dim Y", 3000, 1000, 10000, 100);
	NumVar ShadowDimZ("Application/Lighting/Shadow Dim Z", 3000, 1000, 10000, 100);


	BoolVar ShowWaveTileCounts("Application/Forward+/Show Wave Tile Counts", false);

	EnumVar RayTracingMode("Application/Raytracing/RayTraceMode", RTM_DIFFUSE_WITH_SHADOWMAPS, _countof(rayTracingModes), rayTracingModes);
}

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
		RayTraceMeshInfo& data = meshInfoData[i];
		Model::Mesh& mesh = model.m_pMesh[i];
		
		data.m_indexOffsetBytes = mesh.indexDataByteOffset;

		const auto offset = [&](const int att) -> uint
		{
			return mesh.vertexDataByteOffset + mesh.attrib[att].offset;
		};
		
		data.m_positionAttributeOffsetBytes = offset(Model::attrib_position);
		data.m_normalAttributeOffsetBytes = offset(Model::attrib_normal);
		data.m_tangentAttributeOffsetBytes = offset(Model::attrib_tangent);
		data.m_bitangentAttributeOffsetBytes = offset(Model::attrib_tangent);
		data.m_uvAttributeOffsetBytes = offset(Model::attrib_texcoord0);

		data.m_materialInstanceId = mesh.materialIndex;
		data.m_attributeStrideBytes = mesh.vertexStride;
		ASSERT(data.m_materialInstanceId < model.m_Header.materialCount);
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
	g_GpuSceneMaterialSrvs = new D3D12_GPU_DESCRIPTOR_HANDLE[model.m_Header.materialCount];
	g_pRaytracingDescriptorHeap->AllocateDescriptor(uavHandle, uavDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, uavHandle, SceneColorBuffer()->GetUAV(),
	                                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	g_OutputUAV = g_pRaytracingDescriptorHeap->GetGpuHandle(uavDescriptorIndex);

	{
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
		UINT srvDescriptorIndex;
		g_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, SceneDepthBuffer()->GetDepthSRV(),
		                                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		g_DepthAndNormalsTable = g_pRaytracingDescriptorHeap->GetGpuHandle(srvDescriptorIndex);

		UINT unused;
		g_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, unused);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, SceneNormalBuffer().GetSRV(),
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
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, SSAOFullScreen()->GetSRV(),
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

	CD3DX12_ROOT_PARAMETER1 globalRootSignatureParameters[8];
	globalRootSignatureParameters[0].InitAsDescriptorTable(1, &sceneBuffersDescriptorRange);
	globalRootSignatureParameters[1].InitAsConstantBufferView(0);
	globalRootSignatureParameters[2].InitAsConstantBufferView(1);
	globalRootSignatureParameters[3].InitAsDescriptorTable(1, &srvDescriptorRange);
	globalRootSignatureParameters[4].InitAsDescriptorTable(1, &uavDescriptorRange);
	globalRootSignatureParameters[5].InitAsUnorderedAccessView(0);
	globalRootSignatureParameters[6].InitAsUnorderedAccessView(1);
	globalRootSignatureParameters[7].InitAsShaderResourceView(0);
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

// Returns true if string s
bool string_contains_any_from(
	const std::string &string, 
	const std::vector<std::string>& words)
{
	std::string allLower = string;
    std::transform(allLower.begin(), allLower.end(), allLower.begin(), ::tolower);
	
	return std::any_of(words.begin(), words.end(), [&](auto word) {return allLower.find(word) != std::string::npos; });
}


void set_hard_coded_material_properties(
	Model &Model, 
	std::vector<bool> &Cutout, 
	std::vector<bool> &Reflective)
{
	Cutout.resize(Model.m_Header.materialCount);
	Reflective.resize(Model.m_Header.materialCount);

	for (uint32_t i = 0; i < Model.m_Header.materialCount; ++i)
	{
		const std::string path = Model.m_pMaterial[i].texDiffusePath;

		Reflective[i] = string_contains_any_from(path, g_Scene.Reflective);

		Cutout[i] = string_contains_any_from(path, g_Scene.CutOuts);
	}

}

void D3D12RaytracingMiniEngineSample::Startup(void)
{
	//m_Camera = m_VRCamera[VRCamera::CENTER];

	Settings::RayTracingMode = Settings::RTM_OFF;

    ThrowIfFailed(g_Device->QueryInterface(IID_PPV_ARGS(&g_pRaytracingDevice)), L"Couldn't get DirectX Raytracing interface for the device.\n");
    g_SceneNormalBufferFullRes.CreateArray(L"Main Normal Buffer", SceneColorBuffer()->GetWidth(), SceneColorBuffer()->GetHeight(), 2, DXGI_FORMAT_R8G8B8A8_UNORM);
    g_SceneNormalBufferLowRes.CreateArray(L"Main Normal Buffer", Graphics::divisionHelperFunc(SceneColorBuffer()->GetWidth()), 
										  Graphics::divisionHelperFunc(SceneColorBuffer()->GetHeight()), 2, DXGI_FORMAT_R8G8B8A8_UNORM);

    g_pRaytracingDescriptorHeap = std::unique_ptr<DescriptorHeapStack>(
        new DescriptorHeapStack(*g_Device, MAX_RT_DESCRIPTORS, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 0));

    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1;
    HRESULT hr = g_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1));

    SamplerDesc DefaultSamplerDesc;
    DefaultSamplerDesc.MaxAnisotropy = 8;

    m_RootSig.Reset(6, 2);
    m_RootSig.InitStaticSampler(0, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig.InitStaticSampler(1, SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
    m_RootSig[1].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 6, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 64, 6, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig[4].InitAsConstants(1, 2, D3D12_SHADER_VISIBILITY_VERTEX);
    m_RootSig[5].InitAsConstants(1, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig.Finalize(L"D3D12RaytracingMiniEngineSample", 
					   D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    DXGI_FORMAT ColorFormat = SceneColorBuffer()->GetFormat();
    DXGI_FORMAT NormalFormat = g_SceneNormalBufferFullRes.GetFormat();
    DXGI_FORMAT DepthFormat = SceneDepthBuffer()->GetFormat();
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
    m_DepthPSO[0].SetRootSignature(m_RootSig);
    m_DepthPSO[0].SetRasterizerState(RasterizerDefault);
    m_DepthPSO[0].SetBlendState(BlendNoColorWrite);
    m_DepthPSO[0].SetDepthStencilState(DepthReadWriteStencilReadState);
    m_DepthPSO[0].SetInputLayout(_countof(vertElem), vertElem);
    m_DepthPSO[0].SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    m_DepthPSO[0].SetRenderTargetFormats(0, nullptr, DepthFormat);
    m_DepthPSO[0].SetVertexShader(g_pDepthViewerVS, sizeof(g_pDepthViewerVS));

    // Make a copy of the desc before we mess with it
    m_CutoutDepthPSO[0] = m_DepthPSO[0];
    m_ShadowPSO = m_DepthPSO[0];

    m_DepthPSO[0].Finalize();

    // Depth-only shading but with alpha testing

    m_CutoutDepthPSO[0].SetPixelShader(g_pDepthViewerPS, sizeof(g_pDepthViewerPS));
    m_CutoutDepthPSO[0].SetRasterizerState(RasterizerTwoSided);
    m_CutoutDepthPSO[0].Finalize();

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
    m_ModelPSO[0] = m_DepthPSO[0];
    m_ModelPSO[0].SetBlendState(BlendDisable);
    m_ModelPSO[0].SetDepthStencilState(DepthStateTestEqual);
    DXGI_FORMAT formats[] { ColorFormat, ColorFormat, NormalFormat };
    m_ModelPSO[0].SetRenderTargetFormats(_countof(formats), formats, DepthFormat);
    m_ModelPSO[0].SetVertexShader(g_pModelViewerVS, sizeof(g_pModelViewerVS));
    m_ModelPSO[0].SetPixelShader(g_pModelViewerPS, sizeof(g_pModelViewerPS));
    m_ModelPSO[0].Finalize();

    m_CutoutModelPSO[0] = m_ModelPSO[0];
    m_CutoutModelPSO[0].SetRasterizerState(RasterizerTwoSided);
    m_CutoutModelPSO[0].Finalize();

    // A debug shader for counting lights in a tile
    m_WaveTileCountPSO = m_ModelPSO[0];
    m_WaveTileCountPSO.SetPixelShader(g_pWaveTileCountPS, sizeof(g_pWaveTileCountPS));
    m_WaveTileCountPSO.Finalize();

    Lighting::InitializeResources();

    //m_ExtraTextures[0] = SSAOFullScreen()->GetSRV();
    m_ExtraTextures[1] = g_ShadowBuffer.GetSRV();

	m_LowPassSig.Reset(2, 1);
	m_LowPassSig[0].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
	m_LowPassSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
	m_LowPassSig.InitStaticSampler(0, SamplerLinearWrapDesc);
	m_LowPassSig.Finalize(L"LowPassSignature", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	m_LowPassPSO.SetRootSignature(m_LowPassSig);
	m_LowPassPSO.SetComputeShader(g_pLowPassCS, sizeof(g_pLowPassCS));
	m_LowPassPSO.Finalize();

	m_DownsamplePSO.SetRootSignature(m_LowPassSig);
	m_DownsamplePSO.SetComputeShader(g_pDownsampleCS, sizeof(g_pDownsampleCS));
	m_DownsamplePSO.Finalize();

	m_FrameIntegrationSig.Reset(4, 1);
	m_FrameIntegrationSig[0].InitAsConstants(0, 1);
	m_FrameIntegrationSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
	m_FrameIntegrationSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
	m_FrameIntegrationSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
	m_FrameIntegrationSig.InitStaticSampler(0, SamplerLinearWrapDesc);
	m_FrameIntegrationSig.Finalize(L"FrameIntegrationSignature", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	m_FrameIntegrationPSO.SetRootSignature(m_FrameIntegrationSig);
	m_FrameIntegrationPSO.SetComputeShader(g_pFrameIntegrationCS, sizeof(g_pFrameIntegrationCS));
	m_FrameIntegrationPSO.Finalize();


	
	TextureManager::Initialize(g_Scene.TextureFolderPath);
	bool bModelLoadSuccess = m_Model.Load(g_Scene.ModelPath.c_str());
	ASSERT(bModelLoadSuccess, "Failed to load model");
	ASSERT(m_Model.m_Header.meshCount > 0, "Model contains no meshes");

	set_hard_coded_material_properties(
		m_Model, m_pMaterialIsCutout, m_pMaterialIsReflective
	);


	g_hitConstantBuffer.Create(L"Hit Constant Buffer", 1, sizeof(HitShaderConstants));
	g_dynamicConstantBuffer.Create(L"Dynamic Constant Buffer", 1, sizeof(DynamicCB));

	InitializeSceneInfo(m_Model);
	InitializeViews(m_Model);
	UINT numMeshes = m_Model.m_Header.meshCount;

	if (g_RayTraceSupport)
	{
		CreateRayTraceAccelerationStructures(numMeshes);
		OutputDebugStringW(L"DXR support present on Device");
	}
	else
	{
		Settings::RayTracingMode = Settings::RTM_OFF;
		OutputDebugStringW(L"DXR support not present on Device");
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
	LoadCamPos();

	m_Camera.Setup(false);
    m_Camera.SetZRange(1.0f, 10000.0f);
	LODGlobal::g_camera = &m_Camera;
    m_CameraController.reset(new VRCameraController(m_Camera, Vector3(kYUnitVector)));
	LODGlobal::g_cameraController = m_CameraController.get();
	SetCameraToPredefinedPosition(0);
    
    Settings::MotionBlur_Enable = false;//true;
    Settings::TAA_Enable = false;//true;
    Settings::FXAA_Enable = false;
	Settings::EnableHDR = false;//true;
	Settings::EnableAdaptation = false;//true;
    Settings::SSAO_Enable = true;

    Lighting::CreateRandomLights(m_Model.GetBoundingBox().min, m_Model.GetBoundingBox().max);

    m_ExtraTextures[2] = Lighting::m_LightBuffer.GetSRV();
    m_ExtraTextures[3] = Lighting::m_LightShadowArray.GetSRV();
    m_ExtraTextures[4] = Lighting::m_LightGrid.GetSRV();
    m_ExtraTextures[5] = Lighting::m_LightGridBitMask.GetSRV();
}

void D3D12RaytracingMiniEngineSample::Cleanup(void)
{
	m_Model.Clear();
}

void D3D12RaytracingMiniEngineSample::Update(float deltaT)
{
	ScopedTimer _prof(L"Update State");

	if (GameInput::IsFirstPressed(GameInput::kLShoulder))
		Settings::DebugZoom.Decrement();
	else if (GameInput::IsFirstPressed(GameInput::kRShoulder))
		Settings::DebugZoom.Increment();
	if (g_RayTraceSupport)
	{
		if (GameInput::IsFirstPressed(GameInput::kKey_1))
			Settings::RayTracingMode = Settings::RTM_OFF;
		else if (GameInput::IsFirstPressed(GameInput::kKey_2))
			Settings::RayTracingMode = Settings::RTM_TRAVERSAL;
		else if (GameInput::IsFirstPressed(GameInput::kKey_3))
			Settings::RayTracingMode = Settings::RTM_SSR;
		else if (GameInput::IsFirstPressed(GameInput::kKey_4))
			Settings::RayTracingMode = Settings::RTM_SHADOWS;
		else if (GameInput::IsFirstPressed(GameInput::kKey_5))
			Settings::RayTracingMode = Settings::RTM_DIFFUSE_WITH_SHADOWMAPS;
		else if (GameInput::IsFirstPressed(GameInput::kKey_6))
			Settings::RayTracingMode = Settings::RTM_DIFFUSE_WITH_SHADOWRAYS;
		else if (GameInput::IsFirstPressed(GameInput::kKey_7))
			Settings::RayTracingMode = Settings::RTM_REFLECTIONS;
	}

	static bool freezeCamera = false;

	if (GameInput::IsFirstPressed(GameInput::kKey_f))
	{
		freezeCamera = !freezeCamera;
		GameInput::g_MouseLock = !GameInput::g_MouseLock;
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
	else if (GameInput::IsFirstPressed(GameInput::kKey_f3))
	{
		SaveCamPos();
	}
	else if (GameInput::IsFirstPressed(GameInput::kKey_f4))
	{
		LoadCamPos();
	}

	if (!freezeCamera)
	{
		m_CameraController->Update(deltaT);
	}

	float costheta = cosf(Settings::SunOrientation);
	float sintheta = sinf(Settings::SunOrientation);
	float cosphi = cosf(Settings::SunInclination * XM_PIDIV2);
	float sinphi = sinf(Settings::SunInclination * XM_PIDIV2);
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

	m_MainViewport.Width = (float)SceneColorBuffer()->GetWidth();
	m_MainViewport.Height = (float)SceneColorBuffer()->GetHeight();
	m_MainViewport.MinDepth = 0.0f;
	m_MainViewport.MaxDepth = 1.0f;

	m_MainScissor.left = 0;
	m_MainScissor.top = 0;
	m_MainScissor.right = (LONG)SceneColorBuffer()->GetWidth();
	m_MainScissor.bottom = (LONG)SceneColorBuffer()->GetHeight();
}

void D3D12RaytracingMiniEngineSample::RenderObjects(GraphicsContext& gfxContext, UINT curCam, const Matrix4& ViewProjMat,
                                                    eObjectFilter Filter)
{
	struct VSConstants
	{
		Matrix4 modelToProjection;
		Matrix4 modelToShadow;
		XMFLOAT3 viewerPos;
		UINT curCam;
	};

	VSConstants constants;

	Matrix4 model = g_Scene.Matrix;
	constants.modelToProjection = ViewProjMat * model;
	constants.curCam = curCam;

	constants.modelToShadow = m_SunShadow.GetShadowMatrix();
	XMStoreFloat3(&constants.viewerPos, m_Camera[curCam]->GetPosition());

	gfxContext.SetDynamicConstantBufferView(0, sizeof(constants), &constants);

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
		// (RayTracingMode != RTM_REFLECTIONS) || m_pMaterialIsReflective[mesh.materialIndex];
		gfxContext.SetConstants(4, baseVertex, materialIdx);
		gfxContext.SetConstants(5, areNormalsNeeded);

		gfxContext.DrawIndexed(indexCount, startIndex, baseVertex);
	}
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

		XMStoreFloat3x4((XMFLOAT3X4 *)instanceDesc.Transform, g_Scene.Matrix);

		instanceDesc.AccelerationStructure = g_bvh_bottomLevelAccelerationStructures[i]->GetGPUVirtualAddress();
		instanceDesc.Flags = 0;
		instanceDesc.InstanceID = 0;
		instanceDesc.InstanceMask = 1;
		instanceDesc.InstanceContributionToHitGroupIndex = i;
	}
	
	ByteAddressBuffer instanceDataBuffer;
	instanceDataBuffer.Create(L"Instance Data Buffer", numBottomLevels, sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
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
		RenderObjects(gfxContext, curCam, m_LightShadowMatrix[LightIndex], kOpaque);
		gfxContext.SetPipelineState(m_CutoutShadowPSO);
		RenderObjects(gfxContext, curCam, m_LightShadowMatrix[LightIndex], kCutout);
	}
	m_LightShadowTempBuffer.EndRendering(gfxContext);

	gfxContext.TransitionResource(m_LightShadowTempBuffer, D3D12_RESOURCE_STATE_GENERIC_READ);
	gfxContext.TransitionResource(m_LightShadowArray, D3D12_RESOURCE_STATE_COPY_DEST);

	gfxContext.CopySubresource(m_LightShadowArray, LightIndex, m_LightShadowTempBuffer, 0);

	gfxContext.TransitionResource(m_LightShadowArray, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	++LightIndex;
}

void D3D12RaytracingMiniEngineSample::RenderShadowMap()
{
	const bool skipShadowMap =
		Settings::RayTracingMode == Settings::RTM_DIFFUSE_WITH_SHADOWRAYS ||
		Settings::RayTracingMode == Settings::RTM_TRAVERSAL ||
		Settings::RayTracingMode == Settings::RTM_SSR;

	if (!skipShadowMap)
	{
		if (!Settings::SSAO_DebugDraw)
		{
			GraphicsContext& gfxContext = 
				GraphicsContext::Begin(L"Shadow Map Render");

			gfxContext.SetRootSignature(m_RootSig);								// TODO: Replace with pfnSetupGraphicsState()
			gfxContext.SetPrimitiveTopology(									//
				D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);							//
			gfxContext.SetIndexBuffer(m_Model.m_IndexBuffer.IndexBufferView());	//
			gfxContext.SetVertexBuffer(											//
				0, m_Model.m_VertexBuffer.VertexBufferView());					//
			{
				ScopedTimer _prof(L"Render Shadow Map", gfxContext);

				m_SunShadow.UpdateMatrix(-m_SunDirection, 
					Vector3(0, -500.0f, 0),
					Vector3(Settings::ShadowDimX, Settings::ShadowDimY, Settings::ShadowDimZ),
					(uint32_t)g_ShadowBuffer.GetWidth(), 
					(uint32_t)g_ShadowBuffer.GetHeight(), 16);

				g_ShadowBuffer.BeginRendering(gfxContext);
				gfxContext.SetPipelineState(m_ShadowPSO);
				RenderObjects(
					gfxContext, 0, m_SunShadow.GetViewProjMatrix(),  kOpaque);
				gfxContext.SetPipelineState(m_CutoutShadowPSO);
				RenderObjects(
					gfxContext, 0, m_SunShadow.GetViewProjMatrix(),  kCutout);
				g_ShadowBuffer.EndRendering(gfxContext);
			}

			gfxContext.Finish();
		}
	}
}

void D3D12RaytracingMiniEngineSample::RenderScene(UINT curCam)
{
	const bool skipDiffusePass =
		Settings::RayTracingMode == Settings::RTM_DIFFUSE_WITH_SHADOWMAPS ||
		Settings::RayTracingMode == Settings::RTM_DIFFUSE_WITH_SHADOWRAYS ||
		Settings::RayTracingMode == Settings::RTM_TRAVERSAL;

	static bool s_ShowLightCounts = false;
	if (Settings::ShowWaveTileCounts != s_ShowLightCounts)
	{
		static bool EnableHDR;
		if (Settings::ShowWaveTileCounts)
		{
			EnableHDR = Settings::EnableHDR;
			Settings::EnableHDR = false;
		}
		else
		{
			Settings::EnableHDR = EnableHDR;
		}
		s_ShowLightCounts = Settings::ShowWaveTileCounts;
	}

	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

	ParticleEffects::Update(gfxContext.GetComputeContext(), Graphics::GetFrameTime());

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
	psConstants.sunLight = Vector3(1.0f, 1.0f, 1.0f) * Settings::SunLightIntensity;
	psConstants.ambientLight = Vector3(1.0f, 1.0f, 1.0f) * Settings::AmbientIntensity;
	psConstants.ShadowTexelSize[0] = 1.0f / g_ShadowBuffer.GetWidth();
	psConstants.InvTileDim[0] = 1.0f / Settings::LightGridDim;
	psConstants.InvTileDim[1] = 1.0f / Settings::LightGridDim;
	psConstants.TileCount[0] = Math::DivideByMultiple(SceneColorBuffer()->GetWidth(), Settings::LightGridDim);
	psConstants.TileCount[1] = Math::DivideByMultiple(SceneColorBuffer()->GetHeight(), Settings::LightGridDim);	psConstants.FirstLightIndex[0] = Lighting::m_FirstConeLight;
	psConstants.FirstLightIndex[1] = Lighting::m_FirstConeShadowedLight;
	psConstants.FrameIndexMod2 = FrameIndex;

	// Set the default state for command lists
	auto& pfnSetupGraphicsState = [&](void)
	{
		gfxContext.SetRootSignature(m_RootSig);
		gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		gfxContext.SetIndexBuffer(m_Model.m_IndexBuffer.IndexBufferView());
		gfxContext.SetVertexBuffer(0, m_Model.m_VertexBuffer.VertexBufferView());
	};

	pfnSetupGraphicsState();

	RenderLightShadows(gfxContext, curCam);
	{
		ScopedTimer _prof(L"Z PrePass", gfxContext);
	
		gfxContext.SetStencilRef(0x0);

        gfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);

		{
			ScopedTimer _prof(L"Opaque", gfxContext);
			{
				gfxContext.TransitionResource(*SceneDepthBuffer(), D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
				// note: we no longer clear because we need the stencil from engine
				// prepass
				if (!Settings::VRDepthStencil)
				{
					gfxContext.ClearDepthAndStencil(*SceneDepthBuffer());
				}
				
                gfxContext.SetPipelineState(m_DepthPSO[0]);
                gfxContext.SetDepthStencilTarget(SceneDepthBuffer()->GetSubDSV(curCam));
				gfxContext.SetPipelineState(m_DepthPSO[0]);
				gfxContext.SetDepthStencilTarget(SceneDepthBuffer()->GetSubDSV(curCam));

                gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);
            }
			RenderObjects(gfxContext, curCam, m_Camera[curCam]->GetViewProjMatrix(), kOpaque);
		}

		{
			ScopedTimer _prof(L"Cutout", gfxContext);
			{
				gfxContext.SetPipelineState(m_CutoutDepthPSO[0]);
			}
			RenderObjects(gfxContext, curCam, m_Camera[curCam]->GetViewProjMatrix(), kCutout);
		}
	}

	SSAO::Render(gfxContext, *m_Camera[curCam], curCam);

	if (!skipDiffusePass)
	{
		Lighting::FillLightGrid(gfxContext, *m_Camera[curCam]);

		if (!Settings::SSAO_DebugDraw)
		{
			ScopedTimer _prof(L"Main Render", gfxContext);
			{
				gfxContext.TransitionResource(*SceneColorBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, true);
				gfxContext.TransitionResource(SceneNormalBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, true);
				gfxContext.ClearColor(*SceneColorBuffer(), curCam);
			}
		}
	}

	if (!skipDiffusePass)
	{
		if (!Settings::SSAO_DebugDraw)
		{
			if (Settings::AsyncCompute)
			{
				gfxContext.Flush();
				pfnSetupGraphicsState();

                // Make the 3D queue wait for the Compute queue to finish SSAO
                g_CommandManager.GetGraphicsQueue().StallForProducer(g_CommandManager.GetComputeQueue());
            }

            {
                ScopedTimer _prof(L"Render Color", gfxContext);

                gfxContext.TransitionResource(*SSAOFullScreen(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

				// Update what ExtraTextures[0] is pointing to
				m_ExtraTextures[0] = SSAOFullScreen()->GetSRV();

                gfxContext.SetDynamicDescriptors(3, 0, ARRAYSIZE(m_ExtraTextures), m_ExtraTextures);
                gfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);

				bool RenderIDs = !Settings::TAA_Enable;

				{
					gfxContext.SetPipelineState(Settings::ShowWaveTileCounts ? m_WaveTileCountPSO : m_ModelPSO[0]);

                    gfxContext.TransitionResource(*SceneDepthBuffer(), 
						D3D12_RESOURCE_STATE_DEPTH_READ);

					D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2]{
						SceneColorBuffer()->GetSubRTV(curCam),
						SceneNormalBuffer().GetSubRTV(curCam)
					};

					gfxContext.SetRenderTargets(ARRAYSIZE(rtvs), rtvs,
					                            SceneDepthBuffer()->GetSubDSV(curCam));


					gfxContext.SetViewportAndScissor(
						m_MainViewport, m_MainScissor);
				}

				RenderObjects(gfxContext, curCam, m_Camera[curCam]->GetViewProjMatrix(),  kOpaque);

				if (!Settings::ShowWaveTileCounts)
				{
					gfxContext.SetPipelineState(m_CutoutModelPSO[0]);
					RenderObjects(gfxContext, curCam, m_Camera[curCam]->GetViewProjMatrix(), kCutout);
				}
			}
		}

		// Some systems generate a per-pixel velocity buffer to better track dynamic and skinned meshes.  Everything
		// is static in our scene, so we generate velocity from camera motion and the depth buffer.  A velocity buffer
		// is necessary for all temporal effects (and motion blur).
		MotionBlur::GenerateCameraVelocityBuffer(gfxContext, *m_Camera[curCam], true);

		TemporalEffects::ResolveImage(gfxContext, curCam);

		ParticleEffects::Render(gfxContext, *m_Camera[curCam], *SceneColorBuffer(), *SceneDepthBuffer(),
		                        *LinearDepth(FrameIndex));

		// Until I work out how to couple these two, it's "either-or".
		if (Settings::DOF_Enable)
			DepthOfField::Render(gfxContext, m_Camera[curCam]->GetNearClip(), m_Camera[curCam]->GetFarClip(), curCam);
		else
			MotionBlur::RenderObjectBlur(gfxContext, g_VelocityBuffer, curCam);
	}
	
	if (g_RayTraceSupport/* && RayTracingMode != RTM_OFF*/)
	{
		Raytrace(gfxContext, curCam);
	}

	gfxContext.Finish(true);
}

void D3D12RaytracingMiniEngineSample::FrameIntegration()
{
	ComputeContext& cmpContext = ComputeContext::Begin(L"Frame Integration");

	if (Settings::TMPMode != Settings::TMPDebug::kOffFullRes && Settings::TMPMode != Settings::TMPDebug::kOffLowRes)
	{
		if (Graphics::GetFrameCount() % 2 == 0)
		{
			cmpContext.SetRootSignature(m_LowPassSig);
			cmpContext.SetPipelineState(m_LowPassPSO);

			cmpContext.TransitionResource(g_SceneColorBufferFullRes, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, true);
			cmpContext.TransitionResource(g_SceneColorBufferLowPassed, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

			cmpContext.SetDynamicDescriptor(0, 0, g_SceneColorBufferFullRes.GetSRV());
			cmpContext.SetDynamicDescriptor(1, 0, g_SceneColorBufferLowPassed.GetUAV());

			cmpContext.Dispatch2D(g_SceneColorBufferLowPassed.GetWidth(), g_SceneColorBufferLowPassed.GetHeight());

			// Reuses root signature m_LowPassSig
			cmpContext.SetPipelineState(m_DownsamplePSO);

			cmpContext.TransitionResource(g_SceneColorBufferLowPassed, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, true);
			cmpContext.TransitionResource(g_SceneColorBufferLowRes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

			cmpContext.SetDynamicDescriptor(0, 0, g_SceneColorBufferLowPassed.GetSRV());
			cmpContext.SetDynamicDescriptor(1, 0, g_SceneColorBufferLowRes.GetUAV());

			cmpContext.Dispatch2D(g_SceneColorBufferLowRes.GetWidth(), g_SceneColorBufferLowRes.GetHeight());

			cmpContext.TransitionResource(g_SceneColorBufferResidules, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
			cmpContext.ClearUAV(g_SceneColorBufferResidules);
		}

		cmpContext.SetRootSignature(m_FrameIntegrationSig);
		cmpContext.SetPipelineState(m_FrameIntegrationPSO);

		cmpContext.TransitionResource(g_SceneColorBufferLowRes, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, true);
		cmpContext.TransitionResource(g_SceneColorBufferFullRes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

		cmpContext.SetConstant(0, Graphics::GetFrameCount() % 2 == 0);
		cmpContext.SetDynamicDescriptor(1, 0, g_SceneColorBufferLowRes.GetSRV());
		cmpContext.SetDynamicDescriptor(2, 0, g_SceneColorBufferFullRes.GetUAV());
		cmpContext.SetDynamicDescriptor(3, 0, g_SceneColorBufferResidules.GetUAV());

		cmpContext.Dispatch2D(g_SceneColorBufferFullRes.GetWidth(), g_SceneColorBufferFullRes.GetHeight());
	}

	cmpContext.Finish();
}

//
// Tests traversal
//

void g_initialize_dynamicCb(
	CommandContext &Context,
	VRCamera &Camera,
	UINT CurCam,
	const ColorBuffer &ColorTarget,
	ByteAddressBuffer &Buffer)
{
	DynamicCB inputs = {};

	inputs.curCam = CurCam;

	Matrix4 viewProj = Camera[CurCam]->GetViewProjMatrix();
	const Matrix4 transInvViewProj = Transpose(Invert(viewProj));
	memcpy(&inputs.cameraToWorld, &transInvViewProj, sizeof(inputs.cameraToWorld));

	Vector3 position = Camera[CurCam]->GetPosition();
	memcpy(&inputs.worldCameraPosition, &position, sizeof(inputs.worldCameraPosition));
	
	inputs.resolution.x = (float)ColorTarget.GetWidth();
	inputs.resolution.y = (float)ColorTarget.GetHeight();

	Context.WriteBuffer(Buffer, 0, &inputs, sizeof(inputs));
}

void Raytracebarycentrics(
	CommandContext& context,
	UINT CurCam,
	ColorBuffer& colorTarget)
{
	ScopedTimer _p0(L"Raytracing barycentrics", context);

	// Create hit constants
	HitShaderConstants hitShaderConstants = {};
	hitShaderConstants.IsReflection = false;
	context.WriteBuffer(g_hitConstantBuffer, 0, &hitShaderConstants, sizeof(hitShaderConstants));

	// Transition resources
	// HITCB VERTEX AND CONSTANT BUFFER
	// DynCB VERTEX AND CONSTANT BUFFER
	// ColorTarget UAV
	context.TransitionResource(g_hitConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	context.TransitionResource(g_dynamicConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	context.TransitionResource(colorTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.FlushResourceBarriers();

	// Set bind resources
	CComPtr<ID3D12GraphicsCommandList4> pCmdList;
	context.GetCommandList()->QueryInterface(IID_PPV_ARGS(&pCmdList));

	ID3D12DescriptorHeap* pDescriptorHeaps[] = {&g_pRaytracingDescriptorHeap->GetDescriptorHeap()};
	pCmdList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);

	// 0,1,2,3,4,7 
	pCmdList->SetComputeRootSignature(g_GlobalRaytracingRootSignature);
	pCmdList->SetComputeRootDescriptorTable(0, g_SceneSrvs);
	pCmdList->SetComputeRootConstantBufferView(1, g_hitConstantBuffer.GetGpuVirtualAddress());
	pCmdList->SetComputeRootConstantBufferView(2, g_dynamicConstantBuffer.GetGpuVirtualAddress());
	pCmdList->SetComputeRootDescriptorTable(4, g_OutputUAV);
	pCmdList->SetComputeRootShaderResourceView(
		7, g_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress());

	D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = g_RaytracingInputs[Primarybarycentric].GetDispatchRayDesc(
		colorTarget.GetWidth(), colorTarget.GetHeight());
	pCmdList->SetPipelineState1(g_RaytracingInputs[Primarybarycentric].m_pPSO);
	pCmdList->DispatchRays(&dispatchRaysDesc);
}

void RaytracebarycentricsSSR(
	CommandContext& context,
	UINT CurCam,
	ColorBuffer& colorTarget,
	DepthBuffer& depth,
	ColorBuffer& normals)
{
	ScopedTimer _p0(L"Raytracing SSR barycentrics", context);

	HitShaderConstants hitShaderConstants = {};
	hitShaderConstants.IsReflection = false;
	context.WriteBuffer(g_hitConstantBuffer, 0, &hitShaderConstants, sizeof(hitShaderConstants));

	ComputeContext& ctx = context.GetComputeContext();
	ID3D12GraphicsCommandList* pCommandList = context.GetCommandList();

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
	pCommandList->SetComputeRootDescriptorTable(3, g_DepthAndNormalsTable);
	pCommandList->SetComputeRootDescriptorTable(4, g_OutputUAV);
	pRaytracingCommandList->SetComputeRootShaderResourceView(
		7, g_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress());

	D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = g_RaytracingInputs[Reflectionbarycentric].GetDispatchRayDesc(
		colorTarget.GetWidth(), colorTarget.GetHeight());
	pRaytracingCommandList->SetPipelineState1(g_RaytracingInputs[Reflectionbarycentric].m_pPSO);
	pRaytracingCommandList->DispatchRays(&dispatchRaysDesc);
}


void D3D12RaytracingMiniEngineSample::RaytraceShadows(
	GraphicsContext& context,
	UINT CurCam,
	ColorBuffer& colorTarget,
	DepthBuffer& depth)
{
	ScopedTimer _p0(L"Raytracing Shadows", context);

	HitShaderConstants hitShaderConstants = {};
	hitShaderConstants.sunDirection = m_SunDirection;
	hitShaderConstants.sunLight = Vector3(1.0f, 1.0f, 1.0f) * Settings::SunLightIntensity;
	hitShaderConstants.ambientLight = Vector3(1.0f, 1.0f, 1.0f) * Settings::AmbientIntensity;
	hitShaderConstants.ShadowTexelSize[0] = 1.0f / g_ShadowBuffer.GetWidth();
	hitShaderConstants.modelToShadow = m_SunShadow.GetShadowMatrix();
	hitShaderConstants.IsReflection = false;
	hitShaderConstants.UseShadowRays = false;
	context.WriteBuffer(g_hitConstantBuffer, 0, &hitShaderConstants, sizeof(hitShaderConstants));

	ComputeContext& ctx = context.GetComputeContext();
	ID3D12GraphicsCommandList* pCommandList = context.GetCommandList();

	ctx.TransitionResource(g_dynamicConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	ctx.TransitionResource(SceneNormalBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(*SSAOFullScreen(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
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
	UINT CurCam,
	ColorBuffer& colorTarget)
{
	ScopedTimer _p0(L"RaytracingWithHitShader", context);


	HitShaderConstants hitShaderConstants = {};
	hitShaderConstants.sunDirection = m_SunDirection;
	hitShaderConstants.sunLight = Vector3(1.0f, 1.0f, 1.0f) * Settings::SunLightIntensity;
	hitShaderConstants.ambientLight = Vector3(1.0f, 1.0f, 1.0f) * Settings::AmbientIntensity;
	hitShaderConstants.ShadowTexelSize[0] = 1.0f / g_ShadowBuffer.GetWidth();
	hitShaderConstants.modelToShadow = Transpose(m_SunShadow.GetShadowMatrix());
	hitShaderConstants.IsReflection = false;
	hitShaderConstants.UseShadowRays = Settings::RayTracingMode == Settings::RTM_DIFFUSE_WITH_SHADOWRAYS;
	context.WriteBuffer(g_hitConstantBuffer, 0, &hitShaderConstants, sizeof(hitShaderConstants));

	context.TransitionResource(g_dynamicConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	context.TransitionResource(*SSAOFullScreen(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
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
	UINT CurCam,
	ColorBuffer& colorTarget,
	DepthBuffer& depth,
	ColorBuffer& normals)
{
	ScopedTimer _p0(L"RaytracingWithHitShader", context);


	HitShaderConstants hitShaderConstants = {};
	hitShaderConstants.sunDirection = m_SunDirection;
	hitShaderConstants.sunLight = Vector3(1.0f, 1.0f, 1.0f) * Settings::SunLightIntensity;
	hitShaderConstants.ambientLight = Vector3(1.0f, 1.0f, 1.0f) * Settings::AmbientIntensity;
	hitShaderConstants.ShadowTexelSize[0] = 1.0f / g_ShadowBuffer.GetWidth();
	hitShaderConstants.modelToShadow = Transpose(m_SunShadow.GetShadowMatrix());
	hitShaderConstants.IsReflection = true;
	hitShaderConstants.UseShadowRays = false;
	context.WriteBuffer(g_hitConstantBuffer, 0, &hitShaderConstants, sizeof(hitShaderConstants));

	context.TransitionResource(g_dynamicConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	context.TransitionResource(*SSAOFullScreen(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
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

	float primaryRaysPerSec = SceneColorBuffer()->GetWidth() * SceneColorBuffer()->GetHeight() * rollingAverageFrameRate /
		(1000000.0f);
	TextContext text(gfxContext);
	text.Begin();
	//text.DrawFormattedString("\nMillion Primary Rays/s: %7.3f", primaryRaysPerSec);
	Vector3 camPos = m_Camera.GetPosition();
	text.DrawFormattedString("\nCam %d pos: %f, %f, %f",
		m_CameraPosArrayCurrentPosition,
		float(camPos.GetX()), float(camPos.GetY()), float(camPos.GetZ()));
	text.DrawFormattedString("\nCam rot: %f, %f",
		m_CameraController.get()->GetCurrentHeading(),
		m_CameraController.get()->GetCurrentPitch());
	text.End();
}

void D3D12RaytracingMiniEngineSample::Raytrace(class GraphicsContext& gfxContext, UINT CurCam)
{
	ScopedTimer _prof(L"Raytrace", gfxContext);

	gfxContext.TransitionResource(*SSAOFullScreen(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	g_initialize_dynamicCb(gfxContext, m_Camera, CurCam, *SceneColorBuffer(), g_dynamicConstantBuffer);

	switch (Settings::RayTracingMode)
	{
	case Settings::RTM_TRAVERSAL:
		Raytracebarycentrics(gfxContext, CurCam, *SceneColorBuffer());
		break;

	case Settings::RTM_SSR:
		RaytracebarycentricsSSR(gfxContext, CurCam, *SceneColorBuffer(), *SceneDepthBuffer(),
		                        SceneNormalBuffer());
		break;

	case Settings::RTM_SHADOWS:
		RaytraceShadows(gfxContext, CurCam, *SceneColorBuffer(), *SceneDepthBuffer());
		break;

	case Settings::RTM_DIFFUSE_WITH_SHADOWMAPS:
	case Settings::RTM_DIFFUSE_WITH_SHADOWRAYS:
		RaytraceDiffuse(gfxContext, CurCam, *SceneColorBuffer());
		break;

	case Settings::RTM_REFLECTIONS:
		RaytraceReflections(gfxContext, CurCam, *SceneColorBuffer(), *SceneDepthBuffer(), SceneNormalBuffer());
		break;
	}

	// Clear the gfxContext's descriptor heap since ray tracing changes this underneath the sheets
	gfxContext.SetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, nullptr);
}

void D3D12RaytracingMiniEngineSample::SaveCamPos()
{
	{
		std::ifstream file(m_CamPosFilename);

		if (file.good())
		{
			int msgboxID = MessageBox(
				NULL,
				L"Please make sure you are not \
				overwriting a needed camera position",
				L"Confirm Save",
				MB_ICONEXCLAMATION | MB_OKCANCEL
			);

			if (msgboxID == IDCANCEL)
			{
				return;
			}
		}
	}

	std::ofstream file(m_CamPosFilename, std::ios::trunc);

	for (int i = 0; i < m_CamPosCount; i++)
	{
		if (i == m_CameraPosArrayCurrentPosition)
		{
			Vector3 camPos = m_Camera.GetPosition();

			file << camPos.GetX() << "\n" 
				 << camPos.GetY() << "\n" 
				 << camPos.GetZ() << "\n" 
				 << m_CameraController.get()->GetCurrentHeading() << "\n" 
				 << m_CameraController.get()->GetCurrentPitch() << "\n";
		}
		else
		{
			Vector3 camPos = m_CameraPosArray[i].position;

			file << camPos.GetX() << "\n" 
				 << camPos.GetY() << "\n" 
				 << camPos.GetZ() << "\n" 
				 << m_CameraPosArray[i].heading << "\n" 
				 << m_CameraPosArray[i].pitch << "\n";
		}
	}
	file.close();
}

void D3D12RaytracingMiniEngineSample::LoadCamPos()
{
	std::ifstream file(m_CamPosFilename);

	if (file.good())
	{
		for (int i = 0; i < m_CamPosCount; i++)
		{
			std::string x, y, z, h, p;
			std::getline(file, x);
			std::getline(file, y);
			std::getline(file, z);
			std::getline(file, h);
			std::getline(file, p);
			m_CameraPosArray[i].position =
				Vector3(atof(x.c_str()), atof(y.c_str()), atof(z.c_str()));
			m_CameraPosArray[i].heading = atof(h.c_str());
			m_CameraPosArray[i].pitch = atof(p.c_str());
		}
	}
}