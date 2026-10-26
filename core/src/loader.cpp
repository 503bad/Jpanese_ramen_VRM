#include "rvrm/loader.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>

#include "cgltf.h"
#include "rvrm/json.h"

namespace rvrm {

namespace {

template <class T>
int indexOf(const T* base, const T* p) {
    return p ? static_cast<int>(p - base) : -1;
}

// 列優先 4x4 を TRS に分解(シアは無視)
Transform decompose(const float* m16) {
    Mat4 m = Mat4::fromArray(m16);
    Transform t;
    t.t = m.translation();
    Mat3 l = m.linear();
    Vec3 c0 = l.col(0), c1 = l.col(1), c2 = l.col(2);
    t.s = {length(c0), length(c1), length(c2)};
    if (l.determinant() < 0) t.s.x = -t.s.x;
    Mat3 r;
    r.setCol(0, t.s.x != 0 ? c0 / t.s.x : Vec3{1, 0, 0});
    r.setCol(1, t.s.y != 0 ? c1 / t.s.y : Vec3{0, 1, 0});
    r.setCol(2, t.s.z != 0 ? c2 / t.s.z : Vec3{0, 0, 1});
    t.r = r.toQuat();
    return t;
}

class Loader {
public:
    Loader(cgltf_data* d, Model& m, const LoadOptions& o) : g_(d), m_(m), opt_(o) {}

    bool run(std::string* error) {
        if (!readNodes(error)) return false;
        if (!readMeshes(error)) return false;
        if (!readSkins(error)) return false;
        readImagesAndTextures();
        readMaterials();

        const cgltf_extension* vrm1 = findExt("VRMC_vrm");
        const cgltf_extension* vrm0 = findExt("VRM");
        if (vrm1) {
            m_.version = VrmVersion::Vrm1;
            json::Value v;
            if (!parseExt(vrm1, v, error)) return false;
            readVrm1(v);
            if (const cgltf_extension* sb = findExt("VRMC_springBone")) {
                json::Value s;
                if (parseExt(sb, s, nullptr)) readVrm1Spring(s);
                else warn("VRMC_springBone の JSON を解析できませんでした");
            }
        } else if (vrm0) {
            m_.version = VrmVersion::Vrm0;
            json::Value v;
            if (!parseExt(vrm0, v, error)) return false;
            readVrm0(v);
        } else if (!opt_.allowNonVrm) {
            if (error) *error = "VRM 拡張(VRMC_vrm / VRM)が見つかりません。VRM ファイルではない可能性があります";
            return false;
        }

        if (m_.version != VrmVersion::Unknown && m_.humanNode(HumanBone::hips) < 0) {
            if (error) *error = "ヒューマノイドの hips が定義されていません";
            return false;
        }
        for (int i = 0; i < kHumanBoneCount; ++i) {
            auto b = static_cast<HumanBone>(i);
            if (m_.version != VrmVersion::Unknown && humanBoneRequired(b) && m_.humanoid[i] < 0)
                warn(std::string("必須ボーンがありません: ") + humanBoneName(b));
        }
        return true;
    }

private:
    cgltf_data* g_;
    Model& m_;
    const LoadOptions& opt_;

    void warn(std::string s) { m_.warnings.push_back(std::move(s)); }

    int nodeIndex(const cgltf_node* n) const { return indexOf(g_->nodes, n); }

    bool validNode(int n) const { return n >= 0 && n < static_cast<int>(m_.nodes.size()); }

    const cgltf_extension* findExt(const char* name) const {
        for (cgltf_size i = 0; i < g_->data_extensions_count; ++i)
            if (g_->data_extensions[i].name && std::string(g_->data_extensions[i].name) == name)
                return &g_->data_extensions[i];
        return nullptr;
    }

    static bool parseExt(const cgltf_extension* e, json::Value& out, std::string* error) {
        std::string err;
        if (!e->data || !json::parse(e->data, out, &err)) {
            if (error) *error = std::string("拡張 ") + (e->name ? e->name : "?") + " の解析に失敗: " + err;
            return false;
        }
        return true;
    }

    // ---- glTF 本体 ----

