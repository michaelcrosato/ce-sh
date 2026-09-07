// Small vector types. Units are metres and radians unless a name says otherwise.
#pragma once

#include <cmath>
#include <cstdint>

namespace lc::math {

inline constexpr float kPi = 3.14159265358979323846f;

constexpr float DegreesToRadians(float degrees) { return degrees * (kPi / 180.0f); }
constexpr float RadiansToDegrees(float radians) { return radians * (180.0f / kPi); }

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct UInt2 {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};

constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
constexpr Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
constexpr Vec3 operator*(float s, Vec3 a) { return {a.x * s, a.y * s, a.z * s}; }
constexpr Vec3 operator*(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
constexpr Vec3 operator/(Vec3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }
constexpr Vec3& operator+=(Vec3& a, Vec3 b) { a = a + b; return a; }
constexpr Vec3& operator-=(Vec3& a, Vec3 b) { a = a - b; return a; }
constexpr Vec3& operator*=(Vec3& a, float s) { a = a * s; return a; }
constexpr bool operator==(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

constexpr float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Right-handed cross product: Cross(+X, +Y) == +Z.
constexpr Vec3 Cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

constexpr float LengthSquared(Vec3 v) { return Dot(v, v); }
inline float Length(Vec3 v) { return std::sqrt(LengthSquared(v)); }

// Returns the zero vector for a zero-length input instead of NaN.
inline Vec3 Normalize(Vec3 v) {
    const float len = Length(v);
    return len > 0.0f ? v / len : Vec3{};
}

constexpr Vec3 Lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
constexpr Vec3 Min(Vec3 a, Vec3 b) { return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z}; }
constexpr Vec3 Max(Vec3 a, Vec3 b) { return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z}; }

constexpr bool NearlyEqual(float a, float b, float tolerance) {
    const float d = a > b ? a - b : b - a;
    return d <= tolerance;
}
constexpr bool NearlyEqual(Vec3 a, Vec3 b, float tolerance) {
    return NearlyEqual(a.x, b.x, tolerance) && NearlyEqual(a.y, b.y, tolerance) && NearlyEqual(a.z, b.z, tolerance);
}

constexpr Vec4 ToVec4(Vec3 v, float w) { return {v.x, v.y, v.z, w}; }
constexpr Vec3 Xyz(Vec4 v) { return {v.x, v.y, v.z}; }

inline bool IsFinite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

}  // namespace lc::math
