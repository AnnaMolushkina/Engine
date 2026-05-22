struct VSInput
{
    float3 Pos : POSITION;
    float4 Color : COLOR;
};

struct VSOutput
{
    float4 Pos : SV_POSITION;
    float4 Color : COLOR;
};

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorldViewProj;
};

VSOutput main(VSInput vin)
{
    VSOutput vout;
    vout.Pos = mul(float4(vin.Pos, 1.0f), gWorldViewProj);
    vout.Color = vin.Color;
    return vout;
}