#include "rvrm/springbone.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace rvrm {

namespace {
float uniformScale(const Mat4& m) { return length(m.linear().col(0)); }

Vec3 closestOnSegment(const Vec3& a, const Vec3& b, const Vec3& p) {
    Vec3 ab = b - a;
    float l2 = dot(ab, ab);
    if (l2 < 1e-12f) return a;
    float t = std::clamp(dot(p - a, ab) / l2, 0.0f, 1.0f);
    return a + ab * t;
}
}  // namespace

void SpringBoneSim::init(const Model& model, const Skeleton& skeleton, const SpringBoneOptions& options) {
    model_ = &model;
    skeleton_ = &skeleton;
    opt_ = options;
    joints_.clear();
    springs_.clear();
    initialized_ = false;
    const Pose& rest = skeleton.restPose();
    const int nodeCount = static_cast<int>(skeleton.nodeCount());

    for (size_t si = 0; si < model.springs.size(); ++si) {
        const Spring& s = model.springs[si];
        SpringInfo info;
        info.center = s.center;
        std::set<int> cols;
        for (int g : s.colliderGroups)
            for (int c : model.colliderGroups[g].colliders)
                if (model.colliders[c].node >= 0) cols.insert(c);
        info.colliders.assign(cols.begin(), cols.end());
        springs_.push_back(std::move(info));

        const size_t n = s.joints.size();
        const size_t simulated = s.virtualTailLength > 0 ? n : (n > 0 ? n - 1 : 0);
        for (size_t i = 0; i < simulated; ++i) {
            const SpringJoint& sj = s.joints[i];
            if (sj.node < 0 || sj.node >= nodeCount) continue;
            JointState js;
            js.node = sj.node;
            js.spring = static_cast<int>(si);
            js.hitRadius = sj.hitRadius;
            js.stiffness = sj.stiffness;
            js.gravityPower = sj.gravityPower;
            js.gravityDir = sj.gravityDir;
            js.dragForce = sj.dragForce;
            js.initLocalRot = rest.local[sj.node].r;
            if (i + 1 < n) {
                js.tailNode = s.joints[i + 1].node;
                Vec3 local = rest.global[sj.node].inverse().transformPoint(rest.global[js.tailNode].translation());
                if (length(local) < 1e-6f) continue;
                js.boneAxis = normalize(local);
            } else {
                // 仮想の尾: 親→自分の方向へ延長
                const Transform& l = rest.local[sj.node];
                Vec3 dir = l.r.inverse().rotate(l.t);
                if (length(dir) < 1e-6f) continue;
                js.boneAxis = normalize(dir);
                float sc = uniformScale(rest.global[sj.node]);
                js.virtualLength = s.virtualTailLength / (sc > 1e-6f ? sc : 1.0f);
            }
            joints_.push_back(js);
        }
    }
}

Mat4 SpringBoneSim::centerMatrix(const Pose& pose, const Mat4& toWorld, int center) const {
    return center >= 0 ? toWorld * pose.global[center] : Mat4::identity();
}

Vec3 SpringBoneSim::tailWorld(const Pose& pose, const Mat4& toWorld, const JointState& j) const {
    if (j.tailNode >= 0) return (toWorld * pose.global[j.tailNode]).translation();
    return (toWorld * pose.global[j.node]).transformPoint(j.boneAxis * j.virtualLength);
}

void SpringBoneSim::reset(const Pose& pose, const Mat4& toWorld) {
    for (JointState& j : joints_) {
        Mat4 ci = centerMatrix(pose, toWorld, springs_[j.spring].center).inverse();
        j.currentTail = j.prevTail = ci.transformPoint(tailWorld(pose, toWorld, j));
    }
    initialized_ = true;
}

void SpringBoneSim::update(Pose& pose, const Mat4& toWorld, float deltaTime) {
    if (!initialized_) {
        reset(pose, toWorld);
        return;
    }
    float dt = std::clamp(deltaTime, 0.0f, opt_.maxDeltaTime);
    if (dt <= 0) return;
    int steps = std::clamp(static_cast<int>(std::ceil(dt / opt_.maxStep)), 1, std::max(1, opt_.maxSubSteps));
    for (int i = 0; i < steps; ++i) step(pose, toWorld, dt / steps);
}

void SpringBoneSim::step(Pose& pose, const Mat4& toWorld, float dt) {
    const float upm = model_->unitsPerMeter;
    const float worldScale = uniformScale(toWorld);
    const Quat worldRot = toWorld.rotation();

    for (JointState& j : joints_) {
        const SpringInfo& sp = springs_[j.spring];
        const int parent = skeleton_->parent(j.node);
        const Mat4 nodeWorld = toWorld * pose.global[j.node];
        const Vec3 head = nodeWorld.translation();
        const Quat parentRot = parent >= 0 ? (toWorld * pose.global[parent]).rotation() : worldRot;

        const Mat4 C = centerMatrix(pose, toWorld, sp.center);
        const Vec3 cur = C.transformPoint(j.currentTail);
        const Vec3 prev = C.transformPoint(j.prevTail);
        const float boneLength = length(tailWorld(pose, toWorld, j) - head);
        if (boneLength < 1e-6f) continue;

        const Quat restWorld = parentRot * j.initLocalRot;
        const Vec3 restDir = restWorld.rotate(j.boneAxis);
        Vec3 next = cur + (cur - prev) * (1.0f - j.dragForce)
                  + restDir * (j.stiffness * dt * upm * worldScale)
                  + worldRot.rotate(j.gravityDir) * (j.gravityPower * dt * upm * worldScale);
        next = head + normalize(next - head, restDir) * boneLength;

        for (int ci : sp.colliders) {
            const Collider& c = model_->colliders[ci];
            const Mat4 cw = toWorld * pose.global[c.node];
            const float radius = c.radius * uniformScale(cw) + j.hitRadius * worldScale;
            const Vec3 p0 = cw.transformPoint(c.offset);
            const Vec3 p = c.shape == Collider::Shape::Capsule ? closestOnSegment(p0, cw.transformPoint(c.tail), next) : p0;
            const Vec3 d = next - p;
            const float dl = length(d);
            if (dl < radius) {
                next = p + normalize(d, restDir) * radius;
                next = head + normalize(next - head, restDir) * boneLength;
            }
        }

        j.prevTail = j.currentTail;
        j.currentTail = C.inverse().transformPoint(next);

        const Vec3 to = restWorld.inverse().rotate(next - head);
        pose.local[j.node].r = normalize(j.initLocalRot * fromToRotation(j.boneAxis, to));
        skeleton_->updateSubtree(pose, j.node);
    }
}

}  // namespace rvrm
