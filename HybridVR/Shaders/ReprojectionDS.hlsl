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

  Authors: Niko Wissmann, Martin Misiak
 */

//import ShaderCommon;

SamplerState gLinearSampler : register(s0);
Texture2D gDepthTex : register(t0);

cbuffer ReprojInput : register(b0)
{
    float4x4 reprojectionMat;
	float3 camPosLeft;
    float3 camPosRight;
    float depthThreshold;
    float angleThreshold;
};

struct HS_Constant_Output
{
    float edges[4] : SV_TessFactor;
    float inside[2] : SV_InsideTessFactor;
};

struct HS_Output
{
    float3 posW : POSW;
    float4 posH : SV_POSITION;
    float2 texC : TEXCRD;
};

struct VertexOutput
{
    float3 posW : POSW;
    float4 posH : SV_POSITION;
    float2 texC : TEXCRD;
    float occFlag : DOCCFLAG;
};

[domain("quad")]
VertexOutput main(HS_Constant_Output input, float2 UV : SV_DomainLocation, const OutputPatch<HS_Output, 4> patch, uint patchId : SV_PrimitiveID)
{
    VertexOutput output;
    
    float3 topMidpoint = lerp(patch[0].posW, patch[1].posW, UV.x);
    float3 bottomMidpoint = lerp(patch[3].posW, patch[2].posW, UV.x);

    float2 topMidpointUV = lerp(patch[0].texC, patch[1].texC, UV.x);
    float2 bottomMidpointUV = lerp(patch[3].texC, patch[2].texC, UV.x);

    float4 posH = float4(lerp(topMidpoint, bottomMidpoint, UV.y), 1);
    output.texC = float2(lerp(topMidpointUV, bottomMidpointUV, UV.y));

    float z = clamp(gDepthTex.SampleLevel(gLinearSampler, output.texC.xy, 0).r, 0.000001, 0.99999); // clamp is needed to avoid flickering if quad grid does not cover any geometry

    float4 posWH = mul(float4(posH.xy, z, posH.w), reprojectionMat);

    output.posW = posWH.xyz;
    output.posH = float4(posWH.xy, z, posWH.w);
    /*output.posW = 0.1337;
    output.posH = 0.1337;*/

    //output.posW = posH.xyz; // [Debug] screen quad (no reprojection)
    //output.posH = posH;

    float zLeft   = gDepthTex.SampleLevel(gLinearSampler, output.texC.xy, 0, int2(-1, 0)).r;
    float zRight  = gDepthTex.SampleLevel(gLinearSampler, output.texC.xy, 0, int2(1, 0)).r;
    //float zTop    = gDepthTex.SampleLevel(gLinearSampler, output.texC.xy, 0, int2(0, 1)).r;
    //float zBottom = gDepthTex.SampleLevel(gLinearSampler, output.texC.xy, 0, int2(0, -1)).r;
    //float zMiddle = gDepthTex.SampleLevel(gLinearSampler, output.texC.xy, 0, int2(0, 0)).r;

    output.occFlag = abs(2 * zLeft - 2 * zRight); // sobel operator
    //output.occFlag = 0;
    //output.occFlag = abs(-4 * zMiddle + zLeft + zRight + zTop + zBottom); // laplace operator 
    return output;
}
