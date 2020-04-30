// Raytracing.h

#pragma once

// System
#include <vector>

// External
#include <d3d12.h>
#include <atlcomcli.h>

// Internal
#include "DXSampleHelper.h"
#include "GpuBuffer.h"


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