    bool readNodes(std::string* error) {
        m_.nodes.resize(g_->nodes_count);
        for (cgltf_size i = 0; i < g_->nodes_count; ++i) {
            const cgltf_node& n = g_->nodes[i];
            Node& o = m_.nodes[i];
            o.name = n.name ? n.name : "";
            o.parent = nodeIndex(n.parent);
            for (cgltf_size c = 0; c < n.children_count; ++c) o.children.push_back(nodeIndex(n.children[c]));
            if (n.has_matrix) {
                o.rest = decompose(n.matrix);
            } else {
                if (n.has_translation) o.rest.t = {n.translation[0], n.translation[1], n.translation[2]};
                if (n.has_rotation) o.rest.r = normalize(Quat{n.rotation[0], n.rotation[1], n.rotation[2], n.rotation[3]});
                if (n.has_scale) o.rest.s = {n.scale[0], n.scale[1], n.scale[2]};
            }
            o.mesh = indexOf(g_->meshes, n.mesh);
            o.skin = indexOf(g_->skins, n.skin);
        }
        // 親子の循環チェック(cgltf_validate でも見るが念のため)
        for (size_t i = 0; i < m_.nodes.size(); ++i) {
            int p = m_.nodes[i].parent;
            size_t steps = 0;
            while (p >= 0) {
                if (++steps > m_.nodes.size()) {
                    if (error) *error = "ノード階層が循環しています";
                    return false;
                }
                p = m_.nodes[p].parent;
            }
        }
        const cgltf_scene* scene = g_->scene ? g_->scene : (g_->scenes_count ? &g_->scenes[0] : nullptr);
        if (scene) {
            for (cgltf_size i = 0; i < scene->nodes_count; ++i) m_.sceneRoots.push_back(nodeIndex(scene->nodes[i]));
        } else {
            for (size_t i = 0; i < m_.nodes.size(); ++i)
                if (m_.nodes[i].parent < 0) m_.sceneRoots.push_back(static_cast<int>(i));
        }
        return true;
    }

    static std::vector<float> unpack(const cgltf_accessor* a, int comps) {
        std::vector<float> v;
        if (!a) return v;
        if (static_cast<int>(cgltf_num_components(a->type)) != comps) return v;
        v.resize(a->count * comps);
        if (cgltf_accessor_unpack_floats(a, v.data(), v.size()) != v.size()) v.clear();
        return v;
    }

    static std::vector<Vec3> toVec3(const std::vector<float>& f) {
        std::vector<Vec3> r(f.size() / 3);
        for (size_t i = 0; i < r.size(); ++i) r[i] = {f[i * 3], f[i * 3 + 1], f[i * 3 + 2]};
        return r;
    }

    bool readPrimitive(const cgltf_primitive& p, const cgltf_mesh& mesh, Primitive& o, std::string* error) {
        o.material = indexOf(g_->materials, p.material);
        const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr, *col = nullptr, *jnt = nullptr, *wgt = nullptr;
        for (cgltf_size i = 0; i < p.attributes_count; ++i) {
            const cgltf_attribute& a = p.attributes[i];
            switch (a.type) {
                case cgltf_attribute_type_position: pos = a.data; break;
                case cgltf_attribute_type_normal: nrm = a.data; break;
                case cgltf_attribute_type_texcoord: if (a.index == 0) uv = a.data; break;
                case cgltf_attribute_type_color: if (a.index == 0) col = a.data; break;
                case cgltf_attribute_type_joints: if (a.index == 0) jnt = a.data; break;
                case cgltf_attribute_type_weights: if (a.index == 0) wgt = a.data; break;
                default: break;
            }
        }
        if (!pos) {
            if (error) *error = "POSITION の無いプリミティブがあります";
            return false;
        }
        const size_t vc = pos->count;
        auto sizeOk = [&](const cgltf_accessor* a) { return !a || a->count == vc; };
        if (!sizeOk(nrm) || !sizeOk(uv) || !sizeOk(col) || !sizeOk(jnt) || !sizeOk(wgt)) {
            if (error) *error = "頂点属性の数が一致しません";
            return false;
        }
        o.positions = toVec3(unpack(pos, 3));
        if (o.positions.size() != vc) {
            if (error) *error = "POSITION を読めません";
            return false;
        }
        if (nrm) o.normals = toVec3(unpack(nrm, 3));
        if (uv) {
            auto f = unpack(uv, 2);
            o.uv0.resize(f.size() / 2);
            for (size_t i = 0; i < o.uv0.size(); ++i) o.uv0[i] = {f[i * 2], f[i * 2 + 1]};
        }
        if (col) {
            int nc = static_cast<int>(cgltf_num_components(col->type));
            auto f = unpack(col, nc);
            if (nc == 3 || nc == 4) {
                o.colors.resize(vc);
                for (size_t i = 0; i < vc && (i + 1) * nc <= f.size(); ++i)
                    o.colors[i] = {f[i * nc], f[i * nc + 1], f[i * nc + 2], nc == 4 ? f[i * nc + 3] : 1.0f};
            }
        }
        if (jnt && wgt) {
            if (cgltf_num_components(jnt->type) != 4) {
                if (error) *error = "JOINTS_0 が VEC4 ではありません";
                return false;
            }
            o.joints.resize(vc);
            for (size_t i = 0; i < vc; ++i) {
                cgltf_uint j[4] = {0, 0, 0, 0};
                cgltf_accessor_read_uint(jnt, i, j, 4);
                for (int k = 0; k < 4; ++k) o.joints[i][k] = static_cast<uint16_t>(std::min<cgltf_uint>(j[k], 0xFFFF));
            }
            auto f = unpack(wgt, 4);
            if (f.size() != vc * 4) {
                if (error) *error = "WEIGHTS_0 を読めません";
                return false;
            }
            o.weights.resize(vc);
            for (size_t i = 0; i < vc; ++i) {
                Vec4 w{f[i * 4], f[i * 4 + 1], f[i * 4 + 2], f[i * 4 + 3]};
                float s = w.x + w.y + w.z + w.w;
                if (s > 1e-6f) w = {w.x / s, w.y / s, w.z / s, w.w / s};
                else w = {1, 0, 0, 0};
                o.weights[i] = w;
            }
        }
        if (p.indices) {
            o.indices.resize(p.indices->count);
            for (size_t i = 0; i < o.indices.size(); ++i) {
                size_t v = cgltf_accessor_read_index(p.indices, i);
                if (v >= vc) {
                    if (error) *error = "インデックスが頂点数を超えています";
                    return false;
                }
                o.indices[i] = static_cast<uint32_t>(v);
            }
        } else {
            o.indices.resize(vc);
            for (size_t i = 0; i < vc; ++i) o.indices[i] = static_cast<uint32_t>(i);
        }
        o.indices.resize(o.indices.size() / 3 * 3);

        for (cgltf_size t = 0; t < p.targets_count; ++t) {
            MorphTarget mt;
            if (t < mesh.target_names_count && mesh.target_names[t]) mt.name = mesh.target_names[t];
            const cgltf_morph_target& tg = p.targets[t];
            for (cgltf_size i = 0; i < tg.attributes_count; ++i) {
                const cgltf_attribute& a = tg.attributes[i];
                if (a.data && a.data->count != vc) continue;
                if (a.type == cgltf_attribute_type_position) mt.dPosition = toVec3(unpack(a.data, 3));
                else if (a.type == cgltf_attribute_type_normal) mt.dNormal = toVec3(unpack(a.data, 3));
            }
            o.morphs.push_back(std::move(mt));
        }

        if (o.normals.size() != vc) computeNormals(o);
        return true;
    }

