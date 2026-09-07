// Material evaluation for the explicit material types (spec §7). Diffuse surfaces are Lambertian
// and two-sided; mirrors are ideal delta reflectors; emitters emit from their front side only and
// otherwise behave like diffuse surfaces with their own reflectance; rough conductors are GGX
// microfacet surfaces (Trowbridge-Reitz NDF, height-correlated Smith masking-shadowing, Schlick
// Fresnel with F0 = reflectance, visible-normal sampling after Heitz 2018).
//
// Convention: `n` is the shading normal facing the incoming ray, `wo` points from the surface
// toward the previous vertex (against the ray), `wi` toward the next vertex. All unit, world space.
#ifndef LC_MATERIALS_HLSLI
#define LC_MATERIALS_HLSLI

#include "shared/layouts.hlsli"
#include "shared/math.hlsli"
#include "shared/sampling.hlsli"

bool IsDeltaMaterial(MaterialRecord m) {
    return m.type == LC_MATERIAL_MIRROR;
}

bool IsActiveEmitter(MaterialRecord m) {
    return m.type == LC_MATERIAL_EMITTER && (m.flags & LC_MATERIAL_FLAG_EMITTER_ON) != 0u;
}

// Emitted radiance leaving the surface toward direction `toViewer` (unit), given the unflipped
// geometric normal of the emitting triangle. One-sided: the back emits nothing.
float3 EmittedRadiance(MaterialRecord m, float3 geometricNormal, float3 toViewer) {
    if (!IsActiveEmitter(m) || dot(geometricNormal, toViewer) <= 0.0) {
        return 0.0;
    }
    return m.radiance;
}

// ---------------------------------------------------------------------------------------------
// GGX helpers (local frame: n = +Z)

float GgxAlpha(MaterialRecord m) {
    const float r = clamp(m.roughness, 0.02, 1.0);
    return max(r * r, 1e-4);
}

// Trowbridge-Reitz normal distribution.
float GgxD(float alpha, float nDotH) {
    const float a2 = alpha * alpha;
    const float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (LC_PI * d * d);
}

// Smith Lambda for GGX.
float GgxLambda(float alpha, float nDotV) {
    const float c2 = max(nDotV * nDotV, 1e-8);
    const float tan2 = (1.0 - c2) / c2;
    return (-1.0 + sqrt(1.0 + alpha * alpha * tan2)) * 0.5;
}

float GgxG1(float alpha, float nDotV) {
    return 1.0 / (1.0 + GgxLambda(alpha, nDotV));
}

// Height-correlated masking-shadowing.
float GgxG2(float alpha, float nDotV, float nDotL) {
    return 1.0 / (1.0 + GgxLambda(alpha, nDotV) + GgxLambda(alpha, nDotL));
}

float3 FresnelSchlick(float3 f0, float vDotH) {
    const float t = pow(1.0 - saturate(vDotH), 5.0);
    return f0 + (1.0 - f0) * t;
}

// Visible-normal sampling (Heitz, "Sampling the GGX Distribution of Visible Normals", JCGT 2018).
// `v` is the local-space view direction with v.z > 0. Returns a local-space microfacet normal.
float3 SampleGgxVndf(float alpha, float3 v, float2 u) {
    const float3 vh = normalize(float3(alpha * v.x, alpha * v.y, v.z));
    const float lensq = vh.x * vh.x + vh.y * vh.y;
    const float3 t1 = lensq > 0.0 ? float3(-vh.y, vh.x, 0.0) / sqrt(lensq) : float3(1.0, 0.0, 0.0);
    const float3 t2 = cross(vh, t1);
    const float r = sqrt(u.x);
    const float phi = 2.0 * LC_PI * u.y;
    const float p1 = r * cos(phi);
    float p2 = r * sin(phi);
    const float s = 0.5 * (1.0 + vh.z);
    p2 = (1.0 - s) * sqrt(max(0.0, 1.0 - p1 * p1)) + s * p2;
    const float3 nh = p1 * t1 + p2 * t2 + sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2)) * vh;
    return normalize(float3(alpha * nh.x, alpha * nh.y, max(0.0, nh.z)));
}

