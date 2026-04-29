#pragma once
// HellVerdict — types.hpp
// Copyright © 2026 Neofilisoft / Studio Balmung
// Lightweight math types compatible with Balmung::Vec3/Mat4

#include <cmath>
#include <algorithm>
#include <cstdint>

namespace HellVerdict {

// ── Scalar helpers ────────────────────────────────────────────────────────────
constexpr float PI       = 3.14159265358979323846f;
constexpr float DEG2RAD  = PI / 180.0f;
constexpr float RAD2DEG  = 180.0f / PI;
constexpr float EPSILON  = 0.001f;   // Collision epsilon — prevents jitter
constexpr float EPSILON2 = EPSILON * EPSILON;

inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
inline float lerpf (float a, float b, float t)   { return a + (b - a) * t; }
inline float signf (float v)                      { return (v > 0.f) ? 1.f : (v < 0.f) ? -1.f : 0.f; }

// ── Vec2 ─────────────────────────────────────────────────────────────────────
struct Vec2 {
    float x = 0.f, y = 0.f;

    Vec2 operator+(const Vec2& o) const { return {x+o.x, y+o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x-o.x, y-o.y}; }
    Vec2 operator*(float s)       const { return {x*s, y*s}; }
    Vec2& operator+=(const Vec2& o) { x+=o.x; y+=o.y; return *this; }

    float len2() const { return x*x + y*y; }
    float len()  const { return std::sqrt(len2()); }
    Vec2  norm() const { float l = len(); return l > EPSILON ? Vec2{x/l, y/l} : Vec2{}; }
};

inline float dot(Vec2 a, Vec2 b) { return a.x*b.x + a.y*b.y; }

// ── Vec3 ─────────────────────────────────────────────────────────────────────
struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;

    Vec3 operator+(const Vec3& o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x-o.x, y-o.y, z-o.z}; }
    Vec3 operator*(float s)       const { return {x*s, y*s, z*s}; }
    Vec3 operator-()              const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x+=o.x; y+=o.y; z+=o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x-=o.x; y-=o.y; z-=o.z; return *this; }
    Vec3& operator*=(float s)       { x*=s; y*=s; z*=s; return *this; }

    float len2() const { return x*x + y*y + z*z; }
    float len()  const { return std::sqrt(len2()); }
    Vec3  norm() const { float l = len(); return l > EPSILON ? Vec3{x/l, y/l, z/l} : Vec3{}; }
    Vec2  xz()   const { return {x, z}; }
};

inline float dot  (Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vec3  cross(Vec3 a, Vec3 b) {
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

// ── Mat4 (column-major) ───────────────────────────────────────────────────────
struct Mat4 {
    float m[16] = {};
    static Mat4 identity() {
        Mat4 r; r.m[0]=r.m[5]=r.m[10]=r.m[15]=1.f; return r;
    }
};

inline Mat4 mat4_mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k)
                r.m[col*4+row] += a.m[k*4+row] * b.m[col*4+k];
    return r;
}

inline Mat4 mat4_perspective(float fov, float aspect, float near, float far) {
    float f = 1.f / std::tan(fov * 0.5f);
    Mat4 m{};
    m.m[0]  =  f / aspect;
    m.m[5]  =  f;
    m.m[10] =  (far + near) / (near - far);
    m.m[11] = -1.f;
    m.m[14] =  (2.f * far * near) / (near - far);
    return m;
}

inline Mat4 mat4_look_at(Vec3 eye, Vec3 center, Vec3 up) {
    Vec3 f = (center - eye).norm();
    Vec3 s = cross(f, up).norm();
    Vec3 u = cross(s, f);
    Mat4 m = Mat4::identity();
    m.m[0]=s.x; m.m[4]=s.y; m.m[8] =s.z; m.m[12]=-dot(s,eye);
    m.m[1]=u.x; m.m[5]=u.y; m.m[9] =u.z; m.m[13]=-dot(u,eye);
    m.m[2]=-f.x;m.m[6]=-f.y;m.m[10]=-f.z;m.m[14]= dot(f,eye);
    return m;
}

inline Mat4 mat4_translate(Vec3 t) {
    Mat4 m = Mat4::identity();
    m.m[12]=t.x; m.m[13]=t.y; m.m[14]=t.z;
    return m;
}

// ── AABB ─────────────────────────────────────────────────────────────────────
struct AABB {
    Vec3 min, max;

    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 half()   const { return (max - min) * 0.5f; }

    bool overlaps(const AABB& o) const {
        return (max.x - EPSILON) > o.min.x && (min.x + EPSILON) < o.max.x
            && (max.y - EPSILON) > o.min.y && (min.y + EPSILON) < o.max.y
            && (max.z - EPSILON) > o.min.z && (min.z + EPSILON) < o.max.z;
    }

    static AABB from_center(Vec3 c, Vec3 half) {
        return { {c.x-half.x, c.y-half.y, c.z-half.z},
                 {c.x+half.x, c.y+half.y, c.z+half.z} };
    }
};

// ── Color ─────────────────────────────────────────────────────────────────────
struct Color3 { float r, g, b; };

constexpr Color3 COL_RED      = {0.8f, 0.1f, 0.1f};
constexpr Color3 COL_DARKGRAY = {0.18f,0.18f,0.18f};
constexpr Color3 COL_GRAY     = {0.35f,0.35f,0.35f};
constexpr Color3 COL_BROWN    = {0.45f,0.28f,0.12f};
constexpr Color3 COL_GREEN    = {0.1f, 0.5f, 0.15f};
constexpr Color3 COL_FLESH    = {0.75f,0.55f,0.45f};
constexpr Color3 COL_ORANGE   = {0.9f, 0.45f, 0.1f};
constexpr Color3 COL_WHITE    = {1.f, 1.f, 1.f};
constexpr Color3 COL_YELLOW   = {1.f, 0.85f, 0.1f};
constexpr Color3 COL_BLOOD    = {0.55f,0.05f,0.05f};

} // namespace HellVerdict
