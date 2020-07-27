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

#define HLSL
#include "ModelViewerRaytracing.h"

[shader("miss")]
void Miss(inout RayPayload payload)
{
	if (!payload.SkipShading && !IsReflection && payload.Bounces < 1)
    {
        g_screenOutput[int3(DispatchRaysIndex().xy, 0)] = float4(0, 0, 0, 1);
        g_screenOutput[int3(DispatchRaysIndex().xy, 1)] = float4(0, 0, 0, 1);// TODO: Do second view differently
    }
}

