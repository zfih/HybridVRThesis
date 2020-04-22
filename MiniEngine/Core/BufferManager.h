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

#include "ColorBuffer.h"
#include "DepthBuffer.h"
#include "ShadowBuffer.h"
#include "GpuBuffer.h"
#include "GraphicsCore.h"

namespace Graphics
{
    extern DepthBuffer g_SceneDepthBufferFullRes;    // D32_FLOAT_S8_UINT
    extern DepthBuffer g_SceneDepthBufferLowRes;    // D32_FLOAT_S8_UINT
    extern ColorBuffer g_SceneColorBufferFullRes;    // R11G11B10_FLOAT
    extern ColorBuffer g_SceneColorBufferLowRes;    // R11G11B10_FLOAT
    extern ColorBuffer g_SceneColorBufferLowPassed;    // R11G11B10_FLOAT
    extern ColorBuffer g_SceneColorBufferResidules;    // R11G11B10_FLOAT
    extern ColorBuffer g_PostEffectsBuffer;    // R32_UINT (to support Read-Modify-Write with a UAV)
    extern ColorBuffer g_OverlayBuffer;        // R8G8B8A8_UNORM
    extern ColorBuffer g_HorizontalBuffer;    // For separable (bicubic) upsampling

    extern ColorBuffer g_VelocityBuffer;    // R10G10B10  (3D velocity)
    extern ShadowBuffer g_ShadowBuffer;

    extern ColorBuffer g_SSAOFullScreenFullRes;    // R8_UNORM
    extern ColorBuffer g_SSAOFullScreenLowRes;    // R8_UNORM
    extern ColorBuffer g_LinearDepthFullRes[2];    // Normalized planar distance (0 at eye, 1 at far plane) computed from the SceneDepthBuffer
    extern ColorBuffer g_LinearDepthLowRes[2];    // Normalized planar distance (0 at eye, 1 at far plane) computed from the SceneDepthBuffer
    extern ColorBuffer g_MinMaxDepth8;        // Min and max depth values of 8x8 tiles
    extern ColorBuffer g_MinMaxDepth16;        // Min and max depth values of 16x16 tiles
    extern ColorBuffer g_MinMaxDepth32;        // Min and max depth values of 16x16 tiles
    extern ColorBuffer g_DepthDownsize1FullRes;
    extern ColorBuffer g_DepthDownsize2FullRes;
    extern ColorBuffer g_DepthDownsize3FullRes;
    extern ColorBuffer g_DepthDownsize4FullRes;
    extern ColorBuffer g_DepthTiled1FullRes;
    extern ColorBuffer g_DepthTiled2FullRes;
    extern ColorBuffer g_DepthTiled3FullRes;
    extern ColorBuffer g_DepthTiled4FullRes;
    extern ColorBuffer g_AOMerged1FullRes;
    extern ColorBuffer g_AOMerged2FullRes;
    extern ColorBuffer g_AOMerged3FullRes;
    extern ColorBuffer g_AOMerged4FullRes;
    extern ColorBuffer g_AOSmooth1FullRes;
    extern ColorBuffer g_AOSmooth2FullRes;
    extern ColorBuffer g_AOSmooth3FullRes;
    extern ColorBuffer g_AOHighQuality1FullRes;
    extern ColorBuffer g_AOHighQuality2FullRes;
    extern ColorBuffer g_AOHighQuality3FullRes;
    extern ColorBuffer g_AOHighQuality4FullRes;
    extern ColorBuffer g_DepthDownsize1LowRes;
    extern ColorBuffer g_DepthDownsize2LowRes;
    extern ColorBuffer g_DepthDownsize3LowRes;
    extern ColorBuffer g_DepthDownsize4LowRes;
    extern ColorBuffer g_DepthTiled1LowRes;
    extern ColorBuffer g_DepthTiled2LowRes;
    extern ColorBuffer g_DepthTiled3LowRes;
    extern ColorBuffer g_DepthTiled4LowRes;
    extern ColorBuffer g_AOMerged1LowRes;
    extern ColorBuffer g_AOMerged2LowRes;
    extern ColorBuffer g_AOMerged3LowRes;
    extern ColorBuffer g_AOMerged4LowRes;
    extern ColorBuffer g_AOSmooth1LowRes;
    extern ColorBuffer g_AOSmooth2LowRes;
    extern ColorBuffer g_AOSmooth3LowRes;
    extern ColorBuffer g_AOHighQuality1LowRes;
    extern ColorBuffer g_AOHighQuality2LowRes;
    extern ColorBuffer g_AOHighQuality3LowRes;
    extern ColorBuffer g_AOHighQuality4LowRes;

