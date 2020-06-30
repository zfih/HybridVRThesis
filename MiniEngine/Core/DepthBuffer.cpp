//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author:  James Stanard 
//

#include "pch.h"
#include "DepthBuffer.h"

#include "CommandContext.h"
#include "GraphicsCore.h"
#include "EsramAllocator.h"
#include "DescriptorHeap.h"

using namespace Graphics;

void DepthBuffer::Create(const std::wstring& Name, uint32_t Width, uint32_t Height, DXGI_FORMAT Format,
                         D3D12_GPU_VIRTUAL_ADDRESS VidMemPtr)
{
	D3D12_RESOURCE_DESC ResourceDesc = DescribeTex2D(Width, Height, 1, 1, Format,
	                                                 D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
	D3D12_CLEAR_VALUE ClearValue = {};
	ClearValue.Format = Format;
	ClearValue.DepthStencil.Stencil = 0xff;

	m_ClearStencil = ClearValue.DepthStencil.Stencil;

	CreateTextureResource(Graphics::g_Device, Name, ResourceDesc, ClearValue, VidMemPtr);
	CreateDerivedViews(Graphics::g_Device, Format);
}

void DepthBuffer::Create(const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t Samples,
                         DXGI_FORMAT Format, D3D12_GPU_VIRTUAL_ADDRESS VidMemPtr)
{
	D3D12_RESOURCE_DESC ResourceDesc = DescribeTex2D(Width, Height, 1, 1, Format,
	                                                 D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
	ResourceDesc.SampleDesc.Count = Samples;

	D3D12_CLEAR_VALUE ClearValue = {};
	ClearValue.Format = Format;
	ClearValue.DepthStencil.Stencil = 0xff;

	m_ClearStencil = ClearValue.DepthStencil.Stencil;

	CreateTextureResource(Graphics::g_Device, Name, ResourceDesc, ClearValue, VidMemPtr);
	CreateDerivedViews(Graphics::g_Device, Format);
}

void DepthBuffer::Create(const std::wstring& Name, uint32_t Width, uint32_t Height, DXGI_FORMAT Format, EsramAllocator&)
{
	Create(Name, Width, Height, Format);
}

void DepthBuffer::Create(const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t Samples,
                         DXGI_FORMAT Format, EsramAllocator&)
{
	Create(Name, Width, Height, Samples, Format);
}


void DepthBuffer::CreateArray(const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t ArrayCount,
                              DXGI_FORMAT Format, D3D12_GPU_VIRTUAL_ADDRESS VidMemPtr)
{
	D3D12_RESOURCE_DESC ResourceDesc = DescribeTex2D(Width, Height, ArrayCount, 1, Format,
	                                                 D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

	D3D12_CLEAR_VALUE ClearValue = {};
	ClearValue.Format = Format;
	ClearValue.DepthStencil.Depth = 0;
	ClearValue.DepthStencil.Stencil = 0xff;

	m_ClearStencil = ClearValue.DepthStencil.Stencil;

	CreateTextureResource(Graphics::g_Device, Name, ResourceDesc, ClearValue);
	CreateDerivedViews(Graphics::g_Device, Format, ArrayCount);
}

void DepthBuffer::CreateDerivedViews(ID3D12Device* Device, DXGI_FORMAT Format, uint32_t ArraySize)
{
	ID3D12Resource* Resource = m_pResource.Get();

	// Fill out dsvDesc
	D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};

	D3D12_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
	DSVDesc.Format = GetDSVFormat(Format);
	SRVDesc.Format = GetDepthFormat(Format);
	if (ArraySize > 1)
	{
		DSVDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
		DSVDesc.Texture2DArray.MipSlice = 0;
		DSVDesc.Texture2DArray.FirstArraySlice = 0;
		DSVDesc.Texture2DArray.ArraySize = ArraySize;

		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		SRVDesc.Texture2DArray.MipLevels = 4;
		SRVDesc.Texture1DArray.MostDetailedMip = 0;
		SRVDesc.Texture2DArray.FirstArraySlice = 0;
		SRVDesc.Texture2DArray.ArraySize = ArraySize;
	}
	else if (Resource->GetDesc().SampleDesc.Count == 1)
	{
		DSVDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
		DSVDesc.Texture2D.MipSlice = 0;

		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		SRVDesc.Texture2D.MipLevels = 1;
	}
	else
	{
		DSVDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
		SRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
	}

	if (m_hDSV[0].ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
	{
		m_hDSV[0] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		m_hDSV[1] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	}

	DSVDesc.Flags = D3D12_DSV_FLAG_NONE;
	Device->CreateDepthStencilView(Resource, &DSVDesc, m_hDSV[0]);

	DSVDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
	Device->CreateDepthStencilView(Resource, &DSVDesc, m_hDSV[1]);

	DXGI_FORMAT stencilReadFormat = GetStencilFormat(Format);
	if (stencilReadFormat != DXGI_FORMAT_UNKNOWN)
	{
		if (m_hDSV[2].ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
		{
			m_hDSV[2] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
			m_hDSV[3] = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		}

		DSVDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_STENCIL;
		Device->CreateDepthStencilView(Resource, &DSVDesc, m_hDSV[2]);

		DSVDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL;
		Device->CreateDepthStencilView(Resource, &DSVDesc, m_hDSV[3]);
	}
	else
	{
		m_hDSV[2] = m_hDSV[0];
		m_hDSV[3] = m_hDSV[1];
	}

	if (m_hDepthSRV.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
		m_hDepthSRV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// Create the shader resource view
	SRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	Device->CreateShaderResourceView(Resource, &SRVDesc, m_hDepthSRV);

	m_DSVSubHandles.reserve(ArraySize);
	m_DSVReadOnlySubHandles.reserve(ArraySize);
	m_SRVSubHandles.reserve(ArraySize);
	for (int i = 0; i < ArraySize; i++)
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
		dsvDesc.Format = GetDSVFormat(Format);
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
		dsvDesc.Texture2DArray.MipSlice = 0;
		dsvDesc.Texture2DArray.FirstArraySlice = i;
		dsvDesc.Texture2DArray.ArraySize = 1;
		dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

		D3D12_CPU_DESCRIPTOR_HANDLE handle{};
		handle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		Device->CreateDepthStencilView(Resource, &dsvDesc, handle);
		m_DSVSubHandles.push_back(handle);

		
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvReadOnlyDesc{};
		dsvReadOnlyDesc.Format = Format;
		dsvReadOnlyDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
		dsvReadOnlyDesc.Texture2DArray.MipSlice = 0;
		dsvReadOnlyDesc.Texture2DArray.FirstArraySlice = i;
		dsvReadOnlyDesc.Texture2DArray.ArraySize = 1;
		dsvReadOnlyDesc.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;

		D3D12_CPU_DESCRIPTOR_HANDLE handleReadOnly{};
		handleReadOnly = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		Device->CreateDepthStencilView(Resource, &dsvReadOnlyDesc, handleReadOnly);
		m_DSVReadOnlySubHandles.push_back(handleReadOnly);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = GetDepthFormat(Format);
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		srvDesc.Texture2DArray.FirstArraySlice = i;
		srvDesc.Texture2DArray.ArraySize = 1;
		srvDesc.Texture2DArray.MipLevels = 1;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle{};
		srvHandle = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		Device->CreateShaderResourceView(Resource, &srvDesc, srvHandle);
		m_SRVSubHandles.push_back(srvHandle);
	}

	if (stencilReadFormat != DXGI_FORMAT_UNKNOWN)
	{
		if (m_hStencilSRV.ptr == D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN)
			m_hStencilSRV = Graphics::AllocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		SRVDesc.Format = stencilReadFormat;

		if (ArraySize > 1)
		{
			SRVDesc.Texture2DArray.PlaneSlice = 1;
		}
		else if (Resource->GetDesc().SampleDesc.Count == 1)
		{
			SRVDesc.Texture2D.PlaneSlice = 1;
		}

		Device->CreateShaderResourceView(Resource, &SRVDesc, m_hStencilSRV);
	}
}

void DepthBuffer::GenerateMipMaps(CommandContext& BaseContext)
{
	if (m_NumMipMaps == 0)
		return;

	ComputeContext& Context = BaseContext.GetComputeContext();

	Context.SetRootSignature(Graphics::g_GenerateMipsRS);

	Context.TransitionResource(*this, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	Context.SetDynamicDescriptor(1, 0, m_SRVSubHandles[0]);

	for (uint32_t TopMip = 0; TopMip < m_NumMipMaps;)
	{
		uint32_t SrcWidth = m_Width >> TopMip;
		uint32_t SrcHeight = m_Height >> TopMip;
		uint32_t DstWidth = SrcWidth >> 1;
		uint32_t DstHeight = SrcHeight >> 1;

		// Determine if the first downsample is more than 2:1.  This happens whenever
		// the source width or height is odd.
		uint32_t NonPowerOfTwo = (SrcWidth & 1) | (SrcHeight & 1) << 1;
		if (m_Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)
			Context.SetPipelineState(Graphics::g_GenerateMipsGammaPSO[NonPowerOfTwo]);
		else
			Context.SetPipelineState(Graphics::g_GenerateMipsLinearPSO[NonPowerOfTwo]);

		// We can downsample up to four times, but if the ratio between levels is not
		// exactly 2:1, we have to shift our blend weights, which gets complicated or
		// expensive.  Maybe we can update the code later to compute sample weights for
		// each successive downsample.  We use _BitScanForward to count number of zeros
		// in the low bits.  Zeros indicate we can divide by two without truncating.
		uint32_t AdditionalMips;
		_BitScanForward((unsigned long*)&AdditionalMips,
			(DstWidth == 1 ? DstHeight : DstWidth) | (DstHeight == 1 ? DstWidth : DstHeight));
		uint32_t NumMips = 1 + (AdditionalMips > 3 ? 3 : AdditionalMips);
		if (TopMip + NumMips > m_NumMipMaps)
			NumMips = m_NumMipMaps - TopMip;

		// These are clamped to 1 after computing additional mips because clamped
		// dimensions should not limit us from downsampling multiple times.  (E.g.
		// 16x1 -> 8x1 -> 4x1 -> 2x1 -> 1x1.)
		if (DstWidth == 0)
			DstWidth = 1;
		if (DstHeight == 0)
			DstHeight = 1;

		Context.SetConstants(0, TopMip, NumMips, 1.0f / DstWidth, 1.0f / DstHeight);
		//Context.SetDynamicDescriptors(2, 0, NumMips,  + TopMip + 1);
		Context.Dispatch2D(DstWidth, DstHeight);

		Context.InsertUAVBarrier(*this);

		TopMip += NumMips;
	}

	Context.TransitionResource(*this, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}