// ---------------------------------------------------------------------------------------------
// Generic BSDF interface used by the integrator for every non-delta material.

struct BsdfSample {
    float3 direction;  // wi, world space, unit.
    float pdf;         // Solid-angle density of `direction`.
    float3 weight;     // f * cos(theta_i) / pdf.
    bool valid;
};

bool IsRoughConductor(MaterialRecord m) {
    return m.type == LC_MATERIAL_ROUGH_CONDUCTOR;
}

// BRDF value f(wo, wi). Zero when wi is below the shading hemisphere.
float3 EvalBsdf(MaterialRecord m, float3 n, float3 wo, float3 wi) {
    const float nDotL = dot(n, wi);
    const float nDotV = dot(n, wo);
    if (nDotL <= 0.0 || nDotV <= 0.0) {
        return 0.0;
    }
    if (IsRoughConductor(m)) {
        const float alpha = GgxAlpha(m);
        const float3 h = normalize(wo + wi);
        const float nDotH = saturate(dot(n, h));
        const float vDotH = saturate(dot(wo, h));
        const float d = GgxD(alpha, nDotH);
        const float g = GgxG2(alpha, nDotV, nDotL);
        const float3 f = FresnelSchlick(m.reflectance, vDotH);
        return d * g * f / (4.0 * nDotV * nDotL);
    }
    return m.reflectance / LC_PI;  // Diffuse and emitter surfaces.
}

// Solid-angle density that SampleBsdf would produce `wi` with.
float PdfBsdf(MaterialRecord m, float3 n, float3 wo, float3 wi) {
    const float nDotL = dot(n, wi);
    const float nDotV = dot(n, wo);
    if (nDotL <= 0.0 || nDotV <= 0.0) {
        return 0.0;
    }
    if (IsRoughConductor(m)) {
        const float alpha = GgxAlpha(m);
        const float3 h = normalize(wo + wi);
        const float nDotH = saturate(dot(n, h));
        return GgxG1(alpha, nDotV) * GgxD(alpha, nDotH) / (4.0 * nDotV);
    }
    return nDotL / LC_PI;
}

BsdfSample SampleBsdf(MaterialRecord m, float3 n, float3 wo, float2 u) {
    BsdfSample s = (BsdfSample)0;
    const float nDotV = dot(n, wo);
    if (nDotV <= 0.0) {
        return s;
    }
    if (IsRoughConductor(m)) {
        const float alpha = GgxAlpha(m);
        float3 t, b;
        OrthonormalBasis(n, t, b);
        const float3 vLocal = float3(dot(wo, t), dot(wo, b), nDotV);
        const float3 hLocal = SampleGgxVndf(alpha, vLocal, u);
        const float3 h = t * hLocal.x + b * hLocal.y + n * hLocal.z;
        const float vDotH = dot(wo, h);
        if (vDotH <= 0.0) {
            return s;
        }
        const float3 wi = 2.0 * vDotH * h - wo;
        const float nDotL = dot(n, wi);
        if (nDotL <= 0.0) {
            return s;  // Reflected below the surface: single-scattering energy loss, not an error.
        }
        const float nDotH = saturate(hLocal.z);
        const float g1 = GgxG1(alpha, nDotV);
        s.direction = wi;
        s.pdf = g1 * GgxD(alpha, nDotH) / (4.0 * nDotV);
        s.weight = FresnelSchlick(m.reflectance, vDotH) * (GgxG2(alpha, nDotV, nDotL) / g1);
        s.valid = s.pdf > 0.0;
        return s;
    }
    float pdf = 0.0;
    s.direction = CosineSampleHemisphere(u, n, pdf);
    s.pdf = pdf;
    s.weight = m.reflectance;  // f * cos / pdf for a Lambertian surface.
    s.valid = pdf > 0.0;
    return s;
}

#endif  // LC_MATERIALS_HLSLI