    static void computeNormals(Primitive& p) {
        p.normals.assign(p.positions.size(), Vec3{0, 0, 0});
        for (size_t i = 0; i + 2 < p.indices.size(); i += 3) {
            uint32_t a = p.indices[i], b = p.indices[i + 1], c = p.indices[i + 2];
            Vec3 n = cross(p.positions[b] - p.positions[a], p.positions[c] - p.positions[a]);
            p.normals[a] += n; p.normals[b] += n; p.normals[c] += n;
        }
        for (auto& n : p.normals) n = normalize(n, {0, 1, 0});
    }

    bool readMeshes(std::string* error) {
        m_.meshes.resize(g_->meshes_count);
        for (cgltf_size i = 0; i < g_->meshes_count; ++i) {
            const cgltf_mesh& mesh = g_->meshes[i];
            Mesh& o = m_.meshes[i];
            o.name = mesh.name ? mesh.name : "";
            for (cgltf_size w = 0; w < mesh.weights_count; ++w) o.defaultWeights.push_back(mesh.weights[w]);
            for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
                const cgltf_primitive& prim = mesh.primitives[p];
                if (prim.type != cgltf_primitive_type_triangles) {
                    warn("三角形リスト以外のプリミティブを無視しました(mesh " + std::to_string(i) + ")");
                    continue;
                }
                Primitive po;
                if (!readPrimitive(prim, mesh, po, error)) return false;
                o.primitives.push_back(std::move(po));
            }
        }
        return true;
    }

    bool readSkins(std::string* error) {
        m_.skins.resize(g_->skins_count);
        for (cgltf_size i = 0; i < g_->skins_count; ++i) {
            const cgltf_skin& s = g_->skins[i];
            Skin& o = m_.skins[i];
            for (cgltf_size j = 0; j < s.joints_count; ++j) o.joints.push_back(nodeIndex(s.joints[j]));
            o.inverseBind.assign(o.joints.size(), Mat4::identity());
            if (s.inverse_bind_matrices) {
                auto f = unpack(s.inverse_bind_matrices, 16);
                if (f.size() < o.joints.size() * 16) {
                    if (error) *error = "inverseBindMatrices が不足しています";
                    return false;
                }
                for (size_t j = 0; j < o.joints.size(); ++j) o.inverseBind[j] = Mat4::fromArray(&f[j * 16]);
            }
        }
        // スキンを使うプリミティブの JOINTS がスキンの範囲内か確認
        for (const Node& n : m_.nodes) {
            if (n.mesh < 0 || n.skin < 0) continue;
            size_t jc = m_.skins[n.skin].joints.size();
            for (const Primitive& p : m_.meshes[n.mesh].primitives)
                for (const auto& j : p.joints)
                    for (int k = 0; k < 4; ++k)
                        if (j[k] >= jc) {
                            if (error) *error = "JOINTS_0 がスキンのジョイント数を超えています";
                            return false;
                        }
        }
        return true;
    }

