# 設計

方式C: **VRM4U を使わず、MOD 内で VRM を読み、ProceduralMesh + CPU スキニングで表示する。**
(方式の比較は CLAUDE.md §2)

## 全体構成

```
           ┌────────────── core(rvrm, エンジン非依存 C++17) ──────────────┐
VRM(GLB) → │ loader → convert → Skeleton → Retargeter → SpringBoneSim → Skinner │ → 頂点/法線
           └──────────────────────────────────────────────────────────────┘
                 ↑ ソース骨格の姿勢(コンポーネント空間)           ↓
           ┌──────────── mod(UE4SS C++、Phase 2)────────────┐
           │ ゲームのスケルタルメッシュから姿勢を取得 / ProceduralMesh 更新 │
           └─────────────────────────────────────────────┘
```

MOD 側は `rvrm::Avatar`(`core/include/rvrm/avatar.h`)だけを使う想定:

```cpp
Avatar a;
a.loadFromFile(path, options, &err);   // 解析 + UE 空間へ変換
a.bindSource(sourceRestPose, &err);    // ソース骨格のレスト姿勢でリターゲット準備
// 毎フレーム
a.update(sourceCurrentPose, componentToWorld, dt);
for (i : a.sections()) UpdateMeshSection(i, a.skinned()[i].positions, normals...);
```

## core のモジュール

| ファイル | 役割 |
|---|---|
| `math.h` | Vec3 / Quat / Mat3 / Mat4。列ベクトル・列優先。`a*b` は b を先に適用 |
| `json.h` | VRM 拡張部分の JSON を読む最小パーサ(深さ制限・不正入力で false) |
| `loader.h` | cgltf で glTF を読み、VRM 0.x / 1.0 の拡張を 1.0 寄りの `Model` に正規化 |
| `convert.h` | glTF 空間 → ソースのメッシュ空間(UE)への変換 |
| `skeleton.h` | ノード階層、トポロジカル順、global 計算、部分木の更新 |
| `retarget.h` | ソース(MetaHuman)→ VRM ヒューマノイドのリターゲット |
| `springbone.h` | VRM SpringBone |
| `skinning.h` | CPU スキニング、モーフ、表情 → モーフウェイト |
| `license.h` | meta から「他プレイヤーへ送ってよいか」を判定 |
| `avatar.h` | 上記をまとめた 1 体分のアバター |

### 読み込み(loader)

- 不正なファイルを受け取る前提(Phase 3 で他人のファイルを読む)。`cgltf_validate` に加え、
  インデックス範囲・JOINTS の範囲・属性数の一致・ノード階層の循環を検査し、失敗は `false` で返す。
- 外部ファイル参照(`uri` が data: 以外)のバッファは読まない。
- VRM 0.x の正規化:
  - 親指 `Proximal/Intermediate/Distal` → 1.0 の `Metacarpal/Proximal/Distal`
  - blendShape の weight(0..100)→ 0..1、プリセット名(`a`→`aa`, `joy`→`happy` など)
  - meta: `allowedUserName` → `avatarPermission`、`licenseName` → 再配布可否(`Redistribution_Prohibited`=不可、
    `CC0`/`CC_BY*`=可、`Other`=不明扱い)、`commercialUssageName: Allow` → `personalProfit`(移行ガイドの対応。要再確認)
  - secondaryAnimation: 1 つのルート以下の木を「最初の子を尾とする直列の鎖」に分解。2 番目以降の子は新しい鎖。
    末端は仮想の尾(7cm)。offset / gravityDir は Unity 座標とみなし Z 反転(切替可能)。

### 座標変換(convert)

- 基本: `UE = (z, -x, y) * 100`(行列式 -1)。VRM 0.x は先に Y 軸 180°(-Z 正面 → +Z 正面)。
- その後 `sourceMeshRotation`(既定 Z 軸 +90°)を掛け、**ソースのスケルタルメッシュのコンポーネント空間**
  (+Y 前・-X 右・+Z 上・cm)に合わせる。VRM のメッシュはこの空間で頂点を出すので、ProceduralMesh を
  ソースのメッシュコンポーネントに相対変換なしで付ければ重なる。
- 変換 `M`(直交、行列式 -1)に対し: 位置 `s·M·p`、方向 `M·n`、回転 `M R Mᵀ`(正しい回転になる)、
  inverseBind `C·IB·C⁻¹`。スケールは符号付き置換行列を前提に `|M·s|`(90° 単位の回転のみ想定)。
- 掌性が反転するので三角形の巻き順を反転(`flipWinding`、UE 側の表裏は実機で要確認)。

### リターゲット

すべて同じ空間(ソースのコンポーネント空間)でのグローバル回転で計算する:

```
Target(h) = SrcCur(h) · SrcRest(h)⁻¹ · Align(h) · DstRest(h)
Align(h)  = ターゲットのレスト骨方向(h → 向きを決める子)をソースのレスト骨方向へ向ける最小回転
```

