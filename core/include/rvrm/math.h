// 最小限のベクトル/クォータニオン/行列。エンジン非依存。
// 規約:
//   - 列ベクトル。行列 M を点 v に適用するのは M * v。
//   - Mat4 は列優先(m[col][row])。glTF の matrix と同じ並び。
//   - クォータニオンの積 a * b は「b を適用してから a を適用」。
#pragma once

#include <cmath>
#include <cstdint>

namespace rvrm {

struct Vec2 {
    float x = 0, y = 0;
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
    float& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v, const Vec3& fallback = {0, 0, 0}) {
    float l = length(v);
    return l > 1e-12f ? v / l : fallback;
}

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;
};

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    Quat() = default;
    constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    static Quat identity() { return {}; }
    static Quat fromAxisAngle(const Vec3& axis, float radians) {
        Vec3 a = normalize(axis, {0, 0, 1});
        float s = std::sin(radians * 0.5f);
        return {a.x * s, a.y * s, a.z * s, std::cos(radians * 0.5f)};
    }

    Quat operator*(const Quat& b) const {
        const Quat& a = *this;
        return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    }
    Quat conjugate() const { return {-x, -y, -z, w}; }
    // 単位クォータニオン前提の逆。
    Quat inverse() const { return conjugate(); }

    Vec3 rotate(const Vec3& v) const {
        // v' = v + 2w(q×v) + 2 q×(q×v)
        Vec3 q{x, y, z};
        Vec3 t = cross(q, v) * 2.0f;
        return v + t * w + cross(q, t);
    }
};