    void readImagesAndTextures() {
        m_.images.resize(g_->images_count);
        for (cgltf_size i = 0; i < g_->images_count; ++i) {
            const cgltf_image& im = g_->images[i];
            Image& o = m_.images[i];
            o.name = im.name ? im.name : "";
            o.mimeType = im.mime_type ? im.mime_type : "";
            if (im.buffer_view) {
                const uint8_t* p = cgltf_buffer_view_data(im.buffer_view);
                if (p) o.bytes.assign(p, p + im.buffer_view->size);
            } else {
                warn("bufferView を使わない画像は未対応です: " + o.name);
            }
            if (o.mimeType.empty() && o.bytes.size() >= 4) {
                if (o.bytes[0] == 0x89 && o.bytes[1] == 'P') o.mimeType = "image/png";
                else if (o.bytes[0] == 0xFF && o.bytes[1] == 0xD8) o.mimeType = "image/jpeg";
            }
        }
        m_.textures.resize(g_->textures_count);
        for (cgltf_size i = 0; i < g_->textures_count; ++i) {
            const cgltf_texture& t = g_->textures[i];
            Texture& o = m_.textures[i];
            o.image = indexOf(g_->images, t.image);
            if (t.sampler) {
                o.wrapS = t.sampler->wrap_s;
                o.wrapT = t.sampler->wrap_t;
                o.magFilter = t.sampler->mag_filter;
                o.minFilter = t.sampler->min_filter;
            }
        }
    }

    int texIndex(const cgltf_texture_view& v) const { return indexOf(g_->textures, v.texture); }

    void readMaterials() {
        m_.materials.resize(g_->materials_count);
        for (cgltf_size i = 0; i < g_->materials_count; ++i) {
            const cgltf_material& mt = g_->materials[i];
            Material& o = m_.materials[i];
            o.name = mt.name ? mt.name : "";
            if (mt.has_pbr_metallic_roughness) {
                const float* c = mt.pbr_metallic_roughness.base_color_factor;
                o.baseColorFactor = {c[0], c[1], c[2], c[3]};
                o.baseColorTexture = texIndex(mt.pbr_metallic_roughness.base_color_texture);
            }
            o.alphaMode = mt.alpha_mode == cgltf_alpha_mode_mask ? AlphaMode::Mask
                        : mt.alpha_mode == cgltf_alpha_mode_blend ? AlphaMode::Blend : AlphaMode::Opaque;
            o.alphaCutoff = mt.alpha_cutoff;
            o.doubleSided = mt.double_sided != 0;
            o.emissiveFactor = {mt.emissive_factor[0], mt.emissive_factor[1], mt.emissive_factor[2]};
            o.emissiveTexture = texIndex(mt.emissive_texture);
            o.normalTexture = texIndex(mt.normal_texture);
            o.unlit = mt.unlit != 0;
            for (cgltf_size e = 0; e < mt.extensions_count; ++e) {
                const cgltf_extension& ext = mt.extensions[e];
                if (!ext.name || std::string(ext.name) != "VRMC_materials_mtoon") continue;
                json::Value v;
                if (!parseExt(&ext, v, nullptr)) continue;
                o.isMToon = true;
                o.shadeColorFactor = vec3(v["shadeColorFactor"], {0, 0, 0});
                o.shadeMultiplyTexture = v["shadeMultiplyTexture"]["index"].asInt(-1);
                o.shadingShiftFactor = v["shadingShiftFactor"].asFloat(0);
                o.shadingToonyFactor = v["shadingToonyFactor"].asFloat(0.9f);
                if (v["outlineWidthMode"].asString("none") != "none") o.outlineWidth = v["outlineWidthFactor"].asFloat(0);
                o.outlineColor = vec3(v["outlineColorFactor"], {0, 0, 0});
            }
        }
    }

    static Vec3 vec3(const json::Value& v, Vec3 def) {
        if (v.isArray() && v.size() >= 3) return {v[0].asFloat(), v[1].asFloat(), v[2].asFloat()};
        if (v.isObject()) return {v["x"].asFloat(def.x), v["y"].asFloat(def.y), v["z"].asFloat(def.z)};
        return def;
    }

