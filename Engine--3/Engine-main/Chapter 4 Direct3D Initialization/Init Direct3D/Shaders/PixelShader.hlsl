Texture2D gTexture : register(t0); // текстура из регистра t0
SamplerState gSampler : register(s0); // сэмплер для текстуры

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float4 Color : COLOR;
    float2 TexCoord : TEXCOORD;
};

float4 main(VertexOut pin) : SV_Target
{
    // Сэмплируем текстуру по UV-координатам
    float4 texColor = gTexture.Sample(gSampler, pin.TexCoord);
    //float2 flippedUV = float2(pin.TexCoord.x, 1.0f - pin.TexCoord.y);
    //float4 texColor = gTexture.Sample(gSampler, flippedUV);
    // Если хочешь только цвет, верни Color
    // Если хочешь цвет * текстура, верни pin.Color * texColor
    
    return texColor; // ИСПОЛЬЗУЕМ ТЕКСТУРУ
}