// Simple diagnostic scale path (spec §13): bilinear resampling of the internal-resolution display
// image to the presented size. Not a reconstruction method; it exists so a 1280x720 trace can be
// shown in a 1920x1080 window while the native path stays the reference.
#include "shared/layouts.hlsli"

ConstantBuffer<FrameConstants> gFrame : register(b0);   // renderSize = internal size.
ConstantBuffer<GuideConstants> gGuide : register(b2);   // outputSize = presented size.
RWTexture2D<float4> gDisplay : register(u0);            // Internal display image (RGBA8 UNORM, sRGB encoded).
RWTexture2D<float4> gDisplayOut : register(u19);        // Presented display image at outputSize.

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gGuide.outputSize.x || dtid.y >= gGuide.outputSize.y) {
        return;
    }
    // Pixel-centre mapping: output centre -> internal continuous coordinate.
    const float2 scale = float2(gFrame.renderSize) / float2(gGuide.outputSize);
    const float2 src = (float2(dtid.xy) + 0.5) * scale - 0.5;
    const float2 base = floor(src);
    const float2 f = src - base;
    const int2 maxCoord = int2(gFrame.renderSize) - 1;
    const int2 c00 = clamp(int2(base), int2(0, 0), maxCoord);
    const int2 c11 = clamp(int2(base) + 1, int2(0, 0), maxCoord);
    const float4 a = gDisplay[uint2(c00.x, c00.y)];
    const float4 b = gDisplay[uint2(c11.x, c00.y)];
    const float4 c = gDisplay[uint2(c00.x, c11.y)];
    const float4 d = gDisplay[uint2(c11.x, c11.y)];
    gDisplayOut[dtid.xy] = lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}
