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
float4x4 reprojectionMat;
float4x4 camToWorldMat;
float3 camPosLeft;
float depthThreshold;
float3 camPosRight;
float angleThreshold;
};

SamplerState gLinearSampler : register(s0);
Texture2D gLeftEyeTex : register(t1);
Texture2D gLeftEyeNormalTex : register(t2);
Texture2D gLeftEyeRawTex : register(t3);

static float3 gClearColor = float3(0, 0, 0);

bool equalsEpsilon(float a, float b, float epsilon)
{
	float diff = abs(a - b);
	return diff < epsilon;
}

bool equalsEpsilon(float3 a, float3 b, float epsilon)
{
	float3 diff = abs(a - b);
	return diff.x < epsilon && diff.y < epsilon && diff.z < epsilon;
}


bool equalsEpsilon(float4 a, float4 b, float epsilon)
{
	float4 diff = abs(a - b);
	return diff.x < epsilon && diff.y < epsilon && diff.z < epsilon && diff.w < epsilon;
}

MRT main(VertexOutput vOut)
{
	// Discard if depth difference too big.
	if(vOut.occFlag > depthThreshold)
	{
		discard;
		// a = 0 && w = 0 => Full retrace
	}

	float4 color;
	float4 normal;

	//// IAPC

	// Get pixel to world space
	float4 p = float4(vOut.posW, 1);
	p = mul(camToWorldMat, p);
	p = p / p.w;

	// Get angles
	double3 leftDir = normalize((double3)p - (double3)camPosLeft);
	double3 rightDir = normalize((double3)p - (double3)camPosRight);

	double angle = pow(dot(leftDir, rightDir), 1000);

	if(angle < angleThreshold) // Angle not good enough, redo
	{
		color = float4(gLeftEyeRawTex.SampleLevel(gLinearSampler, vOut.texC, 0).rgb, 1);
		normal = gLeftEyeNormalTex.SampleLevel(gLinearSampler, vOut.texC, 0);
		// W != 0, A != 0
	}
	else // Angle is good, conserve pixel
	{
		color = float4(gLeftEyeTex.SampleLevel(gLinearSampler, vOut.texC, 0).rgb, 1);
		normal = float4(0, 0, 0, 0);
		// W == 0, A != 0
	}

	MRT mrt;
	mrt.Color = color;
	mrt.Normal = normal;
	return mrt;
}
