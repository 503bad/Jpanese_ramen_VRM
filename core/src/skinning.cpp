#include "rvrm/skinning.h"

#include <algorithm>

namespace rvrm {

void Skinner::init(const Model& model) {
    sections_.clear();
    for (size_t n = 0; n < model.nodes.size(); ++n) {
        const Node& node = model.nodes[n];
        if (node.mesh < 0 || node.mesh >= static_cast<int>(model.meshes.size())) continue;
        const Mesh& mesh = model.meshes[node.mesh];
        for (size_t p = 0; p < mesh.primitives.size(); ++p)
            sections_.push_back({static_cast<int>(n), node.mesh, static_cast<int>(p), mesh.primitives[p].material});
    }
}

void Skinner::skin(const Model& model, const Pose& pose, const std::vector<std::vector<float>>& morphWeights,
                   std::vector<SkinnedSectionOutput>& out) const {
    out.resize(sections_.size());
    for (size_t i = 0; i < sections_.size(); ++i) skinSection(model, pose, morphWeights, i, out[i]);
}

void Skinner::skinSection(const Model& model, const Pose& pose, const std::vector<std::vector<float>>& morphWeights,
                          size_t section, SkinnedSectionOutput& out) const {
    const RenderSection& rs = sections_[section];
    const Node& node = model.nodes[rs.node];
    const Primitive& prim = model.meshes[rs.mesh].primitives[rs.primitive];
    const size_t vc = prim.positions.size();

    // モーフ適用
    out.positions = prim.positions;
    out.normals = prim.normals;
    const std::vector<float>* mw =
        rs.mesh < static_cast<int>(morphWeights.size()) ? &morphWeights[rs.mesh] : nullptr;
    if (mw) {
        for (size_t k = 0; k < prim.morphs.size() && k < mw->size(); ++k) {
            float w = (*mw)[k];
            if (w == 0) continue;
            const MorphTarget& mt = prim.morphs[k];
            if (mt.dPosition.size() == vc)
                for (size_t v = 0; v < vc; ++v) out.positions[v] += mt.dPosition[v] * w;
            if (mt.dNormal.size() == vc)
                for (size_t v = 0; v < vc; ++v) out.normals[v] += mt.dNormal[v] * w;
        }
    }

    const bool skinned = node.skin >= 0 && !prim.joints.empty() && prim.weights.size() == vc;
    if (!skinned) {
        const Mat4& g = pose.global[rs.node];
        for (size_t v = 0; v < vc; ++v) {
            out.positions[v] = g.transformPoint(out.positions[v]);
            out.normals[v] = normalize(g.transformVector(out.normals[v]), {0, 0, 1});
        }
        return;
    }

    const Skin& skin = model.skins[node.skin];
    std::vector<Mat4> mats(skin.joints.size());
    for (size_t j = 0; j < skin.joints.size(); ++j) {
        int jn = skin.joints[j];
        mats[j] = (jn >= 0 ? pose.global[jn] : Mat4::identity()) * skin.inverseBind[j];
    }
    for (size_t v = 0; v < vc; ++v) {
        const auto& J = prim.joints[v];
        const Vec4& W = prim.weights[v];
        const float w[4] = {W.x, W.y, W.z, W.w};
        // 行列をブレンドしてから 1 回だけ変換する
        Mat4 m;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r) m.m[c][r] = 0;
        for (int k = 0; k < 4; ++k) {
            if (w[k] == 0) continue;
            const Mat4& s = mats[J[k]];
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 3; ++r) m.m[c][r] += s.m[c][r] * w[k];
        }
        m.m[3][3] = 1;
        out.positions[v] = m.transformPoint(out.positions[v]);
        out.normals[v] = normalize(m.transformVector(out.normals[v]), {0, 0, 1});
    }
}

void evaluateExpressions(const Model& model, const std::vector<float>& expressionWeights,
                         std::vector<std::vector<float>>& morphWeights) {
    morphWeights.resize(model.meshes.size());
    for (size_t m = 0; m < model.meshes.size(); ++m) {
        size_t count = 0;
        for (const auto& p : model.meshes[m].primitives) count = std::max(count, p.morphs.size());
        morphWeights[m].assign(count, 0.0f);
        for (size_t k = 0; k < count && k < model.meshes[m].defaultWeights.size(); ++k)
            morphWeights[m][k] = model.meshes[m].defaultWeights[k];
    }
    for (size_t e = 0; e < model.expressions.size() && e < expressionWeights.size(); ++e) {
        const Expression& ex = model.expressions[e];
        float w = std::clamp(expressionWeights[e], 0.0f, 1.0f);
        if (ex.isBinary) w = w > 0.5f ? 1.0f : 0.0f;
        if (w == 0) continue;
        for (const MorphBind& b : ex.binds) {
            if (b.mesh < 0 || b.mesh >= static_cast<int>(morphWeights.size())) continue;
            auto& mw = morphWeights[b.mesh];
            if (b.morphIndex < 0 || b.morphIndex >= static_cast<int>(mw.size())) continue;
            mw[b.morphIndex] = std::min(1.0f, mw[b.morphIndex] + b.weight * w);
        }
    }
}

int findExpression(const Model& model, const std::string& presetOrName) {
    for (size_t i = 0; i < model.expressions.size(); ++i)
        if (model.expressions[i].preset == presetOrName) return static_cast<int>(i);
    for (size_t i = 0; i < model.expressions.size(); ++i)
        if (model.expressions[i].name == presetOrName) return static_cast<int>(i);
    return -1;
}

}  // namespace rvrm
