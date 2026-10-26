#include "rvrm/convert.h"

#include <cmath>
#include <utility>

namespace rvrm {

namespace {
Mat3 orientation(VrmVersion version, const ConvertOptions& o) {
    // glTF → UE: 列 = 各 glTF 軸の行き先
    Mat3 base;
    base.setCol(0, {0, -1, 0});  // +X(VRM のキャラの左)→ -Y(UE の左)
    base.setCol(1, {0, 0, 1});   // +Y(上)→ +Z
    base.setCol(2, {1, 0, 0});   // +Z(前)→ +X
    Mat3 pre;  // VRM 0.x: Y 軸 180°
    if (version == VrmVersion::Vrm0 && o.rotateVrm0) {
        pre.setCol(0, {-1, 0, 0});
        pre.setCol(1, {0, 1, 0});
        pre.setCol(2, {0, 0, -1});
    }
    return Mat3::fromQuat(o.sourceMeshRotation) * base * pre;
}

Vec3 absv(const Vec3& v) { return {std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)}; }
}  // namespace

Mat3 conversionLinear(VrmVersion version, const ConvertOptions& o) {
    Mat3 m = orientation(version, o);
    for (int c = 0; c < 3; ++c) m.setCol(c, m.col(c) * o.unitsPerMeter);
    return m;
}

void convertModel(Model& model, const ConvertOptions& o) {
    const Mat3 M = orientation(model.version, o);  // 正規直交(行列式 -1)
    const Mat3 Mt = M.transposed();
    const float s = o.unitsPerMeter;
    const Mat4 C = Mat4::fromLinear(conversionLinear(model.version, o));
    const Mat4 Ci = C.inverse();

    auto pt = [&](const Vec3& v) { return (M * v) * s; };
    auto dir = [&](const Vec3& v) { return M * v; };

    for (Node& n : model.nodes) {
        n.rest.t = pt(n.rest.t);
        n.rest.r = (M * Mat3::fromQuat(n.rest.r) * Mt).toQuat();
        n.rest.s = absv(M * n.rest.s);
    }
    for (Skin& sk : model.skins)
        for (Mat4& ib : sk.inverseBind) ib = C * ib * Ci;
    for (Mesh& mesh : model.meshes) {
        for (Primitive& p : mesh.primitives) {
            for (Vec3& v : p.positions) v = pt(v);
            for (Vec3& v : p.normals) v = dir(v);
            for (MorphTarget& mt : p.morphs) {
                for (Vec3& v : mt.dPosition) v = pt(v);
                for (Vec3& v : mt.dNormal) v = dir(v);
            }
            if (o.flipWinding)
                for (size_t i = 0; i + 2 < p.indices.size(); i += 3) std::swap(p.indices[i + 1], p.indices[i + 2]);
        }
    }
    for (Material& m : model.materials) m.outlineWidth *= s;
    for (Collider& c : model.colliders) {
        c.offset = pt(c.offset);
        c.tail = pt(c.tail);
        c.radius *= s;
    }
    for (Spring& sp : model.springs) {
        sp.virtualTailLength *= s;
        for (SpringJoint& j : sp.joints) {
            j.hitRadius *= s;
            j.gravityDir = dir(j.gravityDir);
        }
    }
    model.unitsPerMeter *= s;
    model.basis = C * model.basis;
}

}  // namespace rvrm