inline float dot(const Quat& a, const Quat& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Quat normalize(const Quat& q) {
    float l = std::sqrt(dot(q, q));
    if (l < 1e-12f) return {};
    return {q.x / l, q.y / l, q.z / l, q.w / l};
}

// from を to へ向ける最小回転(どちらも非ゼロ)。
inline Quat fromToRotation(const Vec3& from, const Vec3& to) {
    Vec3 f = normalize(from), t = normalize(to);
    float d = dot(f, t);
    if (d >= 1.0f - 1e-7f) return {};
    if (d <= -1.0f + 1e-7f) {
        // 180°: from に垂直な任意軸で回す
        Vec3 axis = cross({1, 0, 0}, f);
        if (length(axis) < 1e-4f) axis = cross({0, 1, 0}, f);
        return Quat::fromAxisAngle(axis, 3.14159265358979f);
    }
    Vec3 c = cross(f, t);
    return normalize(Quat{c.x, c.y, c.z, 1.0f + d});
}

inline Quat slerp(const Quat& a, Quat b, float t) {
    float d = dot(a, b);
    if (d < 0) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
    if (d > 0.9995f) {
        return normalize(Quat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                              a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
    }
    float th = std::acos(d), s = std::sin(th);
    float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

struct Mat3 {
    // m[col][row]
    float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

    static Mat3 fromQuat(const Quat& q) {
        Mat3 r;
        float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        r.m[0][0] = 1 - 2 * (yy + zz); r.m[0][1] = 2 * (xy + wz);     r.m[0][2] = 2 * (xz - wy);
        r.m[1][0] = 2 * (xy - wz);     r.m[1][1] = 1 - 2 * (xx + zz); r.m[1][2] = 2 * (yz + wx);
        r.m[2][0] = 2 * (xz + wy);     r.m[2][1] = 2 * (yz - wx);     r.m[2][2] = 1 - 2 * (xx + yy);
        return r;
    }
    Vec3 col(int c) const { return {m[c][0], m[c][1], m[c][2]}; }
    void setCol(int c, const Vec3& v) { m[c][0] = v.x; m[c][1] = v.y; m[c][2] = v.z; }
    Vec3 operator*(const Vec3& v) const {
        return {m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z,
                m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z,
                m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z};
    }
    Mat3 operator*(const Mat3& b) const {
        Mat3 r;
        for (int c = 0; c < 3; ++c) r.setCol(c, (*this) * b.col(c));
        return r;
    }
    Mat3 transposed() const {
        Mat3 r;
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 3; ++k) r.m[c][k] = m[k][c];
        return r;
    }
    float determinant() const { return dot(col(0), cross(col(1), col(2))); }
    // 正規直交(行列式 +1)の回転行列 → クォータニオン
    Quat toQuat() const {
        float tr = m[0][0] + m[1][1] + m[2][2];
        Quat q;
        if (tr > 0) {
            float s = std::sqrt(tr + 1.0f) * 2;
            q = {(m[1][2] - m[2][1]) / s, (m[2][0] - m[0][2]) / s, (m[0][1] - m[1][0]) / s, 0.25f * s};
        } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
            float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2;
            q = {0.25f * s, (m[1][0] + m[0][1]) / s, (m[2][0] + m[0][2]) / s, (m[1][2] - m[2][1]) / s};
        } else if (m[1][1] > m[2][2]) {
            float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2;
            q = {(m[1][0] + m[0][1]) / s, 0.25f * s, (m[2][1] + m[1][2]) / s, (m[2][0] - m[0][2]) / s};
        } else {
            float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2;
            q = {(m[2][0] + m[0][2]) / s, (m[2][1] + m[1][2]) / s, 0.25f * s, (m[0][1] - m[1][0]) / s};
        }
        return normalize(q);
    }
};

// 平行移動・回転・スケール
struct Transform {
    Vec3 t{0, 0, 0};
    Quat r{};
    Vec3 s{1, 1, 1};
};

struct Mat4 {
    // m[col][row]
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

    static Mat4 identity() { return {}; }
    static Mat4 fromArray(const float* a16) {  // 列優先 16 要素
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4; ++k) r.m[c][k] = a16[c * 4 + k];
        return r;
    }
    static Mat4 fromTRS(const Transform& x) {
        Mat3 rm = Mat3::fromQuat(x.r);
        Mat4 r;
        for (int c = 0; c < 3; ++c) {
            float sc = c == 0 ? x.s.x : (c == 1 ? x.s.y : x.s.z);
            for (int k = 0; k < 3; ++k) r.m[c][k] = rm.m[c][k] * sc;
            r.m[c][3] = 0;
        }
        r.m[3][0] = x.t.x; r.m[3][1] = x.t.y; r.m[3][2] = x.t.z; r.m[3][3] = 1;
        return r;
    }
    static Mat4 fromLinear(const Mat3& l, const Vec3& t = {0, 0, 0}) {
        Mat4 r;
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 3; ++k) r.m[c][k] = l.m[c][k];
        r.m[3][0] = t.x; r.m[3][1] = t.y; r.m[3][2] = t.z;
        return r;
    }

    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int k = 0; k < 4; ++k) {
                float s = 0;
                for (int i = 0; i < 4; ++i) s += m[i][k] * b.m[c][i];
                r.m[c][k] = s;
            }
        return r;
    }
    Vec3 transformPoint(const Vec3& v) const {
        return {m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z + m[3][0],
                m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z + m[3][1],
                m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z + m[3][2]};
    }
    Vec3 transformVector(const Vec3& v) const {
        return {m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z,
                m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z,
                m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z};
    }
    Vec3 translation() const { return {m[3][0], m[3][1], m[3][2]}; }
    Mat3 linear() const {
        Mat3 r;
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 3; ++k) r.m[c][k] = m[c][k];
        return r;
    }
    // スケール(シアなし前提)を除いた回転
    Quat rotation() const {
        Mat3 l = linear();
        for (int c = 0; c < 3; ++c) l.setCol(c, normalize(l.col(c), c == 0 ? Vec3{1, 0, 0} : (c == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1})));
        return l.toQuat();
    }
    // 一般の 4x4 逆行列(アフィン以外も可)。特異なら単位行列を返す。
    Mat4 inverse() const;
};

}  // namespace rvrm
