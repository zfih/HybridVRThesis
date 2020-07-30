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

SamplerState gLinearSampler : register(s0);
Texture2D gLeftEyeTex : register(t1);
Texture2D gLeftEyeNormalTex : register(t2);

static float gThreshold = 0.001; // TODO: Do we want to be able to change this? // This was 0.008
static float3 gClearColor = float3(0, 0, 0);

MRT main(VertexOutput vOut)
{
    float4 color;
    float4 normal;

#define _PERFRAGMENT
//#define _SHOWDISOCCLUSION


#ifdef _PERFRAGMENT
    color = float4(gLeftEyeTex.SampleLevel(gLinearSampler, vOut.texC, 0).rgb, 1);
    normal = gLeftEyeNormalTex.SampleLevel(gLinearSampler, vOut.texC, 0);
#endif
#ifdef _SHOWUVS
    color = float4(1, 1, 1, 1);
#endif
#ifdef _SHOWDISOCCLUSION
        if (vOut.occFlag > gThreshold)
        {
            //color = float4(1, 0, 0, 0);
            color = float4(gLeftEyeTex.SampleLevel(gLinearSampler, vOut.texC, 0).rgb, 1);
            normal = float4(gLeftEyeNormalTex.SampleLevel(gLinearSampler, vOut.texC, 0).rgb, 1);
        }
#else
    if (vOut.occFlag > gThreshold)
    {
        discard;
    }
#endif

    //color = float4(0, 1, 0, 1);
	MRT mrt;
	mrt.Color = color;
	mrt.Normal = normal;
	mrt.Color.a = 0;
	mrt.Normal.w = 0;
    return mrt;
}