- `SrcCur·SrcRest⁻¹` はソース骨の「レストからのグローバルな差分回転」。骨のロール(ローカル軸の取り方)の
  違いが打ち消されるので、MetaHuman と VRM で軸の取り方が違っても問題にならない。
- ソースは A ポーズ、VRM は T ポーズなので、`Align` で腕などの向きをそろえる。
  向きを決める子が無いボーン(頭・指先・つま先)は親の `Align` を引き継ぐ。腰は補正しない。
- ローカル回転は `Parent⁻¹ · Target` で求め、骨の長さ(ローカル平行移動)はターゲットのまま。
- 腰の平行移動は `(SrcCur.pos − SrcRest.pos) × (VRM の腰の高さ / ソースの腰の高さ)`。
- ヒューマノイド以外のノード(髪・スカートなど)はレストのローカルのまま親に追従し、揺れものが上書きする。
- ソースのボーン名の既定表は `defaultMetaHumanBoneNames()`(pelvis, spine_01/03/05, neck_01, head,
  clavicle/upperarm/lowerarm/hand, thigh/calf/foot/ball, 指 *_01..03)。目・顎は対応なし。

### SpringBone

VRMC_springBone 1.0 の説明どおり、尾の位置をワールド空間で積分する:

```
next = cur + (cur − prev)(1 − drag) + parentRot·initLocalRot·boneAxis · stiffness·dt + gravityDir · gravityPower·dt
next = head + normalize(next − head) · boneLength
コライダー(球/カプセル)に入っていれば外へ押し出し、再度ボーン長へ
localRot = initLocalRot · fromTo(boneAxis, (parentRot·initLocalRot)⁻¹ · (next − head))
```

- 長さの項はメートル基準なので `unitsPerMeter`(100)とコンポーネントのスケールを掛ける。
- `center` 指定時は尾の状態を center ノードの空間で保持。
- dt は上限(0.1 秒)で切り、1/50 秒ごとに最大 4 分割。テレポート時は `Avatar::resetDynamics`。
- 押し出し後にボーン長へ戻すため、強い重力ではわずかにめり込む(仕様のアルゴリズムどおり)。

### スキニング

- `v' = Σ wᵢ (JointGlobalᵢ · InverseBindᵢ)(v + Σ morphₖ·Δₖ)`。行列をブレンドしてから 1 回変換。
- スキンの無いメッシュノードはそのノードに剛体として追従。
- 描画単位は「メッシュノード × プリミティブ」= UE の 1 メッシュセクション。

## MOD 層(Phase 2 の計画)

- UE4SS C++ MOD。毎フレーム(ゲームスレッド)で:
  1. プレイヤーキャラクター(`BP_Men_PlayerCharacter` / `BP_Men_ReplicaPlayerCharacter`)の三人称メッシュを探す
  2. `GetSocketTransform(bone, RTS_Component)` でヒューマノイド対応ボーンの姿勢を取得 → `SourcePose`
     (初回はレスト姿勢: `GetRefPoseTransform` 等の有無を要確認。無ければ参照姿勢を別手段で取得)
  3. `Avatar::update` → `UpdateMeshSection_LinearColor`
  4. 元のメッシュを非表示
- UFunction の引数は FProperty のオフセットを実行時に引いて組み立てる(UE5 の FVector は double)。
- テクスチャは `ImportBufferAsTexture2D`、マテリアルは流用できるものを調査(CLAUDE.md §2-7)。
- 一人称: `firstPerson` 注釈(`thirdPersonOnly` のメッシュを隠す)と頭ボーン配下の頂点の扱いを検討。

## 負荷の見込み(Phase 4 で対策)

VRoid 標準モデルで 12〜17 万頂点、表情モーフ 400 前後。

計測値(`vrm_inspect --bench`、Release、シングルスレッド、リターゲット + SpringBone + スキニング、表情なし):

| モデル | 頂点数 | 1 フレーム |
|---|---|---|
| VRoid 製モデル(複数) | 12〜17 万 | 1.2〜1.9 ms |
| AliciaSolid | 5.3 万 | 0.5 ms |
| 2 万頂点前後のモデル | 2 万 | 0.2 ms |

これに UE 側の `UpdateMeshSection`(float → double 変換・コピー・GPU 転送)が加わる。6 人とも VRoid 級だと
core だけで 10 ms 前後になり得るので、次を検討する:
- 他プレイヤー分をワーカースレッドで並列に計算(core はスレッド間で状態を共有しない設計)
- 読み込み時に未使用モーフの除去、頂点の重複除去
- 遠い・画面外のプレイヤーは更新を間引く
- 将来: ボーン行列をテクスチャに書き、マテリアルで GPU スキニング

## 未確認事項

CLAUDE.md §5 を参照。core 側で実機確認が必要なもの:
- `flipWinding` の要否(UE の ProceduralMesh の表裏)
- `sourceMeshRotation` の既定値(ゲームのメッシュ空間の向き)
- VRM 0.x の secondaryAnimation ベクトルの座標系
