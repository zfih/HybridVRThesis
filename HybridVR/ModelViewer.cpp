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
#include "CompiledShaders/QuadLevelCompute.h"
#include "CompiledShaders/ReprojectionVS.h"
#include "CompiledShaders/ReprojectionHS.h"
#include "CompiledShaders/ReprojectionDS.h"
#include "CompiledShaders/ReprojectionPS.h"
#include "CompiledShaders/AddSSAOCS.h"

#include "RaytracingHlslCompat.h"
#include "ModelViewerRayTracing.h"

#include <iostream>
#include <fstream>

#include "../MiniEngine/Core/CameraType.h"
#include "GlobalState.h"

using namespace GameCore;
using namespace Math;
using namespace Graphics;

#define ASSET_DIRECTORY "../Assets/"


extern ByteAddressBuffer g_bvh_bottomLevelAccelerationStructure;

namespace Graphics
{
	ColorBuffer g_SceneNormalBuffer;
	StructuredBuffer g_SceneDiffBuffer;
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

__declspec(align(16)) struct PSConstants
{
	Vector3 sunDirection;
	Vector3 sunLight;
	Vector3 ambientLight;
	float ShadowTexelSize[4];

	float InvTileDim[4];
	uint32_t TileCount[4];
	uint32_t FirstLightIndex[4];
	uint32_t FrameIndexMod2;
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
		g_Scene.ModelPath = ASSET_DIRECTORY "Models/Bistro/bistro.h3d";
		g_Scene.TextureFolderPath = ASSET_DIRECTORY L"Models/Bistro/";
		g_Scene.Reflective = { "floor", "glass", "metal" };
		g_Scene.CutOuts = {  };
	} break;
	case Scene::kSponza:
	{
		g_Scene.Matrix = Matrix4(XMMatrixIdentity());
		g_Scene.ModelPath = ASSET_DIRECTORY "Models/Sponza/sponza.h3d";
		g_Scene.TextureFolderPath = ASSET_DIRECTORY L"Models/Sponza/Textures/";
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

	
	virtual void ReprojectScene() override;
	virtual void RenderSSAO() override;

	virtual void RenderScene() override;
	
	
	virtual void RenderUI(class GraphicsContext&) override;

	virtual void Raytrace(class GraphicsContext&, UINT cam);

	void SetCameraToPredefinedPosition(int cameraPosition);

private:

	void GenerateGrid(UINT width, UINT height);
	
	void RenderShadowMap();
	void RenderColor(
		GraphicsContext& Ctx,
		Camera& Camera,
		Cam::CameraType CameraType,
		DepthBuffer& DepthBuffer, PSConstants& Constants);
	void RenderEye(
		Cam::CameraType eye,
		bool SkipDiffusePass,
		bool SkipShadowMap,
		PSConstants& psConstants);
	void SetupGraphicsState(GraphicsContext& Ctx) const;
	void RenderPrepass(
		GraphicsContext& Ctx,
		Cam::CameraType CameraType,
		Camera& Camera,
		PSConstants& Constants
	);
	void MainRender(
		GraphicsContext& Ctx,
		Cam::CameraType CameraType,
		Camera& Camera,
		PSConstants& Constants,
		bool SkipDiffusePass,
		bool SkipShadowMap
	);
	
	void CreateRayTraceAccelerationStructures(UINT numMeshes);

	void RenderLightShadows(GraphicsContext& gfxContext, UINT curCam);

	enum eObjectFilter { kOpaque = 0x1, kCutout = 0x2, kTransparent = 0x4, kAll = 0xF, kNone = 0x0 };
	void RenderObjects(GraphicsContext& Context, UINT CurCam, const Matrix4& ViewProjMat, eObjectFilter Filter = kAll);
	void RaytraceDiffuse(GraphicsContext& context,  ColorBuffer& colorTarget);
	void RaytraceShadows(GraphicsContext& context, ColorBuffer& colorTarget,
	                     DepthBuffer& depth);
	void RaytraceReflections(GraphicsContext& context, ColorBuffer& colorTarget,
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

	RootSignature m_ReprojectionComputeRS;
	ComputePSO m_ReprojectionComputePSO;
	RootSignature m_ReprojectionRS;
	GraphicsPSO m_ReprojectionPSO;

	RootSignature m_SSAOComputeRS;
	ComputePSO m_SSAOComputePSO;

	StructuredBuffer m_GridVertexBuffer;
	ByteAddressBuffer m_GridIndexBuffer;
	
	D3D12_CPU_DESCRIPTOR_HANDLE m_DefaultSampler;
	D3D12_CPU_DESCRIPTOR_HANDLE m_ShadowSampler;
	D3D12_CPU_DESCRIPTOR_HANDLE m_BiasedDefaultSampler;

	D3D12_CPU_DESCRIPTOR_HANDLE m_ExtraTextures[6];
	Model m_Model;
	std::vector<bool> m_pMaterialIsCutout;
	std::vector<bool> m_pMaterialIsReflective;

	Vector3 m_SunDirection;
	ShadowCamera m_SunShadow;
	UINT m_QuadDivideFactor;

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

	BoolVar ReprojEnable("LOD/Reproject", true);

	EnumVar RayTracingMode("Application/Raytracing/RayTraceMode", RTM_DIFFUSE_WITH_SHADOWMAPS, _countof(rayTracingModes), rayTracingModes);

	BoolVar AOFirst("SSAO/AOFirst", true);

	
	CpuTimer g_ZPrepassTimer[2]{ {true, "ZPrepassLeft"}, {true, "ZPrepassRight"} };
	CpuTimer g_SSAOTimer[2]{ {true, "SSAOLeft"}, {true, "SSAORight"} };
	CpuTimer g_RaytraceTimer[2]{ {true, "RaytraceLeft"}, {true, "RaytraceRight"} };
	CpuTimer g_EyeRenderTimer[2]{ { true, "LeftEyeRender" }, { true, "RightEyeRender" } };
	CpuTimer g_ShadowRenderTimer(true, "ShadowRender");
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
		data.m_bitangentAttributeOffsetBytes = offset(Model::attrib_bitangent);
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
	Graphics::g_Device->CopyDescriptorsSimple(1, uavHandle, g_SceneColorBuffer.GetUAV(),
	                                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	g_OutputUAV = g_pRaytracingDescriptorHeap->GetGpuHandle(uavDescriptorIndex);

	{
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
		UINT srvDescriptorIndex;
		g_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, g_SceneDepthBuffer.GetDepthSRV(),
		                                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		g_DepthAndNormalsTable = g_pRaytracingDescriptorHeap->GetGpuHandle(srvDescriptorIndex);

		UINT unused;
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

	ThrowIfFailed(g_Device->QueryInterface(IID_PPV_ARGS(&g_pRaytracingDevice)),
	              L"Couldn't get DirectX Raytracing interface for the device.\n");
#if 0
	g_SceneNormalBuffer.CreateArray(L"Main Normal Buffer", g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight(), 2,
	                           DXGI_FORMAT_R16G16B16A16_FLOAT);
#else
	g_SceneNormalBuffer.CreateArray(L"Main Normal Buffer", g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight(), 2,
	                           DXGI_FORMAT_R8G8B8A8_UNORM);
#endif

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
	D3D12_INPUT_ELEMENT_DESC vertexLayout[] =
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
	m_DepthPSO[0].SetInputLayout(_countof(vertexLayout), vertexLayout);
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
	DXGI_FORMAT formats[]{ColorFormat, ColorFormat, NormalFormat};
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

	m_ReprojectionComputeRS.Reset(3);
	m_ReprojectionComputeRS[0].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2);
	m_ReprojectionComputeRS[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
	m_ReprojectionComputeRS[2].InitAsConstantBuffer(0);
	m_ReprojectionComputeRS.Finalize(L"Reprojection Root Signature",
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	m_ReprojectionComputePSO.SetRootSignature(m_ReprojectionComputeRS);
	m_ReprojectionComputePSO.SetComputeShader(g_pQuadLevelCompute, sizeof(g_pQuadLevelCompute));
	m_ReprojectionComputePSO.Finalize();

	SamplerDesc ClampSamplerDesc;
	ClampSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	ClampSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	ClampSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

	m_ReprojectionRS.Reset(3, 1);
	m_ReprojectionRS.InitStaticSampler(0, ClampSamplerDesc);
	m_ReprojectionRS[0].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 2);
	m_ReprojectionRS[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
	m_ReprojectionRS[2].InitAsConstantBuffer(0);
	m_ReprojectionRS.Finalize(L"Reprojection Root Signature",
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	D3D12_INPUT_ELEMENT_DESC quadGridLayout[] =
	{
		makeVertexInputElement("POSITION", DXGI_FORMAT_R32G32B32A32_FLOAT),
		makeVertexInputElement("TEXCOORD", DXGI_FORMAT_R32G32_FLOAT),
		makeVertexInputElement("QUADID", DXGI_FORMAT_R32_FLOAT)
	};

	m_ReprojectionPSO.SetRootSignature(m_ReprojectionRS);
	m_ReprojectionPSO.SetRasterizerState(RasterizerDefault);
	m_ReprojectionPSO.SetBlendState(BlendDisable); // This might be incorrect
	m_ReprojectionPSO.SetDepthStencilState(DepthStateReadWrite); // This might be incorrect
	m_ReprojectionPSO.SetInputLayout(_countof(quadGridLayout), quadGridLayout);
	m_ReprojectionPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH);
	m_ReprojectionPSO.SetRenderTargetFormat(ColorFormat, DepthFormat);
	m_ReprojectionPSO.SetVertexShader(g_pReprojectionVS, sizeof(g_pReprojectionVS));
	m_ReprojectionPSO.SetHullShader(g_pReprojectionHS, sizeof(g_pReprojectionHS));
	m_ReprojectionPSO.SetDomainShader(g_pReprojectionDS, sizeof(g_pReprojectionDS));
	m_ReprojectionPSO.SetPixelShader(g_pReprojectionPS, sizeof(g_pReprojectionPS));
	m_ReprojectionPSO.Finalize();

	m_SSAOComputeRS.Reset(2);
	m_SSAOComputeRS[0].InitAsDescriptorRange(
		D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1);
	m_SSAOComputeRS[1].InitAsDescriptorRange(
		D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 1);
	m_SSAOComputeRS.Finalize(L"SSAO Root Signature",
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	m_SSAOComputePSO.SetRootSignature(m_SSAOComputeRS);
	m_SSAOComputePSO.SetComputeShader(g_pAddSSAOCS, sizeof(g_pAddSSAOCS));
	m_SSAOComputePSO.Finalize();

	Lighting::InitializeResources();

	m_ExtraTextures[0] = g_SSAOFullScreen.GetSRV();
	m_ExtraTextures[1] = g_ShadowBuffer.GetSRV();


	
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

	// TODO: Make init func?
	// ASRP INIT STUFf
	m_QuadDivideFactor = 16;
	
	UINT bufferSize = g_SceneColorBuffer.GetWidth() * g_SceneColorBuffer.GetHeight() / m_QuadDivideFactor / m_QuadDivideFactor;
	g_SceneDiffBuffer.Create(L"Scene Diff Buffer", bufferSize, sizeof(float));
	GenerateGrid(g_SceneColorBuffer.GetWidth(), g_SceneColorBuffer.GetHeight());
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

	m_MainViewport.Width = (float)g_SceneColorBuffer.GetWidth();
	m_MainViewport.Height = (float)g_SceneColorBuffer.GetHeight();
	m_MainViewport.MinDepth = 0.0f;
	m_MainViewport.MaxDepth = 1.0f;

	m_MainScissor.left = 0;
	m_MainScissor.top = 0;
	m_MainScissor.right = (LONG)g_SceneColorBuffer.GetWidth();
	m_MainScissor.bottom = (LONG)g_SceneColorBuffer.GetHeight();
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
		uint32_t areNormalsNeeded = m_pMaterialIsReflective[mesh.materialIndex];
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

void D3D12RaytracingMiniEngineSample::RenderColor(GraphicsContext& Ctx, Camera& Camera, Cam::CameraType CameraType,
	DepthBuffer& DepthBuffer, PSConstants& Constants)
{
	ScopedTimer _prof(L"Render Color", Ctx);

	Ctx.TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	Ctx.SetDynamicDescriptors(3, 0, ARRAYSIZE(m_ExtraTextures), m_ExtraTextures);
	Ctx.SetDynamicConstantBufferView(1, sizeof(Constants), &Constants);

	bool RenderIDs = !Settings::TAA_Enable;

	{
		Ctx.SetPipelineState(Settings::ShowWaveTileCounts ? m_WaveTileCountPSO : m_ModelPSO[0]);

		Ctx.TransitionResource(g_SceneDepthBuffer,
			D3D12_RESOURCE_STATE_DEPTH_READ);

		D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2]{
			g_SceneColorBuffer.GetSubRTV(CameraType),
			g_SceneNormalBuffer.GetSubRTV(CameraType),
		};

		Ctx.SetRenderTargets(2, rtvs,
			g_SceneDepthBuffer.GetSubDSV(CameraType));

		Ctx.SetViewportAndScissor(
			m_MainViewport, m_MainScissor);
	}

	RenderObjects(Ctx, CameraType, m_Camera[CameraType]->GetViewProjMatrix(), kOpaque);

	if (!Settings::ShowWaveTileCounts)
	{
		if (!Settings::SSAO_DebugDraw)
		{
			GraphicsContext& gfxContext = 
				GraphicsContext::Begin(L"Shadow Map Render");

			SetupGraphicsState(gfxContext);
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
					gfxContext, CameraType, m_SunShadow.GetViewProjMatrix(), kOpaque);
				gfxContext.SetPipelineState(m_CutoutShadowPSO);
				RenderObjects(
					gfxContext, CameraType, m_SunShadow.GetViewProjMatrix(), kCutout);
				g_ShadowBuffer.EndRendering(gfxContext);
			}

			gfxContext.Finish();
		}
	}
}


void D3D12RaytracingMiniEngineSample::ReprojectScene()
{
	ComputeContext& quadLevelContext = ComputeContext::Begin(
		L"Reproject Compute Context");
	
	quadLevelContext.SetRootSignature(m_ReprojectionComputeRS);
	quadLevelContext.SetPipelineState(m_ReprojectionComputePSO);

	quadLevelContext.SetDynamicDescriptor(0, 0, g_SceneDepthBuffer.GetSubSRV(0));
	quadLevelContext.SetDynamicDescriptor(0, 1, g_SceneNormalBuffer.GetSRV());

	quadLevelContext.TransitionResource(
		g_SceneDiffBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// TODO: Maybe the top works. Maybe bottom. Who knows?
	quadLevelContext.SetDynamicDescriptor(1, 0, g_SceneDiffBuffer.GetUAV());
	//quadLevelContext.SetBufferUAV(1, g_SceneDiffBuffer, 0);
	
	struct ComputeCB
	{
		UINT gQuadSizeX;
		float gNearZ;
		float gFarZ;
		Vector3 gCamPos;
	};

	ComputeCB cb{
		g_SceneColorBuffer.GetWidth() / m_QuadDivideFactor,
		m_Camera.GetNearClip(),
		m_Camera.GetFarClip(),
		m_Camera.GetPosition()
	};

	UINT w = g_SceneColorBuffer.GetWidth() / m_QuadDivideFactor;
	UINT h = g_SceneColorBuffer.GetHeight() / m_QuadDivideFactor;
	
	quadLevelContext.SetDynamicConstantBufferView(2, sizeof(cb), &cb);
	quadLevelContext.Dispatch(w, h, 1);


	GraphicsContext& reprojectContext = GraphicsContext::Begin(
		L"Reproject Graphics Context");
	

	reprojectContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);
	reprojectContext.SetRootSignature(m_ReprojectionRS);
	reprojectContext.SetPipelineState(m_ReprojectionPSO);
	reprojectContext.SetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
	reprojectContext.SetIndexBuffer(m_GridIndexBuffer.IndexBufferView());
	reprojectContext.SetVertexBuffer(0, m_GridVertexBuffer.VertexBufferView());

	reprojectContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
	reprojectContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
	reprojectContext.ClearColor(g_SceneColorBuffer, 1);
	reprojectContext.SetRenderTarget(g_SceneColorBuffer.GetSubRTV(1), g_SceneDepthBuffer.GetSubDSV(1));
	
	// Hull Shader
	reprojectContext.SetDynamicDescriptor(1, 0, g_SceneDiffBuffer.GetUAV());
	//reprojectContext.SetBufferUAV(0, g_SceneDiffBuffer, 0);

	// Domain Shader
	reprojectContext.SetDynamicDescriptor(0, 0, g_SceneDepthBuffer.GetSubSRV(0));

	struct ReprojInput
	{
		XMMATRIX reprojectionMat;
	};

	ReprojInput ri{
		// TODO: FIX ENUM
		//XMMatrixIdentity()
		XMMatrixTranspose(m_Camera[1]->GetViewProjMatrix()) * 
		XMMatrixTranspose(XMMatrixInverse(nullptr, m_Camera[0]->GetViewProjMatrix()))
	};
	reprojectContext.SetDynamicConstantBufferView(2, sizeof(ri), &ri);
	
	// Pixel Shader
	reprojectContext.SetDynamicDescriptor(0, 1, g_SceneColorBuffer.GetSRV());

	reprojectContext.DrawIndexed(m_GridIndexBuffer.GetElementCount(), 0, 0);

	quadLevelContext.Finish();
	reprojectContext.Finish();
}

void D3D12RaytracingMiniEngineSample::GenerateGrid(UINT width, UINT height)
{
	UINT quadSizeX = width / m_QuadDivideFactor;
	UINT quadSizeY = height / m_QuadDivideFactor;

	UINT vertexCount = (quadSizeX + 1) * (quadSizeY + 1);
	UINT indexCount = quadSizeX * quadSizeY * 4;

	bool pixelCenter = true; // TODO: Make setting

	struct GridVertex
	{
		Vector4 pos;
		Vector2 uv;
		float quadID;
	};

	std::vector<GridVertex> gridVertices;
	gridVertices.reserve(vertexCount);

	float qID = -1.f; // Quad ID Counter
	for (uint32_t i = 0, y = quadSizeY; y >= 0 && y != 4294967295; y--) {
		for (uint32_t x = 0; x <= quadSizeX; x++, i++) {

			GridVertex gv{};
			
			gv.pos = Vector4((float)x / (float)quadSizeX * 2.f - 1, (float)y / (float)quadSizeY * 2.f - 1, 0, 1);
			gv.uv = Vector2((float)x / (float)quadSizeX, 1 - (float)y / (float)quadSizeY);
			gv.quadID = qID += 1.f;

			// Move vertex position to pixel center
			if (pixelCenter)
			{
				gv.pos += Vector4(0.5f / (float)width, -0.5f / (float)height, 0, 1);
				gv.uv += Vector2(0.5f / (float)width, 0.5f / (float)height);
			}
			gridVertices.push_back(gv);
		}
		qID -= 1.f; // Jump to next row (here might be a bug) - Nice comment lol
	}

	std::vector<UINT> quads;
	quads.reserve(indexCount);
	for (UINT qu = 0, vi = 0, y = 0; y < quadSizeY; y++, vi++) {
		for (UINT x = 0; x < quadSizeX; x++, qu += 4, vi++) {
			quads.push_back(vi + quadSizeX + 1);
			quads.push_back(vi + quadSizeX + 2);
			quads.push_back(vi + 1);
			quads.push_back(vi);
		}
	}
	
	m_GridVertexBuffer.Create(
		L"Grid Vertex Buffer",
		vertexCount,
		sizeof(GridVertex),
		gridVertices.data());
	
	m_GridIndexBuffer.Create(
		L"Grid Index Buffer",
		indexCount,
		sizeof(UINT),
		quads.data());
}

void D3D12RaytracingMiniEngineSample::RenderEye(Cam::CameraType eye, bool SkipDiffusePass, bool SkipShadowMap,
	PSConstants& psConstants)
{
	Settings::g_EyeRenderTimer[eye].Reset();
	Settings::g_EyeRenderTimer[eye].Start();
	GraphicsContext& ctx =
		GraphicsContext::Begin(L"Scene Render " + CameraTypeToWString(eye));
	Camera& camera = *m_Camera[eye];

	SetupGraphicsState(ctx);
	RenderPrepass(ctx, eye, camera, psConstants);

	MainRender(ctx, eye, camera,
		psConstants, SkipDiffusePass, SkipShadowMap);

	ctx.Finish(true);
	Settings::g_EyeRenderTimer[eye].Stop();
}

void D3D12RaytracingMiniEngineSample::SetupGraphicsState(GraphicsContext& Ctx) const
{
	Ctx.SetRootSignature(m_RootSig);
	Ctx.SetPrimitiveTopology(
		D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	Ctx.SetIndexBuffer(m_Model.m_IndexBuffer.IndexBufferView());
	Ctx.SetVertexBuffer(
		0, m_Model.m_VertexBuffer.VertexBufferView());
}

void D3D12RaytracingMiniEngineSample::RenderPrepass(GraphicsContext& Ctx, Cam::CameraType CameraType, Camera& Camera,
	PSConstants& Constants)
{
	
	RenderLightShadows(Ctx, CameraType);

	Settings::g_ZPrepassTimer[CameraType].Reset();
	Settings::g_ZPrepassTimer[CameraType].Start();
	
	{
		Ctx.SetStencilRef(0x0);
		
		ScopedTimer _prof(L"Z PrePass", Ctx);

		Ctx.SetDynamicConstantBufferView(1, sizeof(Constants), &Constants);

		{
			ScopedTimer _prof(L"Opaque", Ctx);
			{
				Ctx.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
				// note: we no longer clear because we need the stencil from engine
				// prepass

				if (!Settings::VRDepthStencil)
				{
					Ctx.ClearDepthAndStencil(g_SceneDepthBuffer, CameraType); // TODO: Check if CameraType is necessary
				}

				Ctx.SetPipelineState(m_DepthPSO[0]);
				Ctx.SetDepthStencilTarget(g_SceneDepthBuffer.GetSubDSV(CameraType));

				Ctx.SetViewportAndScissor(m_MainViewport, m_MainScissor);
			}

			RenderObjects(Ctx, CameraType, m_Camera[CameraType]->GetViewProjMatrix(),  kOpaque);
		}

		{
			ScopedTimer _prof(L"Cutout", Ctx);
			{
				Ctx.SetPipelineState(m_CutoutDepthPSO[0]);
			}
			RenderObjects(Ctx, CameraType, m_Camera[CameraType]->GetViewProjMatrix(), kCutout);
		}
	}
	
	Settings::g_ZPrepassTimer[CameraType].Stop();
}

void D3D12RaytracingMiniEngineSample::MainRender(GraphicsContext& Ctx, Cam::CameraType CameraType, Camera& Camera,
	PSConstants& Constants, bool SkipDiffusePass, bool SkipShadowMap)
{
	Settings::g_SSAOTimer[CameraType].Reset();
	Settings::g_SSAOTimer[CameraType].Start();
	SSAO::Render(Ctx, *m_Camera[CameraType], CameraType);
	Settings::g_SSAOTimer[CameraType].Stop();

	if (!SkipDiffusePass)
	{
		ScopedTimer _prof(L"Main Render", Ctx);

		Lighting::FillLightGrid(Ctx, *m_Camera[CameraType]);

		if (!Settings::SSAO_DebugDraw)
		{
			Ctx.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
			Ctx.TransitionResource(g_SceneNormalBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
			Ctx.ClearColor(g_SceneColorBuffer, CameraType);

			if (Settings::AsyncCompute)
			{
				Ctx.Flush();
				SetupGraphicsState(Ctx);

				// Make the 3D queue wait for the Compute queue to finish SSAO
				g_CommandManager.GetGraphicsQueue().StallForProducer(g_CommandManager.GetComputeQueue());
			}

			RenderColor(Ctx, Camera, CameraType, g_SceneDepthBuffer, Constants);
		}

		// Some systems generate a per-pixel velocity buffer to better track dynamic and skinned meshes.  Everything
		// is static in our scene, so we generate velocity from camera motion and the depth buffer.  A velocity buffer
		// is necessary for all temporal effects (and motion blur).
		MotionBlur::GenerateCameraVelocityBuffer(Ctx, *m_Camera[CameraType], true);

		TemporalEffects::ResolveImage(Ctx, CameraType);
		
		ParticleEffects::Render(Ctx, *m_Camera[CameraType], g_SceneColorBuffer, g_SceneDepthBuffer,
			g_LinearDepth[TemporalEffects::GetFrameIndexMod2()]);

		// Until I work out how to couple these two, it's "either-or".
		if (Settings::DOF_Enable)
			DepthOfField::Render(Ctx, m_Camera[CameraType]->GetNearClip(), m_Camera[CameraType]->GetFarClip(), CameraType);
		else
			MotionBlur::RenderObjectBlur(Ctx, g_VelocityBuffer, CameraType);
	}

	if (g_RayTraceSupport/* && RayTracingMode != RTM_OFF*/)
	{
		Settings::g_RaytraceTimer[CameraType].Reset();
		Settings::g_RaytraceTimer[CameraType].Start();
		Raytrace(Ctx, CameraType);
		Settings::g_RaytraceTimer[CameraType].Stop();
	}
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
	GraphicsContext& Ctx =
		GraphicsContext::Begin(L"Shadow Map Render");
	
	if (!Settings::SSAO_DebugDraw)
	{
		SetupGraphicsState(Ctx);
		{
			m_SunShadow.UpdateMatrix(-m_SunDirection, 
				Vector3(0, -500.0f, 0),
				Vector3(Settings::ShadowDimX, Settings::ShadowDimY, Settings::ShadowDimZ),
				(uint32_t)g_ShadowBuffer.GetWidth(), 
				(uint32_t)g_ShadowBuffer.GetHeight(), 16);

			g_ShadowBuffer.BeginRendering(Ctx);
			Ctx.SetPipelineState(m_ShadowPSO);
			RenderObjects(
				Ctx, 0, m_SunShadow.GetViewProjMatrix(),  kOpaque);
			Ctx.SetPipelineState(m_CutoutShadowPSO);
			RenderObjects(
				Ctx, 0, m_SunShadow.GetViewProjMatrix(),  kCutout);
			g_ShadowBuffer.EndRendering(Ctx);
		}

		Ctx.Finish();
	}
}

void D3D12RaytracingMiniEngineSample::RenderScene()
{
	bool skipDiffusePass =
		Settings::RayTracingMode == Settings::RTM_DIFFUSE_WITH_SHADOWMAPS ||
		Settings::RayTracingMode == Settings::RTM_DIFFUSE_WITH_SHADOWRAYS ||
		Settings::RayTracingMode == Settings::RTM_TRAVERSAL;

	const bool skipShadowMap =
		Settings::RayTracingMode == Settings::RTM_DIFFUSE_WITH_SHADOWMAPS ||
		Settings::RayTracingMode == Settings::RTM_TRAVERSAL ||
		Settings::RayTracingMode == Settings::RTM_SSR;


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

	PSConstants psConstants;
	psConstants.sunDirection = m_SunDirection;
	psConstants.sunLight = Vector3(1.0f, 1.0f, 1.0f) * Settings::SunLightIntensity;
	psConstants.ambientLight = Vector3(1.0f, 1.0f, 1.0f) * Settings::AmbientIntensity;
	psConstants.ShadowTexelSize[0] = 1.0f / g_ShadowBuffer.GetWidth();
	psConstants.InvTileDim[0] = 1.0f / Settings::LightGridDim;
	psConstants.InvTileDim[1] = 1.0f / Settings::LightGridDim;
	psConstants.TileCount[0] = Math::DivideByMultiple(g_SceneColorBuffer.GetWidth(), Settings::LightGridDim);
	psConstants.TileCount[1] = Math::DivideByMultiple(g_SceneColorBuffer.GetHeight(), Settings::LightGridDim);
	psConstants.FirstLightIndex[0] = Lighting::m_FirstConeLight;
	psConstants.FirstLightIndex[1] = Lighting::m_FirstConeShadowedLight;
	psConstants.FrameIndexMod2 = TemporalEffects::GetFrameIndexMod2();

	if(!skipShadowMap)
	{
		Settings::g_ShadowRenderTimer.Reset();
		Settings::g_ShadowRenderTimer.Start();
		RenderShadowMap();
		Settings::g_ShadowRenderTimer.Stop();
	}

	RenderEye(Cam::kLeft, skipDiffusePass, skipShadowMap, psConstants);
		
	if (Settings::ReprojEnable)
	{
		ReprojectScene();

		GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

		gfxContext.SetRootSignature(m_RootSig);
		gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		gfxContext.SetIndexBuffer(m_Model.m_IndexBuffer.IndexBufferView());
		gfxContext.SetVertexBuffer(0, m_Model.m_VertexBuffer.VertexBufferView());

		/*SSAO::Render(gfxContext, *m_Camera[cam], cam);*/

		RaytraceDiffuse(gfxContext, g_SceneColorBuffer);
		gfxContext.Finish();

		skipDiffusePass = true;
	}
	else 
	{
		RenderEye(Cam::kRight, skipDiffusePass, skipShadowMap, psConstants);
	}
}

void D3D12RaytracingMiniEngineSample::RenderSSAO()
{
	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Render SSAO");

	gfxContext.TransitionResource(
		g_SSAOFullScreen, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	gfxContext.TransitionResource(
		g_SceneColorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	ComputeContext& ctx = gfxContext.GetComputeContext();
	//ComputeContext& ctx = ComputeContext::Begin(L"Render SSAO Compute");

	SSAO::Render(gfxContext, *m_Camera[VRCamera::LEFT], VRCamera::LEFT);

	ctx.SetRootSignature(m_SSAOComputeRS);
	ctx.SetPipelineState(m_SSAOComputePSO);
	
	ctx.SetDynamicDescriptors(0, 0, 1, &g_SSAOFullScreen.GetSRV());
	ctx.SetDynamicDescriptors(
		1, 0, 1, &g_SceneColorBuffer.GetSubUAV(VRCamera::LEFT));
	ctx.Dispatch2D(g_SceneColorBuffer.GetWidth(),
				   g_SceneColorBuffer.GetHeight());

	SSAO::Render(gfxContext, *m_Camera[VRCamera::RIGHT], VRCamera::RIGHT);

	ctx.SetRootSignature(m_SSAOComputeRS);
	ctx.SetPipelineState(m_SSAOComputePSO);

	ctx.SetDynamicDescriptors(0, 0, 1, &g_SSAOFullScreen.GetSRV());
	ctx.SetDynamicDescriptors(
		1, 0, 1, &g_SceneColorBuffer.GetSubUAV(VRCamera::RIGHT));
	ctx.Dispatch2D(g_SceneColorBuffer.GetWidth(),
				   g_SceneColorBuffer.GetHeight());

	gfxContext.Finish();
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

	const Matrix4 mvp = Camera[CurCam]->GetViewProjMatrix();
	const Matrix4 transInvMvp = Transpose(Invert(mvp));
	memcpy(&inputs.cameraToWorld, &transInvMvp, sizeof(inputs.cameraToWorld));

	Vector3 position = Camera[CurCam]->GetPosition();
	memcpy(&inputs.worldCameraPosition, &position, sizeof(inputs.worldCameraPosition));
	
	inputs.resolution.x = (float)ColorTarget.GetWidth();
	inputs.resolution.y = (float)ColorTarget.GetHeight();

	Context.WriteBuffer(Buffer, 0, &inputs, sizeof(inputs));
}

void Raytracebarycentrics(
	CommandContext& context,
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
	ColorBuffer& colorTarget,
	DepthBuffer& depth,
	ColorBuffer& normals)
{
	ScopedTimer _p0(L"Raytracing SSR barycentrics", context);

	HitShaderConstants hitShaderConstants = {};
	hitShaderConstants.IsReflection = false;
	context.WriteBuffer(g_hitConstantBuffer, 0, &hitShaderConstants, sizeof(hitShaderConstants));

	ComputeContext& ctx = context.GetComputeContext();
	
	ctx.TransitionResource(g_dynamicConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	ctx.TransitionResource(g_hitConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	ctx.TransitionResource(normals, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(g_ShadowBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(colorTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	ctx.FlushResourceBarriers();

	// Set bind resources
	CComPtr<ID3D12GraphicsCommandList4> pCmdList;
	context.GetCommandList()->QueryInterface(IID_PPV_ARGS(&pCmdList));

	ID3D12DescriptorHeap* pDescriptorHeaps[] = {&g_pRaytracingDescriptorHeap->GetDescriptorHeap()};
	pCmdList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);

	pCmdList->SetComputeRootSignature(g_GlobalRaytracingRootSignature);
	pCmdList->SetComputeRootConstantBufferView(1, g_hitConstantBuffer.GetGpuVirtualAddress());
	pCmdList->SetComputeRootConstantBufferView(2, g_dynamicConstantBuffer.GetGpuVirtualAddress());
	pCmdList->SetComputeRootDescriptorTable(3, g_DepthAndNormalsTable);
	pCmdList->SetComputeRootDescriptorTable(4, g_OutputUAV);
	pCmdList->SetComputeRootShaderResourceView(
		7, g_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress());

	D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = g_RaytracingInputs[Reflectionbarycentric].GetDispatchRayDesc(
		colorTarget.GetWidth(), colorTarget.GetHeight());
	pCmdList->SetPipelineState1(g_RaytracingInputs[Reflectionbarycentric].m_pPSO);
	pCmdList->DispatchRays(&dispatchRaysDesc);
}


void D3D12RaytracingMiniEngineSample::RaytraceShadows(
	GraphicsContext& context,
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
	ctx.TransitionResource(g_SceneNormalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(g_hitConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	ctx.TransitionResource(depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(g_ShadowBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ctx.TransitionResource(colorTarget, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	ctx.FlushResourceBarriers();

	CComPtr<ID3D12GraphicsCommandList4> pCmdList;
	pCommandList->QueryInterface(IID_PPV_ARGS(&pCmdList));

	ID3D12DescriptorHeap* pDescriptorHeaps[] = {&g_pRaytracingDescriptorHeap->GetDescriptorHeap()};
	pCmdList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);

	pCmdList->SetComputeRootSignature(g_GlobalRaytracingRootSignature);
	pCmdList->SetComputeRootConstantBufferView(1, g_hitConstantBuffer.GetGpuVirtualAddress());
	pCmdList->SetComputeRootConstantBufferView(2, g_dynamicConstantBuffer->GetGPUVirtualAddress());
	pCmdList->SetComputeRootDescriptorTable(3, g_DepthAndNormalsTable);
	pCmdList->SetComputeRootDescriptorTable(4, g_OutputUAV);
	pCmdList->SetComputeRootShaderResourceView(
		7, g_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress());

	D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = g_RaytracingInputs[Shadows].GetDispatchRayDesc(
		colorTarget.GetWidth(), colorTarget.GetHeight());
	pCmdList->SetPipelineState1(g_RaytracingInputs[Shadows].m_pPSO);
	pCmdList->DispatchRays(&dispatchRaysDesc);
}

void D3D12RaytracingMiniEngineSample::RaytraceDiffuse(
	GraphicsContext& context,
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

	gfxContext.TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	g_initialize_dynamicCb(gfxContext, m_Camera, CurCam, g_SceneColorBuffer, g_dynamicConstantBuffer);

	switch (Settings::RayTracingMode)
	{
	case Settings::RTM_TRAVERSAL:
		Raytracebarycentrics(gfxContext, g_SceneColorBuffer);
		break;

	case Settings::RTM_SSR:
		RaytracebarycentricsSSR(gfxContext, g_SceneColorBuffer, g_SceneDepthBuffer,
		                        g_SceneNormalBuffer);
		break;

	case Settings::RTM_SHADOWS:
		RaytraceShadows(gfxContext, g_SceneColorBuffer, g_SceneDepthBuffer);
		break;

	case Settings::RTM_DIFFUSE_WITH_SHADOWMAPS:
	case Settings::RTM_DIFFUSE_WITH_SHADOWRAYS:
		RaytraceDiffuse(gfxContext, g_SceneColorBuffer);
		break;

	case Settings::RTM_REFLECTIONS:
		RaytraceReflections(gfxContext, g_SceneColorBuffer, g_SceneDepthBuffer, g_SceneNormalBuffer);
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