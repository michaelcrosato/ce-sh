// Material evaluation for the explicit material types (spec §7). Diffuse surfaces are Lambertian
// and two-sided; mirrors are ideal delta reflectors; emitters emit from their front side only and
// otherwise behave like diffuse surfaces with their own reflectance.
#ifndef LC_MATERIALS_HLSLI
#define LC_MATERIALS_HLSLI

#include "shared/layouts.hlsli"
#include "shared/math.hlsli"

bool IsDeltaMaterial(MaterialRecord m) {
    return m.type == LC_MATERIAL_MIRROR;
}

bool IsActiveEmitter(MaterialRecord m) {
    return m.type == LC_MATERIAL_EMITTER && (m.flags & LC_MATERIAL_FLAG_EMITTER_ON) != 0u;
}

// Lambertian BRDF value for a diffuse or emitter surface.
float3 DiffuseBrdf(MaterialRecord m) {
    return m.reflectance / LC_PI;
}

// Emitted radiance leaving the surface toward direction `toViewer` (unit), given the unflipped
// geometric normal of the emitting triangle. One-sided: the back emits nothing.
float3 EmittedRadiance(MaterialRecord m, float3 geometricNormal, float3 toViewer) {
    if (!IsActiveEmitter(m) || dot(geometricNormal, toViewer) <= 0.0) {
        return 0.0;
    }
    return m.radiance;
}

#endif  // LC_MATERIALS_HLSLI
