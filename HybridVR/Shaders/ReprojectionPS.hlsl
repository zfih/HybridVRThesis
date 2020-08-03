/*
  Copyrighted(c) 2020, TH Köln.All rights reserved. Redistribution and use in source and binary forms, with or without modification,
  are permitted provided that the following conditions are met :
 
  * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
  * Neither the name of TH Köln nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT OWNER
  OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES(INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  Authors: Niko Wissmann
 */

struct VertexOutput
{
    float3 posW : POSW;
    float4 posH : SV_POSITION;
    float2 texC : TEXCRD;
    float occFlag : DOCCFLAG;
};

struct MRT
{
	float4 Color : SV_Target0;
	float4 Normal : SV_Target1;
};

cbuffer ReprojInput : register(b0)
{
    float4x4 g_reprojectionMat;
    float3 g_camPosLeft;
    float3 g_camPosRight;
    float g_depthThreshold;
    float g_angleThreshold;
};

SamplerState gLinearSampler : register(s0);
Texture2D gLeftEyeTex : register(t1);
Texture2D gLeftEyeNormalTex : register(t2);
Texture2D gLeftEyeRawTex : register(t3);

static float3 gClearColor = float3(0, 0, 0);

MRT main(VertexOutput vOut)
{
    float4 color;
    float4 normal;

    color = float4(gLeftEyeTex.SampleLevel(gLinearSampler, vOut.texC, 0).rgb, 1);
    color = float4(gLeftEyeRawTex.SampleLevel(gLinearSampler, vOut.texC, 0).rgb, 1);
    normal = gLeftEyeNormalTex.SampleLevel(gLinearSampler, vOut.texC, 0);

    if (vOut.occFlag > g_depthThreshold)
    {
        //discard;
    }

    //// IAPC

    // Get angles
    float3 leftDir = normalize(g_camPosLeft - vOut.posW);
    float3 rightDir = normalize(g_camPosRight - vOut.posW);
	
    float angle = max(dot(leftDir, rightDir), 0);
	
	//// Compare
	//if(angle > g_angleThreshold)
	//{
 //       discard;
	//}

	// Set Color
	
	MRT mrt;
	mrt.Color = color;
	mrt.Normal = normal;

    if (g_angleThreshold > 0 && g_angleThreshold < 0.002)
    {
        mrt.Color = float4(1, 0.7, 0, 1);
    }
    return mrt;
}
