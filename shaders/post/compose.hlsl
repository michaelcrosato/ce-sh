// Denoised-mode composition (spec §13 order: trace -> denoise -> compose -> exposure -> interface).
// Modulates the denoised signals with the material factors the guided trace stored, adds the
// noise-free emission, writes the production linear image and the exposed sRGB display image,
// accumulates the recomposed raw samples (an invariant: their mean must equal the reference), and
// draws the diagnostic overlays on the display image only.
#include "shared/layouts.hlsli"
#include "shared/math.hlsli"
#include "shared/sampling.hlsli"
#include "NRD.hlsli"

ConstantBuffer<FrameConstants> gFrame : register(b0);
ConstantBuffer<GuideConstants> gGuide : register(b2);
RWTexture2D<float4> gDisplay : register(u0);
RWTexture2D<float4> gLinear : register(u1);
RWTexture2D<float4> gAccum : register(u4);
RWTexture2D<float4> gAccumSq : register(u5);
RWTexture2D<float4> gMotion : register(u7);
RWTexture2D<float4> gNormalRoughness : register(u8);
RWTexture2D<float> gViewZ : register(u9);
RWTexture2D<float4> gDiffIn : register(u10);
RWTexture2D<float4> gSpecIn : register(u11);
RWTexture2D<float4> gDiffFactor : register(u12);
RWTexture2D<float4> gSpecFactor : register(u13);
RWTexture2D<float4> gEmission : register(u14);
RWTexture2D<float4> gDirect : register(u15);
RWTexture2D<float4> gDiffOut : register(u16);
RWTexture2D<float4> gSpecOut : register(u17);
RWTexture2D<float4> gValidation : register(u18);

float3 Exposed(float3 c) {
    return LinearToSrgb(saturate(c * gGuide.exposure));
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.renderSize.x || dtid.y >= gFrame.renderSize.y) {
        return;
    }
    const uint2 p = dtid.xy;
    const float viewZ = gViewZ[p];
    const bool valid = abs(viewZ) < gGuide.denoisingRange;  // NRD leaves other pixels untouched.

    const float4 diffOut = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(gDiffOut[p]);  // .w = history length (frames).
    const float4 specOut = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(gSpecOut[p]);
    const float4 diffIn = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(gDiffIn[p]);   // The same YCoCg unpacking undoes the front end.
    const float4 specIn = REBLUR_BackEnd_UnpackRadianceAndNormHitDist(gSpecIn[p]);
    const float3 diffFactor = gDiffFactor[p].rgb;
    const float3 specFactor = gSpecFactor[p].rgb;
    const float3 emission = gEmission[p].rgb;

    const float3 diffuse = valid ? diffFactor * diffOut.rgb : 0.0;
    const float3 specular = valid ? specFactor * specOut.rgb : 0.0;
    const float3 production = emission + diffuse + specular;
    const float3 raw = emission + (valid ? diffFactor * diffIn.rgb + specFactor * specIn.rgb : 0.0);

    // Mean of the recomposed raw samples since the last history reset (same sums as the reference path).
    const bool reset = (gGuide.flags & LC_GUIDE_FLAG_RESET) != 0u;
    float4 sum = reset ? float4(0.0, 0.0, 0.0, 0.0) : gAccum[p];
    float4 sumSq = reset ? float4(0.0, 0.0, 0.0, 0.0) : gAccumSq[p];
    sum.xyz += raw;
    sumSq.xyz += raw * raw;
    const float count = float(gFrame.sampleIndex + 1u);
    sum.w = count;
    sumSq.w = count;
    gAccum[p] = sum;
    gAccumSq[p] = sumSq;

    gLinear[p] = float4(production, count);

    float3 display = Exposed(production);
    if ((gGuide.flags & LC_GUIDE_FLAG_OVERLAY) != 0u) {
        switch (gGuide.viewMode) {
            case LC_VIEW_NORMALS:
                display = NRD_FrontEnd_UnpackNormalAndRoughness(gNormalRoughness[p]).xyz * 0.5 + 0.5;
                break;
            case LC_VIEW_DEPTH:
            case LC_VIEW_VIEWZ:
                display = saturate(abs(viewZ) / 20.0).xxx;
                break;
            case LC_VIEW_MOTION:
                display = saturate(gMotion[p].xyz * 10.0 + 0.5);
                break;
            case LC_VIEW_HISTORY: {
                const float2 age = saturate(float2(diffOut.w, specOut.w) / max(float(gGuide.historyFrames), 1.0));
                const bool rejected = valid && (age.x < 0.02 || age.y < 0.02);
                display = float3(rejected ? 1.0 : 0.0, age.x, age.y);
                break;
            }
            case LC_VIEW_DIFFUSE:
                display = Exposed(diffuse);
                break;
            case LC_VIEW_SPECULAR:
                display = Exposed(specular);
                break;
            case LC_VIEW_RAW:
                display = Exposed(raw);
                break;
            case LC_VIEW_DIRECT:
                display = Exposed(gDirect[p].rgb);
                break;
            case LC_VIEW_INDIRECT:
                display = Exposed(max(diffuse + specular - gDirect[p].rgb, 0.0));
                break;
            case LC_VIEW_VALIDATION:
                if ((gGuide.flags & LC_GUIDE_FLAG_VALIDATION) != 0u) {
                    const float4 v = gValidation[p];
                    display = lerp(display, v.rgb, v.a);
                }
                break;
            default:
                break;  // Identity and winding views need the diagnostic pass; keep the production image.
        }
    }
    gDisplay[p] = float4(display, 1.0);
}
