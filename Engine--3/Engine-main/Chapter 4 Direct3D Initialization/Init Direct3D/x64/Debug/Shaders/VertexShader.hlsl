cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gView;
    float4x4 gProj;
};

struct VertexIn
{
    float3 PosL : POSITION;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
};

VertexOut main(VertexIn vin)
{
    VertexOut vout;
    
    // Вычисляем позицию в мировых координатах
    float4 worldPos = mul(float4(vin.PosL, 1.0f), gWorld);
    // Переводим в видовые координаты
    float4 viewPos = mul(worldPos, gView);
    // Переводим в проекционные координаты
    vout.PosH = mul(viewPos, gProj);
    
    // Передаем цвет дальше
    vout.Color = vin.Color;
    vout.TexCoord = vin.TexCoord;
    return vout;
}