// Deterministic sampling: a hash-based generator keyed by pixel, sample index, dimension, and
// seed (no clock-derived state), plus the sampling routines the integrator needs.
//
// Dimension allocation (documented in docs/RENDERING.md):
//   0, 1              camera pixel jitter
//   2 + b * 8 + 0     bounce b: emitter selection
//   2 + b * 8 + 1     bounce b: emitter triangle selection
//   2 + b * 8 + 2, 3  bounce b: point on the emitter triangle
//   2 + b * 8 + 4, 5  bounce b: BSDF direction
//   2 + b * 8 + 6, 7  reserved (Russian roulette, later)
#ifndef LC_SAMPLING_HLSLI
#define LC_SAMPLING_HLSLI

#include "shared/math.hlsli"

static const uint LC_DIM_CAMERA = 0;
static const uint LC_DIM_BOUNCE_BASE = 2;
static const uint LC_DIM_PER_BOUNCE = 8;
static const uint LC_DIM_EMITTER_SELECT = 0;
static const uint LC_DIM_EMITTER_TRIANGLE = 1;
static const uint LC_DIM_EMITTER_POINT = 2;
static const uint LC_DIM_BSDF = 4;

// PCG output permutation on a 32-bit LCG state; good bit mixing for hash-based sampling.
uint PcgHash(uint v) {
    const uint state = v * 747796405u + 2891336453u;
    const uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uint Hash4(uint a, uint b, uint c, uint d) {
    uint h = PcgHash(a);
    h = PcgHash(h + b);
    h = PcgHash(h + c);
    h = PcgHash(h + d);
    return h;
}

struct SampleKey {
    uint pixelKey;     // y * 65536 + x
    uint sampleIndex;
    uint seed;
};

SampleKey MakeSampleKey(uint2 pixel, uint sampleIndex, uint seed) {
    SampleKey k;
    k.pixelKey = pixel.y * 65536u + pixel.x;
    k.sampleIndex = sampleIndex;
    k.seed = seed;
    return k;
}

// Uniform in [0, 1) with 24 significant bits.
float Rand(SampleKey key, uint dimension) {
    const uint h = Hash4(key.pixelKey, key.sampleIndex, dimension, key.seed);
    return float(h >> 8u) * (1.0 / 16777216.0);
}

float2 Rand2(SampleKey key, uint dimension) {
    return float2(Rand(key, dimension), Rand(key, dimension + 1u));
}

// Orthonormal basis around a unit normal (Duff et al., "Building an Orthonormal Basis, Revisited").
void OrthonormalBasis(float3 n, out float3 t, out float3 b) {
    const float sign = n.z >= 0.0 ? 1.0 : -1.0;
    const float a = -1.0 / (sign + n.z);
    const float c = n.x * n.y * a;
    t = float3(1.0 + sign * n.x * n.x * a, sign * c, -sign * n.x);
    b = float3(c, sign + n.y * n.y * a, -n.y);
}

// Cosine-weighted direction around n; pdf = cos(theta) / pi.
float3 CosineSampleHemisphere(float2 u, float3 n, out float pdf) {
    const float r = sqrt(u.x);
    const float phi = 2.0 * LC_PI * u.y;
    const float x = r * cos(phi);
    const float y = r * sin(phi);
    const float z = sqrt(max(0.0, 1.0 - u.x));
    float3 t, b;
    OrthonormalBasis(n, t, b);
    pdf = z / LC_PI;
    return normalize(t * x + b * y + n * z);
}

// Uniform barycentrics over a triangle (square-root parameterization).
float3 UniformTriangleBarycentrics(float2 u) {
    const float su = sqrt(u.x);
    const float b1 = 1.0 - su;
    const float b2 = u.y * su;
    return float3(1.0 - b1 - b2, b1, b2);
}

// Scale-aware ray origin offset along the geometric normal (Ray Tracing Gems, chapter 6,
// "A Fast and Robust Method for Avoiding Self-Intersection"): an integer ULP offset for points
// away from the origin, a fixed float offset close to it. n must point toward the side the new
// ray travels on.
float3 OffsetRay(float3 p, float3 n) {
    const float origin = 1.0 / 32.0;
    const float floatScale = 1.0 / 65536.0;
    const float intScale = 256.0;
    const int3 ofI = int3(intScale * n);
    const float3 pI = float3(asfloat(asint(p.x) + ((p.x < 0.0) ? -ofI.x : ofI.x)),
                             asfloat(asint(p.y) + ((p.y < 0.0) ? -ofI.y : ofI.y)),
                             asfloat(asint(p.z) + ((p.z < 0.0) ? -ofI.z : ofI.z)));
    return float3(abs(p.x) < origin ? p.x + floatScale * n.x : pI.x,
                  abs(p.y) < origin ? p.y + floatScale * n.y : pI.y,
                  abs(p.z) < origin ? p.z + floatScale * n.z : pI.z);
}

float LinearToSrgbChannel(float c) {
    return c <= 0.0031308 ? 12.92 * c : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

float3 LinearToSrgb(float3 c) {
    return float3(LinearToSrgbChannel(c.x), LinearToSrgbChannel(c.y), LinearToSrgbChannel(c.z));
}

float Luminance709(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

#endif  // LC_SAMPLING_HLSLI