    static std::string joinStrings(const json::Value& v) {
        if (v.isString()) return v.asString();
        std::string r;
        for (const auto& e : v.items()) {
            if (!e.isString()) continue;
            if (!r.empty()) r += ", ";
            r += e.asString();
        }
        return r;
    }

    // ---- VRM 1.0 ----

    void readVrm1(const json::Value& v) {
        const json::Value& bones = v["humanoid"]["humanBones"];
        for (const auto& kv : bones.members()) {
            HumanBone b = humanBoneFromName(kv.first, false);
            int node = kv.second["node"].asInt(-1);
            if (b == HumanBone::None) { warn("未知のヒューマノイドボーン: " + kv.first); continue; }
            if (!validNode(node)) { warn("ヒューマノイドボーンのノード番号が不正: " + kv.first); continue; }
            m_.humanoid[idx(b)] = node;
        }

        const json::Value& meta = v["meta"];
        Meta& mt = m_.meta;
        mt.name = meta["name"].asString("");
        mt.version = meta["version"].asString("");
        mt.authors = joinStrings(meta["authors"]);
        mt.contactInformation = meta["contactInformation"].asString("");
        mt.references = joinStrings(meta["references"]);
        mt.thumbnailImage = meta["thumbnailImage"].asInt(-1);
        mt.licenseUrl = meta["licenseUrl"].asString("");
        mt.avatarPermission = meta["avatarPermission"].asString("onlyAuthor");
        mt.commercialUsage = meta["commercialUsage"].asString("personalNonProfit");
        mt.modification = meta["modification"].asString("prohibited");
        mt.creditNotation = meta["creditNotation"].asString("required");
        mt.allowExcessivelyViolentUsage = meta["allowExcessivelyViolentUsage"].asBool(false);
        mt.allowExcessivelySexualUsage = meta["allowExcessivelySexualUsage"].asBool(false);
        mt.allowPoliticalOrReligiousUsage = meta["allowPoliticalOrReligiousUsage"].asBool(false);
        mt.allowAntisocialOrHateUsage = meta["allowAntisocialOrHateUsage"].asBool(false);
        // 仕様の既定値は false
        mt.allowRedistribution = meta["allowRedistribution"].asBool(false) ? 1 : 0;
        mt.otherLicenseUrl = meta["otherLicenseUrl"].asString("");

        const json::Value& ex = v["expressions"];
        for (const auto& kv : ex["preset"].members()) readVrm1Expression(kv.first, kv.first, kv.second);
        for (const auto& kv : ex["custom"].members()) readVrm1Expression(kv.first, "", kv.second);

        for (const auto& a : v["firstPerson"]["meshAnnotations"].items()) {
            FirstPersonAnnotation fa;
            fa.node = a["node"].asInt(-1);
            if (!validNode(fa.node)) continue;
            fa.flag = parseFirstPersonFlag(a["type"].asString("auto"));
            m_.firstPerson.push_back(fa);
        }
    }

    static FirstPersonFlag parseFirstPersonFlag(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (s == "both") return FirstPersonFlag::Both;
        if (s == "thirdpersononly") return FirstPersonFlag::ThirdPersonOnly;
        if (s == "firstpersononly") return FirstPersonFlag::FirstPersonOnly;
        return FirstPersonFlag::Auto;
    }

    bool validMorph(int mesh, int morph) const {
        if (mesh < 0 || mesh >= static_cast<int>(m_.meshes.size()) || morph < 0) return false;
        for (const auto& p : m_.meshes[mesh].primitives)
            if (morph < static_cast<int>(p.morphs.size())) return true;
        return false;
    }

    void readVrm1Expression(const std::string& name, const std::string& preset, const json::Value& e) {
        Expression x;
        x.name = name;
        x.preset = preset;
        x.isBinary = e["isBinary"].asBool(false);
        for (const auto& b : e["morphTargetBinds"].items()) {
            int node = b["node"].asInt(-1);
            if (!validNode(node)) continue;
            MorphBind mb;
            mb.mesh = m_.nodes[node].mesh;
            mb.morphIndex = b["index"].asInt(-1);
            mb.weight = std::clamp(b["weight"].asFloat(1.0f), 0.0f, 1.0f);
            if (validMorph(mb.mesh, mb.morphIndex)) x.binds.push_back(mb);
        }
        m_.expressions.push_back(std::move(x));
    }

