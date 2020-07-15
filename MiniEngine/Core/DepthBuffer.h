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

#pragma once

#include "PixelBuffer.h"

class EsramAllocator;

class DepthBuffer : public PixelBuffer
{
public:
    DepthBuffer( float ClearDepth = 0.0f, uint8_t ClearStencil = 0 )
        : m_ClearDepth(ClearDepth), m_ClearStencil(ClearStencil) 
    {
        m_hDSV[0].ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
        m_hDSV[1].ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
        m_hDSV[2].ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
        m_hDSV[3].ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
        m_hDepthSRV.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
        m_hStencilSRV.ptr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN;
    }

    // Create a depth buffer.  If an address is supplied, memory will not be allocated.
    // The vmem address allows you to alias buffers (which can be especially useful for
    // reusing ESRAM across a frame.)
    void Create( const std::wstring& Name, uint32_t Width, uint32_t Height, DXGI_FORMAT Format,
        D3D12_GPU_VIRTUAL_ADDRESS VidMemPtr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN );

	void Create( const std::wstring& Name, uint32_t Width, uint32_t Height, DXGI_FORMAT Format, uint32_t NumMips,
        D3D12_GPU_VIRTUAL_ADDRESS VidMemPtr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN );

    // Create a depth buffer.  Memory will be allocated in ESRAM (on Xbox One).  On Windows,
    // this functions the same as Create() without a video address.
    void Create( const std::wstring& Name, uint32_t Width, uint32_t Height, DXGI_FORMAT Format,
        EsramAllocator& Allocator );
	void Create( const std::wstring& Name, uint32_t Width, uint32_t Height, DXGI_FORMAT Format, uint32_t NumMips,
        EsramAllocator& Allocator );

    void Create(const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t NumSamples, DXGI_FORMAT Format,
        D3D12_GPU_VIRTUAL_ADDRESS VidMemPtr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN );
    void Create( const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t NumSamples, DXGI_FORMAT Format,
        EsramAllocator& Allocator );
	
    void Create(const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t NumSamples, DXGI_FORMAT Format, uint32_t NumMips,
        D3D12_GPU_VIRTUAL_ADDRESS VidMemPtr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN );
    void Create( const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t NumSamples, DXGI_FORMAT Format, uint32_t NumMips,
        EsramAllocator& Allocator );

    // Create a color buffer.  If an address is supplied, memory will not be allocated.
    // The vmem address allows you to alias buffers (which can be especially useful for
    // reusing ESRAM across a frame.)
    void CreateArray(const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t ArrayCount,
        DXGI_FORMAT Format, D3D12_GPU_VIRTUAL_ADDRESS VidMemPtr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN);

	void CreateArray(const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t ArrayCount, uint32_t NumMips,
        DXGI_FORMAT Format, D3D12_GPU_VIRTUAL_ADDRESS VidMemPtr = D3D12_GPU_VIRTUAL_ADDRESS_UNKNOWN);

    // Create a color buffer.  Memory will be allocated in ESRAM (on Xbox One).  On Windows,
    // this functions the same as Create() without a video address.
    void CreateArray(const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t ArrayCount,
        DXGI_FORMAT Format, EsramAllocator& Allocator);
	void CreateArray(const std::wstring& Name, uint32_t Width, uint32_t Height, uint32_t ArrayCount, uint32_t NumMips,
        DXGI_FORMAT Format, EsramAllocator& Allocator);

    const D3D12_CPU_DESCRIPTOR_HANDLE& GetSubDSV(int i) const { return m_DSVSubHandles[i]; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetReadOnlySubDSV(int i) const { return m_DSVReadOnlySubHandles[i]; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetSubSRV(int i) const { return m_SRVSubHandles[i]; }

    // Get pre-created CPU-visible descriptor handles
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetDSV() const { return m_hDSV[0]; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetDSV_DepthReadOnly() const { return m_hDSV[1]; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetDSV_StencilReadOnly() const { return m_hDSV[2]; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetDSV_ReadOnly() const { return m_hDSV[3]; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetDepthSRV() const { return m_hDepthSRV; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetStencilSRV() const { return m_hStencilSRV; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetMipDSV(uint32_t arrayIndex, uint32_t mipLevel) const { return m_MipDSVHandles[arrayIndex][mipLevel]; }
    const D3D12_CPU_DESCRIPTOR_HANDLE& GetMipReadOnlyDSV(uint32_t arrayIndex, uint32_t mipLevel) const { return m_MipDSVReadOnlyHandles[arrayIndex][mipLevel]; }


    float GetClearDepth() const { return m_ClearDepth; }
    uint8_t GetClearStencil() const { return m_ClearStencil; }

private:

    void CreateDerivedViews( ID3D12Device* Device, DXGI_FORMAT Format, uint32_t ArraySize = 1, uint32_t NumMips = 1);

    float m_ClearDepth;
    uint8_t m_ClearStencil;
    D3D12_CPU_DESCRIPTOR_HANDLE m_hDSV[4];
    D3D12_CPU_DESCRIPTOR_HANDLE m_hDepthSRV;
    D3D12_CPU_DESCRIPTOR_HANDLE m_hStencilSRV;

    std::vector< std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> > m_MipDSVHandles;
    std::vector< std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> > m_MipDSVReadOnlyHandles;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_DSVSubHandles;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_DSVReadOnlySubHandles;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_SRVSubHandles;
};
