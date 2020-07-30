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


[shader("raygeneration")]
void RayGen()
{
    if (g_dynamic.curCam == 1 && g_screenOutput[int3(DispatchRaysIndex().xy, 1)].a != 0x0)
    {
        return;
    }

    float3 origin, direction;
    GenerateCameraRay(DispatchRaysIndex().xy, origin, direction);

    RayDesc rayDesc = { origin,
        0.0f,
        direction,
        FLT_MAX };
    RayPayload payload;
    payload.SkipShading = false;
    payload.RayHitT = FLT_MAX;
    payload.Bounces = 0;
	payload.Reflectivity = 1;
	TraceRay(g_accel, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, ~0, 0, 1, 0, rayDesc, payload);
}