    void readVrm1Spring(const json::Value& s) {
        for (const auto& c : s["colliders"].items()) {
            Collider col;
            col.node = c["node"].asInt(-1);
            if (!validNode(col.node)) { col.node = -1; }
            const json::Value& shape = c["shape"];
            if (shape.has("capsule")) {
                const json::Value& cap = shape["capsule"];
                col.shape = Collider::Shape::Capsule;
                col.offset = vec3(cap["offset"], {0, 0, 0});
                col.radius = cap["radius"].asFloat(0);
                col.tail = vec3(cap["tail"], {0, 0, 0});
            } else {
                const json::Value& sp = shape["sphere"];
                col.offset = vec3(sp["offset"], {0, 0, 0});
                col.radius = sp["radius"].asFloat(0);
            }
            m_.colliders.push_back(col);  // 番号を保つため不正でも追加(node = -1 は無視される)
        }
        for (const auto& g : s["colliderGroups"].items()) {
            ColliderGroup cg;
            cg.name = g["name"].asString("");
            for (const auto& i : g["colliders"].items()) {
                int ci = i.asInt(-1);
                if (ci >= 0 && ci < static_cast<int>(m_.colliders.size())) cg.colliders.push_back(ci);
            }
            m_.colliderGroups.push_back(std::move(cg));
        }
        for (const auto& sp : s["springs"].items()) {
            Spring spring;
            spring.name = sp["name"].asString("");
            spring.center = sp["center"].asInt(-1);
            if (!validNode(spring.center)) spring.center = -1;
            for (const auto& j : sp["joints"].items()) {
                SpringJoint jt;
                jt.node = j["node"].asInt(-1);
                if (!validNode(jt.node)) continue;
                jt.hitRadius = j["hitRadius"].asFloat(0);
                jt.stiffness = j["stiffness"].asFloat(1.0f);
                jt.gravityPower = j["gravityPower"].asFloat(0);
                jt.gravityDir = normalize(vec3(j["gravityDir"], {0, -1, 0}), {0, -1, 0});
                jt.dragForce = std::clamp(j["dragForce"].asFloat(0.5f), 0.0f, 1.0f);
                spring.joints.push_back(jt);
            }
            for (const auto& i : sp["colliderGroups"].items()) {
                int gi = i.asInt(-1);
                if (gi >= 0 && gi < static_cast<int>(m_.colliderGroups.size())) spring.colliderGroups.push_back(gi);
            }
            if (spring.joints.size() >= 2) m_.springs.push_back(std::move(spring));
        }
    }

    // ---- VRM 0.x ----

    void readVrm0(const json::Value& v) {
        for (const auto& hb : v["humanoid"]["humanBones"].items()) {
            std::string name = hb["bone"].asString("");
            HumanBone b = humanBoneFromName(name, true);
            int node = hb["node"].asInt(-1);
            if (b == HumanBone::None) { warn("未知のヒューマノイドボーン: " + name); continue; }
            if (!validNode(node)) { warn("ヒューマノイドボーンのノード番号が不正: " + name); continue; }
            m_.humanoid[idx(b)] = node;
        }
        readVrm0Meta(v["meta"]);
        readVrm0BlendShapes(v["blendShapeMaster"]);
        readVrm0Spring(v["secondaryAnimation"]);
        readVrm0Materials(v["materialProperties"]);

        for (const auto& a : v["firstPerson"]["meshAnnotations"].items()) {
            int mesh = a["mesh"].asInt(-1);
            FirstPersonFlag f = parseFirstPersonFlag(a["firstPersonFlag"].asString("Auto"));
            for (size_t n = 0; n < m_.nodes.size(); ++n)
                if (m_.nodes[n].mesh == mesh && mesh >= 0) m_.firstPerson.push_back({static_cast<int>(n), f});
        }
    }

