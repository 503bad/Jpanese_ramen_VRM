#include "rvrm/firstperson.h"

namespace rvrm {

FirstPersonFlag sectionFirstPersonFlag(const Model& model, const RenderSection& section) {
    for (const auto& a : model.firstPerson)
        if (a.node == section.node) return a.flag;
    return FirstPersonFlag::Auto;
}

std::vector<bool> headSubtreeNodes(const Model& model) {
    std::vector<bool> r(model.nodes.size(), false);
    const int head = model.humanNode(HumanBone::head);
    if (head < 0) return r;
    for (size_t n = 0; n < model.nodes.size(); ++n) {
        size_t steps = 0;
        for (int p = static_cast<int>(n); p >= 0 && steps <= model.nodes.size(); p = model.nodes[p].parent, ++steps) {
            if (p == head) {
                r[n] = true;
                break;
            }
        }
    }
    return r;
}

std::vector<uint32_t> firstPersonIndices(const Model& model, const RenderSection& section,
                                         const std::vector<bool>& headNodes, float weightThreshold) {
    const Node& node = model.nodes[section.node];
    const Primitive& prim = model.meshes[section.mesh].primitives[section.primitive];
    const size_t vc = prim.positions.size();

    // 頂点ごとに「頭の影響を受けるか」
    std::vector<bool> inHead(vc, false);
    if (node.skin >= 0 && prim.joints.size() == vc && prim.weights.size() == vc) {
        const Skin& skin = model.skins[node.skin];
        for (size_t v = 0; v < vc; ++v) {
            const float w[4] = {prim.weights[v].x, prim.weights[v].y, prim.weights[v].z, prim.weights[v].w};
            for (int k = 0; k < 4; ++k) {
                int jn = skin.joints[prim.joints[v][k]];
                if (w[k] > weightThreshold && jn >= 0 && jn < static_cast<int>(headNodes.size()) && headNodes[jn]) {
                    inHead[v] = true;
                    break;
                }
            }
        }
    } else if (section.node < static_cast<int>(headNodes.size()) && headNodes[section.node]) {
        inHead.assign(vc, true);  // 頭の下に剛体で付いているメッシュ
    }

    std::vector<uint32_t> out;
    out.reserve(prim.indices.size());
    for (size_t i = 0; i + 2 < prim.indices.size(); i += 3) {
        uint32_t a = prim.indices[i], b = prim.indices[i + 1], c = prim.indices[i + 2];
        if (inHead[a] || inHead[b] || inHead[c]) continue;
        out.push_back(a);
        out.push_back(b);
        out.push_back(c);
    }
    return out;
}

}  // namespace rvrm
