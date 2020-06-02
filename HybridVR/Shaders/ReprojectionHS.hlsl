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


RWStructuredBuffer<float> gDiffResult : register(u0);

struct HS_Input
{
    float3 posW : POSW;
    float quadId : QUADID;
    float4 posH : SV_POSITION;
    float2 texC : TEXCRD;
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

static float gThreshold = 0.997; // TODO: Do we want to be able to change this? Yes
static float gTessFactor = 16;
static uint gQuadCountX = 16;

#ifdef _BINOCULAR_METRIC
float getTessellationFactor(float quadid)
{
    // Do I have to split myself?
    if (gDiffResult[(int) quadid] > gThreshold)
    {
        return gTessFactor;
    }
    // Do my neighbors split - so do I have, too?
    else
    {
        // left
        
        if (gDiffResult[((int) quadid) - 1] > gThreshold)
        {
            return gTessFactor;
        }
        // right
        else if (gDiffResult[((int) quadid) + 1] > gThreshold)
        {
            return gTessFactor;
        }
        // top
        else if (gDiffResult[((int) quadid) - gQuadCountX] > gThreshold)
        {
            return gTessFactor;
        }
        // bottom
        else if (gDiffResult[((int) quadid) + gQuadCountX] > gThreshold)
        {
            return gTessFactor;
        }
        

#ifdef _EIGHT_NEIGHBOR
        // top right 
        else if (gDiffResult[((int) quadid) - gQuadCountX + 1] > gThreshold)
        {
            return gTessFactor;
        }
        // top left  
        else if (gDiffResult[((int) quadid) - gQuadCountX - 1] > gThreshold)
        {
            return gTessFactor;
        }
        // bottom right 
        else if (gDiffResult[((int) quadid) + gQuadCountX + 1] > gThreshold)
        {
            return gTessFactor;
        }
        // bottom left
        else if (gDiffResult[((int) quadid) + gQuadCountX - 1] > gThreshold)
        {
            return gTessFactor;
        }
#endif
    }

    // I don't need to split myself
    return 1.0;
}
#else
float getTessellationFactor(float quadid)
{
    // Do I have to split myself?
    if (gDiffResult[(int) quadid] < gThreshold)
    {
        return gTessFactor;
    }
    // Do my neighbors split - so do I have, too?
    else
    {
        // left
        
        if (gDiffResult[((int) quadid) - 1] < gThreshold)
        {
            return gTessFactor;
        }
        // right
        else if (gDiffResult[((int) quadid) + 1] < gThreshold)
        {
            return gTessFactor;
        }
        // top
        else if (gDiffResult[((int) quadid) - gQuadCountX] < gThreshold)
        {
            return gTessFactor;
        }
        // bottom
        else if (gDiffResult[((int) quadid) + gQuadCountX] < gThreshold)
        {
            return gTessFactor;
        }
        

#ifdef _EIGHT_NEIGHBOR
        // top right 
        else if (gDiffResult[((int) quadid) - gQuadCountX + 1] < gThreshold)
        {
            return gTessFactor;
        }
        // top left  
        else if (gDiffResult[((int) quadid) - gQuadCountX - 1] < gThreshold)
        {
            return gTessFactor;
        }
        // bottom right 
        else if (gDiffResult[((int) quadid) + gQuadCountX + 1] < gThreshold)
        {
            return gTessFactor;
        }
        // bottom left
        else if (gDiffResult[((int) quadid) + gQuadCountX - 1] < gThreshold)
        {
            return gTessFactor;
        }
#endif
    }

    // I don't need to split myself
    return 1.0;
}
#endif

HS_Constant_Output HSConstant(InputPatch<HS_Input, 4> inputPatch)
{
    HS_Constant_Output output;

    float fac = getTessellationFactor(inputPatch[0].quadId); // workaround solution for missing patchID parameter (see Github issue)

    output.edges[0] = fac;
    output.edges[1] = fac;
    output.edges[2] = fac;
    output.edges[3] = fac;

    output.inside[0] = fac;
    output.inside[1] = fac;

    return output;
};

[domain("quad")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(4)]
[patchconstantfunc("HSConstant")]
HS_Output main(InputPatch<HS_Input, 4> inputPatch, uint pointId : SV_OutputControlPointID, uint patchId : SV_PrimitiveID)
{
    HS_Output output;
    output.posW = inputPatch[pointId].posW;
    output.posH = inputPatch[pointId].posH;
    output.texC = inputPatch[pointId].texC;
    return output;
}
