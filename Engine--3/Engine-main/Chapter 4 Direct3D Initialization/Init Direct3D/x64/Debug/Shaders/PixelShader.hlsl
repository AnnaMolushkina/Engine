struct PSInput
{
    float4 Pos : SV_POSITION;
    float4 Color : COLOR;
};

float4 main(PSInput pin) : SV_TARGET
{
    return pin.Color;
}