    void readVrm0Meta(const json::Value& meta) {
        Meta& mt = m_.meta;
        mt.name = meta["title"].asString("");
        mt.version = meta["version"].asString("");
        mt.authors = meta["author"].asString("");
        mt.contactInformation = meta["contactInformation"].asString("");
        mt.references = meta["reference"].asString("");
        int tex = meta["texture"].asInt(-1);
        if (tex >= 0 && tex < static_cast<int>(m_.textures.size())) mt.thumbnailImage = m_.textures[tex].image;

        std::string user = meta["allowedUserName"].asString("OnlyAuthor");
        mt.avatarPermission = user == "Everyone" ? "everyone"
                            : user == "ExplicitlyLicensedPerson" ? "onlySeparatelyLicensedPerson" : "onlyAuthor";
        // VRM 0.x の "Ussage" は仕様上の綴り
        mt.allowExcessivelyViolentUsage = meta["violentUssageName"].asString("") == "Allow";
        mt.allowExcessivelySexualUsage = meta["sexualUssageName"].asString("") == "Allow";
        mt.commercialUsage = meta["commercialUssageName"].asString("") == "Allow" ? "personalProfit" : "personalNonProfit";
        mt.otherLicenseUrl = meta["otherLicenseUrl"].asString("");
        mt.licenseUrl = meta["otherPermissionUrl"].asString("");

        std::string lic = meta["licenseName"].asString("");
        mt.vrm0LicenseName = lic;
        if (lic == "Redistribution_Prohibited") {
            mt.allowRedistribution = 0;
            mt.modification = "prohibited";
            mt.creditNotation = "required";
        } else if (lic == "CC0") {
            mt.allowRedistribution = 1;
            mt.modification = "allowModificationRedistribution";
            mt.creditNotation = "unnecessary";
        } else if (lic.rfind("CC_BY", 0) == 0) {
            mt.allowRedistribution = 1;
            mt.modification = lic.find("ND") != std::string::npos ? "prohibited" : "allowModificationRedistribution";
            mt.creditNotation = "required";
            if (lic.find("NC") != std::string::npos) mt.commercialUsage = "personalNonProfit";
        } else {
            mt.allowRedistribution = -1;  // "Other" 等: otherLicenseUrl を人が読む必要がある
            mt.modification = "prohibited";
            mt.creditNotation = "required";
        }
    }

    static std::string vrm0PresetToVrm1(std::string p) {
        std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        static const struct { const char* a; const char* b; } kMap[] = {
            {"a", "aa"}, {"i", "ih"}, {"u", "ou"}, {"e", "ee"}, {"o", "oh"},
            {"joy", "happy"}, {"angry", "angry"}, {"sorrow", "sad"}, {"fun", "relaxed"},
            {"blink", "blink"}, {"blink_l", "blinkLeft"}, {"blink_r", "blinkRight"},
            {"lookup", "lookUp"}, {"lookdown", "lookDown"}, {"lookleft", "lookLeft"}, {"lookright", "lookRight"},
            {"neutral", "neutral"}, {"surprised", "surprised"},
        };
        for (const auto& e : kMap)
            if (p == e.a) return e.b;
        return "";
    }

    void readVrm0BlendShapes(const json::Value& bsm) {
        for (const auto& g : bsm["blendShapeGroups"].items()) {
            Expression x;
            x.name = g["name"].asString("");
            x.preset = vrm0PresetToVrm1(g["presetName"].asString(""));
            x.isBinary = g["isBinary"].asBool(false);
            for (const auto& b : g["binds"].items()) {
                MorphBind mb;
                mb.mesh = b["mesh"].asInt(-1);
                mb.morphIndex = b["index"].asInt(-1);
                mb.weight = std::clamp(b["weight"].asFloat(100.0f) / 100.0f, 0.0f, 1.0f);  // 0.x は 0..100
                if (validMorph(mb.mesh, mb.morphIndex)) x.binds.push_back(mb);
            }
            m_.expressions.push_back(std::move(x));
        }
    }

    Vec3 unityToGltf(Vec3 v) const {
        if (opt_.vrm0SpringVectorsInUnitySpace) v.z = -v.z;
        return v;
    }

    void readVrm0Spring(const json::Value& sa) {
        // colliderGroups: 0.x ではグループごとにノードを 1 つ持つ
        for (const auto& g : sa["colliderGroups"].items()) {
            ColliderGroup cg;
            int node = g["node"].asInt(-1);
            for (const auto& c : g["colliders"].items()) {
                Collider col;
                col.node = validNode(node) ? node : -1;
                col.offset = unityToGltf(vec3(c["offset"], {0, 0, 0}));
                col.radius = c["radius"].asFloat(0);
                cg.colliders.push_back(static_cast<int>(m_.colliders.size()));
                m_.colliders.push_back(col);
            }
            m_.colliderGroups.push_back(std::move(cg));
        }
        for (const auto& bg : sa["boneGroups"].items()) {
            SpringJoint params;
            params.stiffness = bg["stiffiness"].asFloat(1.0f);  // 0.x 仕様の綴り
            params.gravityPower = bg["gravityPower"].asFloat(0);
            params.gravityDir = normalize(unityToGltf(vec3(bg["gravityDir"], {0, -1, 0})), {0, -1, 0});
            params.dragForce = std::clamp(bg["dragForce"].asFloat(0.4f), 0.0f, 1.0f);
            params.hitRadius = bg["hitRadius"].asFloat(0.02f);
            int center = bg["center"].asInt(-1);
            std::vector<int> groups;
            for (const auto& i : bg["colliderGroups"].items()) {
                int gi = i.asInt(-1);
                if (gi >= 0 && gi < static_cast<int>(m_.colliderGroups.size())) groups.push_back(gi);
            }
            std::string name = bg["comment"].asString("");
            std::set<int> visited;
            for (const auto& b : bg["bones"].items()) {
                int root = b.asInt(-1);
                if (!validNode(root)) continue;
                buildVrm0Chains(root, params, groups, validNode(center) ? center : -1, name, visited);
            }
        }
    }

