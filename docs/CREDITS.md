# 出典・ライセンス

## 同梱しているサードパーティ

| 名前 | 用途 | ライセンス | 場所 |
|---|---|---|---|
| cgltf v1.15(Johannes Kuhlmann) | glTF 2.0 / GLB の解析 | MIT | `third_party/cgltf/`(`LICENSE` 同梱) |
| UE4SS(RE-UE4SS, experimental-latest 2026-10-25) | MOD をゲームに読み込ませる土台(配布物に同梱) | MIT | `third_party/ue4ss/`(内包ライブラリのライセンスは `third_party/ue4ss/licenses/`) |

## 実装の根拠にした仕様

- glTF 2.0(Khronos Group): https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
  - ノード階層、スキニング(`JointGlobal * InverseBind`)、モーフターゲット、アクセサ
- VRM 仕様(VRM コンソーシアム): https://github.com/vrm-c/vrm-specification
  - VRM 1.0: `VRMC_vrm`(humanoid / meta / expressions / firstPerson)、`VRMC_springBone`、`VRMC_materials_mtoon`
  - VRM 0.x: `extensions.VRM`(humanoid / meta / blendShapeMaster / secondaryAnimation / firstPerson / materialProperties)
  - SpringBone のアルゴリズム(慣性・剛性・重力・ボーン長拘束・コライダー押し出し・回転の求め方)は
    VRMC_springBone 1.0 仕様の説明に従って自前で実装した(`core/src/springbone.cpp`)。
  - VRM 0.x → 1.0 の名前・値の読み替え(親指ボーン名、表情プリセット名、meta の各項目)は
    仕様書の移行に関する記述に基づく。

## 参考にしたもの(コードは使用していない)

- **VRM4U**(ruyo, MIT): https://github.com/ruyo/VRM4U
  - 「UE で VRM を表示する」という全体像の参考のみ。ソースコードのコピー・移植はしていない。
    本 MOD はエンジンプラグインを使わない方式(実行時に自前で解析・CPU スキニング)を取っている。
- **UniVRM**(VRM コンソーシアム, MIT): https://github.com/vrm-c/UniVRM
  - VRM 0.x の揺れものの挙動として広く知られている次の 2 点に合わせた(コードは使用していない):
    - 各ボーンの「尾」は最初の子ボーン、子が無い末端は親→自分の方向に 7cm の仮想の尾を付ける
    - `secondaryAnimation` のベクトル(コライダーの offset、gravityDir)は Unity 座標系で記録されている
      (glTF へは Z 反転)。この解釈は実機で要確認のため `LoadOptions::vrm0SpringVectorsInUnitySpace` で切り替え可能。

## 実行時に使うゲーム/エンジンの機能

- 表示用マテリアルには UE エンジン標準の `Widget3DPassThrough` 系(`/Engine/EngineMaterials/`)を使う
  (ゲームに含まれているものを実行時に参照するだけで、ファイルは配布しない)。
- MOD は UE4SS の C++ API(MIT)を使い、UE4SS の公開ヘッダーに対してビルドしている。

## 一般的なアルゴリズム

- 4x4 逆行列: 余因子展開による一般的な公式
- 回転行列 ↔ クォータニオン、最小回転(from-to)、slerp: 教科書的な公式
- リターゲット(グローバル回転の差分 + レスト姿勢の向き合わせ)は本プロジェクトで設計したもの(`docs/DESIGN.md`)