    extern ColorBuffer g_DoFTileClass[2];
    extern ColorBuffer g_DoFPresortBuffer;
    extern ColorBuffer g_DoFPrefilter;
    extern ColorBuffer g_DoFBlurColor[2];
    extern ColorBuffer g_DoFBlurAlpha[2];
    extern StructuredBuffer g_DoFWorkQueue;
    extern StructuredBuffer g_DoFFastQueue;
    extern StructuredBuffer g_DoFFixupQueue;

    extern ColorBuffer g_MotionPrepBuffer;        // R10G10B10A2
    extern ColorBuffer g_LumaBuffer;
    extern ColorBuffer g_TemporalColor[2];

    extern ColorBuffer g_aBloomUAV1[2];        // 640x384 (1/3)
    extern ColorBuffer g_aBloomUAV2[2];        // 320x192 (1/6)  
    extern ColorBuffer g_aBloomUAV3[2];        // 160x96  (1/12)
    extern ColorBuffer g_aBloomUAV4[2];        // 80x48   (1/24)
    extern ColorBuffer g_aBloomUAV5[2];        // 40x24   (1/48)
    extern ColorBuffer g_LumaLR;
    extern ByteAddressBuffer g_Histogram;
    extern ByteAddressBuffer g_FXAAWorkCounters;
    extern ByteAddressBuffer g_FXAAWorkQueue;
    extern TypedBuffer g_FXAAColorQueue;

    DepthBuffer *SceneDepthBuffer(int cam = -1);
    ColorBuffer *SceneColorBuffer(int cam = -1);
    ColorBuffer *SSAOFullScreen(int cam = -1);
    ColorBuffer *LinearDepth(int index, int cam = -1);
    ColorBuffer *DepthDownsize1(int cam = -1);
    ColorBuffer *DepthDownsize2(int cam = -1);
    ColorBuffer *DepthDownsize3(int cam = -1);
    ColorBuffer *DepthDownsize4(int cam = -1);
    ColorBuffer *DepthTiled1(int cam = -1);
    ColorBuffer *DepthTiled2(int cam = -1);
    ColorBuffer *DepthTiled3(int cam = -1);
    ColorBuffer *DepthTiled4(int cam = -1);
    ColorBuffer *AOMerged1(int cam = -1);
    ColorBuffer *AOMerged2(int cam = -1);
    ColorBuffer *AOMerged3(int cam = -1);
    ColorBuffer *AOMerged4(int cam = -1);
    ColorBuffer *AOSmooth1(int cam = -1);
    ColorBuffer *AOSmooth2(int cam = -1);
    ColorBuffer *AOSmooth3(int cam = -1);
    ColorBuffer *AOHighQuality1(int cam = -1);
    ColorBuffer *AOHighQuality2(int cam = -1);
    ColorBuffer *AOHighQuality3(int cam = -1);
    ColorBuffer *AOHighQuality4(int cam = -1);

    void InitializeRenderingBuffers(uint32_t NativeWidth, uint32_t NativeHeight );
    void ResizeDisplayDependentBuffers(uint32_t NativeWidth, uint32_t NativeHeight);
    void DestroyRenderingBuffers();

} // namespace Graphics