    // 0.x の 1 ルートの下の全ノードを、分岐ごとに直列の鎖へ分解する。
    // 各ノードの尾は最初の子。2 番目以降の子は、その子から始まる新しい鎖になる。
    void buildVrm0Chains(int root, const SpringJoint& params, const std::vector<int>& groups, int center,
                         const std::string& name, std::set<int>& visited) {
        std::vector<int> starts{root};
        while (!starts.empty()) {
            int start = starts.back();
            starts.pop_back();
            if (visited.count(start)) continue;
            Spring s;
            s.name = name;
            s.center = center;
            s.colliderGroups = groups;
            s.virtualTailLength = opt_.vrm0VirtualTailLength;
            int n = start;
            while (n >= 0 && !visited.count(n)) {
                visited.insert(n);
                SpringJoint j = params;
                j.node = n;
                s.joints.push_back(j);
                const auto& ch = m_.nodes[n].children;
                for (size_t c = ch.size(); c-- > 1;) starts.push_back(ch[c]);
                n = ch.empty() ? -1 : ch[0];
            }
            if (!s.joints.empty()) m_.springs.push_back(std::move(s));
        }
    }

    void readVrm0Materials(const json::Value& props) {
        for (size_t i = 0; i < props.size() && i < m_.materials.size(); ++i) {
            const json::Value& p = props[i];
            if (p["shader"].asString("") != "VRM/MToon") continue;
            Material& o = m_.materials[i];
            o.isMToon = true;
            const json::Value& vp = p["vectorProperties"];
            const json::Value& fp = p["floatProperties"];
            const json::Value& tp = p["textureProperties"];
            o.shadeColorFactor = vec3(vp["_ShadeColor"], {0, 0, 0});
            o.shadeMultiplyTexture = tp["_ShadeTexture"].asInt(-1);
            o.shadingToonyFactor = fp["_ShadeToony"].asFloat(0.9f);
            o.shadingShiftFactor = fp["_ShadeShift"].asFloat(0);
            // 0.x の _OutlineWidth はセンチメートル相当
            o.outlineWidth = fp["_OutlineWidthMode"].asFloat(0) != 0 ? fp["_OutlineWidth"].asFloat(0) * 0.01f : 0;
            o.outlineColor = vec3(vp["_OutlineColor"], {0, 0, 0});
            if (o.baseColorTexture < 0) {
                int t = tp["_MainTex"].asInt(-1);
                if (t >= 0 && t < static_cast<int>(m_.textures.size())) o.baseColorTexture = t;
            }
        }
    }
};

}  // namespace

bool loadVrmFromMemory(const void* data, size_t size, Model& out, std::string* error, const LoadOptions& options) {
    out = Model();
    cgltf_options opt{};
    cgltf_data* g = nullptr;
    cgltf_result r = cgltf_parse(&opt, data, size, &g);
    if (r != cgltf_result_success) {
        if (error) *error = "glTF の解析に失敗しました (cgltf " + std::to_string(static_cast<int>(r)) + ")";
        return false;
    }
    struct Guard { cgltf_data* g; ~Guard() { cgltf_free(g); } } guard{g};

    // 外部ファイル参照は読まない(GLB 内のバッファと data URI のみ)
    for (cgltf_size i = 0; i < g->buffers_count; ++i) {
        const char* uri = g->buffers[i].uri;
        if (uri && std::string(uri).rfind("data:", 0) != 0) {
            if (error) *error = "外部バッファ参照を含むファイルは未対応です";
            return false;
        }
    }
    r = cgltf_load_buffers(&opt, g, nullptr);
    if (r != cgltf_result_success) {
        if (error) *error = "バッファの読み込みに失敗しました (cgltf " + std::to_string(static_cast<int>(r)) + ")";
        return false;
    }
    r = cgltf_validate(g);
    if (r != cgltf_result_success) {
        if (error) *error = "glTF の検証に失敗しました (cgltf " + std::to_string(static_cast<int>(r)) + ")";
        return false;
    }
    Loader loader(g, out, options);
    return loader.run(error);
}

bool loadVrmFromFile(const std::string& path, Model& out, std::string* error, const LoadOptions& options) {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) {
        if (error) *error = "ファイルを開けません: " + path;
        return false;
    }
    std::vector<char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return loadVrmFromMemory(buf.data(), buf.size(), out, error, options);
}

}  // namespace rvrm
