# 実機調査の結果

## 調査 1(2026-10-26、調査用 MOD `RamenVRM_Probe`)

マップ `PL_Men_JP_Playground`(シングルプレイ)で、ゲーム開始の約 15 秒後に取得。
生データ(ログ・オブジェクト一覧・C++ ヘッダー)は `research/probe_20261026/` にある(ゲーム由来のため git 管理外)。

### UE4SS
- 同梱版(experimental `v3.0.1-1183-gcdbc0b66`)で**正常に起動**。エンジンは `5.7` と自動検出、Shipping ビルド。
- Lua MOD・オブジェクト一覧・C++ ヘッダー生成(830 ファイル)すべて動作。

### プレイヤーキャラクター
- クラス: `BP_Men_PlayerCharacter_C` ← `AMenPlayerCharacter`(C++)← `AMoverExamplesCharacter`。
  `BP_Men_ReplicaPlayerCharacter_C` はシングルでは 0 個。`AMenPlayerCharacter` に `LocalPlayerAnimClass` /
  `RemotePlayerAnimClass` があるので、**他プレイヤーも同じクラスで、AnimBP だけ切り替えている**可能性が高い(マルチで要確認)。
- 構成(主要部分):
  ```
  BP_Men_PlayerCharacter_C
   ├ TP (SceneComponent)
   │  └ CharacterMesh (MenAnimationMeshComponent : SkeletalMeshComponent)  ← 骨格・アニメーションの本体
   │     ・AnimBP: ABP_Men_Player_TP_C、ボーン 1187 本(体 + MetaHuman の顔)
   │     ・相対位置 (0,0,-96)、相対回転 Yaw -90°
   │     └ MetaHumanVisualComponent (MenMetaHumanVisualComponent)
   │        ├ MetaHumanVisual_Head (MenSynchronizedFaceMeshComponent)  ← 頭(調査時は非表示 = 一人称で隠している?)
   │        ├ MetaHumanVisual_Hair / Eyebrows / Eyelashes / Mustache / Beard / Fuzz (GroomComponent)
   │        └ MetaHumanVisual_LODSync
   ├ FP (SceneComponent)、FirstPersonCamera、ShoulderCamera / EmoteCamera(+ Boom)
   └ CustomizeComponent (MenPlayerCustomizeComponent) など
  ```
- `AMenPlayerCharacter` の関連プロパティ: `CharacterMeshComponent`(0x04C8)、`MetaHumanVisualComponent`(0x04D0)、
  `bHideLocalCharacterMesh`(0x05DE)、`bShowLocalBodyWhileFirstPersonSeatedIdle`(0x0561)。
- `UMenMetaHumanVisualComponent` に `BodyMeshComponent` / `HeadMeshComponent` と各 Groom の取得関数がある。
  → **VRM 表示時に隠す対象**: 体(`BodyMeshComponent`、たぶん CharacterMesh)・頭・Groom 一式。
  隠すと骨の更新が止まらないか(`VisibilityBasedAnimTickOption`)に注意。

### 骨格(CharacterMesh)
- 名前: `Root` → `pelvis` → `spine_01..05` → `neck_01/02` → **`Head`(大文字)**。腕・脚・指は UE5 マネキン系
  (`clavicle_l`, `upperarm_l`, `lowerarm_l`, `hand_l`, `thigh_l`, `calf_l`, `foot_l`, `ball_l`, `*_metacarpal_l`, `*_01..03_l`)。
  顔は `FACIAL_*`(`FACIAL_L_Eye`, `FACIAL_R_Eye`, `FACIAL_C_Jaw` など)。
  core の既定対応表(`defaultMetaHumanBoneNames`)の 55 本はすべて実在することを確認。
- **メッシュ空間**: +Y 前、+X が左(-X が右)、+Z 上、足元が Z=0、cm。core の変換の既定値(`sourceMeshRotation` = Z 軸 +90°)と一致。
- **レスト姿勢**: A ポーズ(上腕が水平から約 53° 下)。腰の高さ 91.4cm、頭 155.6cm。
- `GetRefPoseTransform(i)` は**親ボーン空間**の値を返す。コンポーネント空間のレストは親をたどって合成する。
- `GetSocketTransform(name, RTS_Component)` で現在の姿勢がコンポーネント空間で取れる。

### 使う予定の関数
すべて存在(`GetNumBones`, `GetBoneName`, `GetParentBone`, `GetRefPoseTransform`, `GetSocketTransform`,
`AddComponentByClass`, `ProceduralMeshComponent` の `CreateMeshSection_LinearColor` / `UpdateMeshSection_LinearColor`,
`ImportBufferAsTexture2D`, `CreateDynamicMaterialInstance`, `SetVisibility`, `SetOwnerNoSee` ほか)。
無いのは `Actor:K2_DestroyComponent` のみ(代わりに `ActorComponent:K2_DestroyComponent` を使う)。

### オフライン検証
取得した「実際のレスト姿勢」と「プレイ中の姿勢 1 フレーム」(`research/probe_20261026/pose.txt`)を
`vrm_inspect --source-pose ... [--use-cur]` で VRM に適用し、画像化して破綻がないことを確認した。

### 未確認(次の実機確認で見る)
- 他プレイヤー(マルチ)のキャラクターの構成
- 一人称時に何が隠れるか、`bHideLocalCharacterMesh` の挙動
- ProceduralMesh の表裏(`flipWinding`)

## 調査 2(2026-10-27、v0.5.x の [診断] ログ)

### ロッカーのカスタマイズ画面のプレビュー
- `AMenCustomizePreviewStageActor` はワールドの (-1000000, -1000000, -1000000) 付近に置かれ、
  `SceneCaptureComponent`(USceneCaptureComponent2D)で撮影した RenderTarget を UI に出している。
- カメラ設定: PrimitiveRenderMode = 2(UseShowOnlyList)、ShowOnlyComponents に元のキャラの部品 9 個前後、
  bCaptureEveryFrame = 0 / bCaptureOnMovement = 0(ゲームが CapturePreview を呼んだときだけ撮影)。
  カメラはキャラの前方 320cm・上 100cm。
- プレビュー用キャラ(PreviewMeshComponent)のライティングチャンネルは bChannel1 のみ(値 2)。
- プレビュー用キャラの骨は通常どおり読める(腰 89cm・頭 152cm)。VRM の頂点も正しい範囲にある。
- VRM を ShowOnlyComponent で登録(9 → 10)し、チャンネルも合わせたが、v0.5.2 時点では写らない。
  残る候補: カスタム深度ステンシルでキャラを切り抜いている / EyeAdaptation がキャプチャで 0 になり発光が壊れる。
  → v0.5.3 で両方に対処(カスタム深度設定のコピー、マテリアルの 0 割り防止とプレビューでの自動露出打ち消しの無効化)。
