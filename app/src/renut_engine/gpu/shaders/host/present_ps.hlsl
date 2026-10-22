#include "utility.hlsli"

struct PSInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0
{
    float4 color = SampleBindless(g_Push.textureIndex, g_Push.samplerIndex, input.uv);
    if (g_Push.param0 < -0.5)
    {
        // bejeweled3-nx (2026-10-22): param0 < 0 selects the display ramp table (present.cpp, UpdateDisplayRampLocked;
        // an exponent is never negative), and param1 holds the bits of its slot. Each channel's 8-bit value indexes 256
        // 10-bit outputs, as the console's scan-out and the emulated GPU's apply_gamma_table do: row c of the 256x3
        // table, high 8 bits in r, low 2 in g, read with the point sampler at texel centres. The slot is read as it is
        // stored, like textureIndex: on deko3d a texture handle computed from a float (the first version) read nothing.
        const uint ramp = asuint(g_Push.param1);
        const float3 index = floor(saturate(color.rgb) * 255.0 + 0.5);
        float3 result;
        [unroll] for (uint c = 0; c < 3; ++c)
        {
            const float2 uv = float2((index[c] + 0.5) / 256.0, (float(c) + 0.5) / 3.0);
            const float2 v = SampleBindless(ramp, 1u /* kPointClampSamplerSlot */, uv).rg;
            result[c] = (round(v.x * 255.0) * 4.0 + round(v.y * 255.0)) / 1023.0;
        }
        color.rgb = result;
    }
    else
    {
        // param0 = output gamma exponent (1 = passthrough).
        color.rgb = pow(saturate(color.rgb), g_Push.param0);
    }
    return float4(color.rgb, 1.0);
}
