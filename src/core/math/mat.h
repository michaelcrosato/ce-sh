// 4x4 matrices with the project transform contract:
//   * storage is row-major: m[row][col];
//   * math uses column vectors: p' = M * p, so (M1 * M2) applies M2 first;
//   * world space is right-handed, +Y up; translation lives in column 3.
// The same memory order is used on the GPU (see shaders/shared/layouts.hlsli).
#pragma once

#include "core/math/vec.h"

namespace lc::math {

struct Mat4 {
    float m[4][4] = {};

    static constexpr Mat4 Identity() {
        Mat4 r;
        r.m[0][0] = 1.0f;
        r.m[1][1] = 1.0f;
        r.m[2][2] = 1.0f;
        r.m[3][3] = 1.0f;
        return r;
    }

    static constexpr Mat4 Translation(Vec3 t) {
        Mat4 r = Identity();
        r.m[0][3] = t.x;
        r.m[1][3] = t.y;
        r.m[2][3] = t.z;
        return r;
    }

    static constexpr Mat4 Scale(Vec3 s) {
        Mat4 r;
        r.m[0][0] = s.x;
        r.m[1][1] = s.y;
        r.m[2][2] = s.z;
        r.m[3][3] = 1.0f;
        return r;
    }

    // Rotations follow the right-hand rule about the named positive axis.
    static Mat4 RotationX(float radians);
    static Mat4 RotationY(float radians);
    static Mat4 RotationZ(float radians);
    static Mat4 RotationAxis(Vec3 unitAxis, float radians);

    constexpr Vec4 Row(int r) const { return {m[r][0], m[r][1], m[r][2], m[r][3]}; }
    constexpr Vec4 Column(int c) const { return {m[0][c], m[1][c], m[2][c], m[3][c]}; }
    constexpr Vec3 TranslationPart() const { return {m[0][3], m[1][3], m[2][3]}; }

    Mat4 operator*(const Mat4& b) const;
    Vec4 operator*(Vec4 v) const;

    // Affine helpers: TransformPoint assumes row 3 is (0,0,0,1).
    Vec3 TransformPoint(Vec3 p) const;
    Vec3 TransformDirection(Vec3 d) const;

    Mat4 Transposed() const;
    float Determinant() const;

    // False when the matrix is singular (|det| < 1e-20); out is untouched then.
    bool TryInverse(Mat4& out) const;
    // Returns Identity for a singular matrix. Prefer TryInverse where singular input is possible.
    Mat4 Inverse() const;
};

// The first three rows of a Mat4, in the memory order D3D12 uses for instance transforms.
struct Mat3x4 {
    float m[3][4] = {};

    static constexpr Mat3x4 FromMat4(const Mat4& a) {
        Mat3x4 r;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 4; ++col) {
                r.m[row][col] = a.m[row][col];
            }
        }
        return r;
    }
};

bool NearlyEqual(const Mat4& a, const Mat4& b, float tolerance);

}  // namespace lc::math
