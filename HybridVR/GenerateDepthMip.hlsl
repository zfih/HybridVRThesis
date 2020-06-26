#ifndef NON_POWER_OF_TWO
#define NON_POWER_OF_TWO 0
#endif

RWTexture2D<float> OutMip1 : register(u0);
RWTexture2D<float> OutMip2 : register(u1);
RWTexture2D<float> OutMip3 : register(u2);
Texture2D<float> SrcMip : register(t0);

cbuffer CB0 : register(b0)
{
    uint SrcMipLevel;    // Texture level of source mip
    uint NumMipLevels;    // Number of OutMips to write: [1, 4]
    float2 TexelSize;    // 1.0 / OutMip1.Dimensions
}

// The reason for separating channels is to reduce bank conflicts in the
// local data memory controller.  A large stride will cause more threads
// to collide on the same memory bank.
groupshared float gs_R[64];

void StoreColor(uint Index, float Color)
{
    gs_R[Index] = Color.r;
}

float LoadColor(uint Index)
{
    return float(gs_R[Index]);
}

[RootSignature(RootSig)]
[numthreads(8, 8, 1)]
void main(uint GI : SV_GroupIndex, uint3 DTid : SV_DispatchThreadID)
{
    StoreColor(GI, SrcMip);
    GroupMemoryBarrierWithGroupSync();

    // With low three bits for X and high three bits for Y, this bit mask
    // (binary: 001001) checks that X and Y are even.
    if ((GI & 0x9) == 0)
    {
        float Src2 = LoadColor(GI + 0x01);
        float Src3 = LoadColor(GI + 0x08);
        float Src4 = LoadColor(GI + 0x09);
        Src1 = max(Src2, max(Src3, Src4));

        OutMip1[DTid.xy / 2] = PackColor(Src1);
        StoreColor(GI, Src1);
    }

    if (NumMipLevels == 1)
        return;

    GroupMemoryBarrierWithGroupSync();

    // This bit mask (binary: 011011) checks that X and Y are multiples of four.
    if ((GI & 0x1B) == 0)
    {
        float Src2 = LoadColor(GI + 0x02);
        float Src3 = LoadColor(GI + 0x10);
        float Src4 = LoadColor(GI + 0x12);
        Src1 = max(Src2, max(Src3, Src4));

        OutMip2[DTid.xy / 4] = PackColor(Src1);
        StoreColor(GI, Src1);
    }

    if (NumMipLevels == 2)
        return;

    GroupMemoryBarrierWithGroupSync();

    // This bit mask would be 111111 (X & Y multiples of 8), but only one
    // thread fits that criteria.
    if (GI == 0)
    {
        float Src2 = LoadColor(GI + 0x04);
        float Src3 = LoadColor(GI + 0x20);
        float Src4 = LoadColor(GI + 0x24);
        Src1 = max(Src2, max(Src3, Src4));

        OutMip3[DTid.xy / 8] = PackColor(Src1);
    }
}
