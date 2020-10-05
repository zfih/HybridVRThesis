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
float depthThreshold;
float angleThreshold;
float angleBlendingRange;
int debugColors;
};

SamplerState gLinearSampler : register(s0);
Texture2D gDepthTex : register(t0);
Texture2D gLeftEyeTex : register(t1);
Texture2D gLeftEyeNormalTex : register(t2);
Texture2D gLeftEyeRawTex : register(t3);

static const float PI = 3.141592;
static const float TWO_PI = PI * 2.0;
static const float PI_HALF = PI / 2.0;
static const float PI_FOURTH = PI / 4.0;
static const float PI_EIGHTH = PI / 8.0;


static float3 gClearColor = float3(0, 0, 0);


float3 RemoveSRGBCurve(float3 x)
{
	// Approximately pow(x, 2.2)
	return x < 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}

float3 ApplySRGBCurve(float3 x)
{
	// Approximately pow(x, 1.0 / 2.2)
	return x < 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

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

bool inRange(float n, float min, float max)
{
	return min < n && n < max;
}

MRT main(VertexOutput vOut)
{
	// Discard if depth difference too big.
	if(vOut.occFlag > depthThreshold)
	{
		discard;
	}
	float4 normal_ratio = gLeftEyeNormalTex.SampleLevel(gLinearSampler, vOut.texC, 0);
	float initRatio = normal_ratio.w;
	
	float4 color = 0;
	float4 colorRefl = gLeftEyeTex.SampleLevel(gLinearSampler, vOut.texC, 0);
	float4 colorRaw = gLeftEyeRawTex.SampleLevel(gLinearSampler, vOut.texC, 0);


	double halfRange = angleBlendingRange / 2;
	double lower = angleThreshold - halfRange;
	double upper = angleThreshold + halfRange;

	float depth = gDepthTex.SampleLevel(gLinearSampler, vOut.texC, 0);
	float angle = abs(2 * atan(0.065 / (2 * depth)) - PI);

	// Ratio is 0 when we're reusing reflections
	// Ratio is > 0  but < 1 when we're blending reflections
	// Ratio is 1 when we're redoing reflections
	float ratio = saturate((angle - lower) / angleBlendingRange);
	color = lerp(colorRefl, colorRaw, ratio);

	// Add debug colors
	if(debugColors)
	{
		if(angle > upper) // Angle not good enough, redo
		{
			color += float4(0.1, 0.0, 0.1, 0);
		}
		else if(inRange(angle, lower, upper)) // Blend
		{
			color += float4(0.1, 0.1, 0, 0);
		}
		else // Angle is below lower, conserve pixel
		{
			color += float4(0, 0.1, 0, 0);
		}
	}


	MRT mrt;
	float specular = color.w;
	
	ratio *= normal_ratio.w;

	
	specular += ratio == 0 && specular == 0;

	mrt.Color = color;
	mrt.Color.a = specular;
	mrt.Normal = normal_ratio;
	mrt.Normal.w = ratio;
	
	return mrt;
}
