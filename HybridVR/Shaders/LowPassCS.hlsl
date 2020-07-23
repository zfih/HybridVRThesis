
Texture2DArray<float3> HighResImage : register(t0);
RWTexture2DArray<float3> LowResPassed : register(u0);
cbuffer CB0 : register(b0)
{
    uint cam;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    float weights[5][5] =
    {
        {
            1.0278445 / 273.0f,
			4.10018648 / 273.0f,
			6.49510362 / 273.0f,
			4.10018648 / 273.0f,
			1.0278445 / 273.0f
        },
        {
            4.10018648 / 273.0f,
			16.35610171 / 273.0f,
			25.90969361 / 273.0f,
			16.35610171 / 273.0f,
			4.10018648 / 273.0f
        },
        {
            6.49510362 / 273.0f,
			25.90969361 / 273.0f, 
			41.0435344 / 273.0f,
			25.90969361 / 273.0f,
			6.49510362 / 273.0f
        },
        {
            4.10018648 / 273.0f,
			16.35610171 / 273.0f,
			25.90969361 / 273.0f,
			16.35610171 / 273.0f,
			4.10018648 / 273.0f
        },
        {
            1.0278445 / 273.0f,
			4.10018648 / 273.0f,
			6.49510362 / 273.0f,
			4.10018648 / 273.0f,
			1.0278445 / 273.0f
        }
    };

    float3 colorSum = float3(0, 0, 0);

    for (int y = -2; y < 3; y++)
    {
        for (int x = -2; x < 3; x++)
        {
            float weight = weights[x + 2][y + 2];
            colorSum += weight * HighResImage[uint3(DTid.x + x, DTid.y + y, cam)];
        }
    }
    
    // TODO : FIX Cam 0 for now
    LowResPassed[uint3(DTid.x, DTid.y, 0)] = colorSum;
    LowResPassed[uint3(DTid.x, DTid.y, 0)] = cam.xxx;

}
