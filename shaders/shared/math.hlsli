// Small shared helpers. Keep them free of resource bindings.
#ifndef LC_MATH_HLSLI
#define LC_MATH_HLSLI

static const float LC_PI = 3.14159265358979323846f;

// Wang hash: cheap integer mixing for diagnostic colours.
uint WangHash(uint x) {
    x = (x ^ 61u) ^ (x >> 16);
    x *= 9u;
    x = x ^ (x >> 4);
    x *= 0x27d4eb2du;
    x = x ^ (x >> 15);
    return x;
}

// Distinct, reasonably bright colour per identifier. Diagnostic use only.
float3 HashColor(uint id) {
    const uint h = WangHash(id + 1u);
    const float3 c = float3(float(h & 0xFFu), float((h >> 8) & 0xFFu), float((h >> 16) & 0xFFu)) / 255.0;
    return c * 0.8 + 0.2;
}

// Column-vector transform with the first three rows of a 4x4 matrix: p' = M * (p, 1).
float3 TransformPoint(float4 rows[3], float3 p) {
    const float4 v = float4(p, 1.0);
    return float3(dot(rows[0], v), dot(rows[1], v), dot(rows[2], v));
}

// Column-vector transform of a direction (translation ignored): d' = M3x3 * d.
float3 TransformDirection(float4 rows[3], float3 d) {
    return float3(dot(rows[0].xyz, d), dot(rows[1].xyz, d), dot(rows[2].xyz, d));
}

#endif  // LC_MATH_HLSLI
