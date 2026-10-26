// プレイヤーキャラクター 1 体に VRM を被せる。
//   1. ゲームの骨格(CharacterMesh)からヒューマノイド対応ボーンのレスト姿勢を取り、リターゲットを準備
//   2. ProceduralMeshComponent を CharacterMesh に付け、VRM のメッシュ・マテリアル・テクスチャを作る
//   3. 元のキャラクター(CharacterMesh とその子)を描画から外す(アニメーションは動かしたまま)
//   4. 毎フレーム、骨の姿勢を読んで core で変形し、メッシュを更新する
// どこかで失敗したら、その時点で元の表示に戻して何もしない(ゲームを壊さない)。
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <Unreal/FWeakObjectPtr.hpp>

#include "rvrm/avatar.h"
#include "settings.hpp"
#include "custom_materials.hpp"
#include "material_finder.hpp"
#include "ue_call.hpp"

namespace rvrm_mod {

class VrmCharacter {
public:
    // meshProperty: 骨格を持つスケルタルメッシュのプロパティ名
    //   プレイヤー: CharacterMeshComponent / ロッカーのプレビュー: PreviewMeshComponent
    // firstPerson: 一人称判定を行うか(プレビューでは行わない)
    VrmCharacter(UObject* owner, std::unique_ptr<rvrm::Avatar> avatar, const Settings& settings,
                 std::wstring meshProperty, bool firstPerson, const MaterialFinder* materials,
                 CustomMaterials* custom);
    ~VrmCharacter();

    // false を返したら破棄してよい(対象が消えた、または失敗した)
    bool tick(float deltaSeconds);

    UObject* pawn() const { return pawn_.Get(); }
    bool failed() const { return failed_; }

    // 元の表示に戻して、作ったコンポーネントを消す
    void shutdown();

private:
    enum class State { WaitingForMesh, Running, Failed };

    bool bind();
    bool readRestPose(rvrm::SourcePose& rest);
    bool readCurrentPose(rvrm::SourcePose& cur, rvrm::Mat4& componentToWorld);
    bool createMeshComponent();
    bool createSections();
    bool createMaterials();
    bool createSection(size_t i, bool firstPerson);
    void updateSections();
    void hideOriginal(bool hide);
    void logPreviewDiagnostics(UObject* capture, UObject* procMesh);
    bool detectFirstPerson();
    void updateBlink(float dt);
    void fail(const std::wstring& why);

    RC::Unreal::FWeakObjectPtr pawn_;
    RC::Unreal::FWeakObjectPtr mesh_;      // ゲームの CharacterMesh
    RC::Unreal::FWeakObjectPtr procMesh_;  // 作った ProceduralMeshComponent
    RC::Unreal::FWeakObjectPtr capture_;   // ロッカーのプレビュー用カメラ(あれば)
    std::unique_ptr<rvrm::Avatar> avatar_;
    Settings settings_;
    std::wstring meshProperty_;
    bool useFirstPerson_ = true;
    const MaterialFinder* materials_ = nullptr;
    CustomMaterials* custom_ = nullptr;
    State state_ = State::WaitingForMesh;
    bool failed_ = false;
    float waitTime_ = 0;

    // ヒューマノイドボーン → ゲームのボーン名
    std::vector<std::pair<int, FName>> boneNames_;
    rvrm::SourcePose restPose_{};
    bool poseLogged_ = false;
    bool boundsLogged_ = false;
    int captureReadds_ = 0;
    int captureLogCount_ = 0;
    float captureLogTimer_ = 0;
    // 描画から外した元のコンポーネントと、元の設定(元に戻すため)
    struct Hidden {
        RC::Unreal::FWeakObjectPtr object;
        bool mainPass = true, depthPass = true, castShadow = true;
    };
    std::vector<Hidden> hidden_;
    int originalTickOption_ = -1;
    int hideCheckCountdown_ = 0;

    // セクションごとの一人称用データ
    std::vector<rvrm::FirstPersonFlag> fpFlags_;
    std::vector<std::vector<int32_t>> fullIndices_, fpIndices_;
    bool firstPerson_ = false;

    // 毎フレームの変換用バッファ
    std::vector<FVectorD> vtx_, nrm_;
    std::vector<std::vector<FVector2DD>> uvs_;

    // 自動まばたき
    float blinkTimer_ = 3.0f, blinkPhase_ = -1.0f;
};

}  // namespace rvrm_mod
