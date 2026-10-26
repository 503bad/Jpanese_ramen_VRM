# ramen_vrm_mod — 引き継ぎメモ(CLAUDE.md)

Japanese Ramen Simulator(Steam AppID 4968640)のプレイヤーキャラクターを、
**各プレイヤーが自分で用意した VRM モデル**に置き換える MOD。マルチプレイ前提。

このファイルは、claude.ai 上の調査セッションから Claude Code への引き継ぎ資料です。
「確認済み」と「未確認」を区別して書いています。未確認事項は実装前に必ず検証してください。

---

## 0. ユーザーからの必須要件・制約(厳守)

1. **VRM4U のコードを写さない(クリーンルーム実装)。**
   - VRM4U(MIT, https://github.com/ruyo/VRM4U)は OSS だが、盗作と取られないよう**参考にとどめる**。
   - 実装の根拠は **VRM / glTF の公開仕様**(vrm-c/vrm-specification、Khronos glTF 2.0)に置く。
   - VRM4U のソースをコピー・逐語的に移植しない。アルゴリズムを参考にした箇所は `docs/CREDITS.md` に出典を明記する。
   - サードパーティのライブラリ(例: cgltf, MIT)を同梱する場合はライセンス表記を残す。
2. **ゲームのインストールフォルダを勝手に書き換えない。**
   - 調査は読み取りのみ。書き込みが必要な作業(UE4SS の導入など)はユーザーに確認してから。
   - 開発作業は `E:\develop\ramen_vrm_mod`(このフォルダ)で行う。
3. **最終成果物として `install.bat` と `uninstall.bat` を用意する。**
   - インストール先は**動的に検出**すること。パスの決め打ちは不可(ユーザーの明確な指示)。
   - 一般論として自動化が難しい部分があれば、その部分は**手順書(ドキュメント)**として用意する。
4. 公式(開発元)への問い合わせは不要(ユーザーに事情あり)。提案しないこと。
5. 返答・ドキュメントは日本語。

---

## 1. 確認済みの事実(2026-10-26 調査、ゲームファイルの読み取りのみ)

ユーザー環境のインストール先(参考。**コードには決め打ちしない**):
`C:\Program Files (x86)\Steam\steamapps\common\Japanese Ramen Simulator`

### エンジン・ビルド
- **Unreal Engine 5.7 系**。exe の FileVersion/ProductVersion は `++DMZ+ue573-upgrade-CL-0`、
  exe 内に UTF-16 文字列 `5.7.4` あり。開発元の別作品「DMZ」と共有の独自エンジンブランチ。
  (公式クレジットにも Unreal Engine と Wwise の使用が明記されている)
- 本体: `RealRamenSimulator\Binaries\Win64\RealRamenSimulator-Win64-Shipping.exe`(約 208MB)
- **monolithic ビルド**(エンジンが exe に静的リンク。エクスポート 760 個は主に Mutable)。
  → **エンジンプラグイン(VRM4U など)の DLL を後から読ませる方式は不可**と判断。
- 起動用の小さな `RealRamenSimulator.exe` がルートにある。

### pak
- `RealRamenSimulator\Content\Paks\RealRamenSimulator-Windows.{pak,utoc,ucas}`(ucas 約 4.3GB)+ `global.{utoc,ucas}`
- IoStore(utoc v8)。ContainerFlags = 9 → **圧縮・インデックスあり / 暗号化なし / 署名なし**。
  → FModel 等でのアセット抽出、MOD pak の追加が容易。
- utoc のディレクトリインデックスから約 11,000 件のパスを列挙済み(下記)。

### exe 内に存在が確認できたクラス/関数(文字列ベース。確度は高いが実行時に最終確認すること)
- `ProceduralMeshComponent`, `CreateMeshSection_LinearColor`, `UpdateMeshSection_LinearColor`
- `DynamicMeshComponent`, GeometryScripting(`GeometryScriptLibrary` 多数)
- `ImportBufferAsTexture2D`, `ImportFileAsTexture2D`(KismetRenderingLibrary)
- `PoseableMeshComponent`, `CopyPoseFromMesh`, `SetLeaderPoseComponent`, `SetSkeletalMeshAsset`
- Interchange(`InterchangeManager`, `ImportSceneAsync`, `GLTFTranslator`)
  → 実行時の glTF 読み込みが使える可能性はあるが、SkeletalMesh のビルドは通常エディタ専用なので期待薄。要検証。
- Mutable(CustomizableObject)ランタイムあり。
- オンライン: `OnlineSubsystemSteam`、`OnlineSubsystemEOS`、`NetDriverEOS`、`SteamSockets`、`P2PSockets`、`CreateLobby` の文字列あり。
  Steamworks v161(`Engine\Binaries\ThirdParty\Steamworks\Steamv161\Win64\steam_api64.dll`)、`EOSSDK-Win64-Shipping.dll` も同梱。
  **実際にどちらを使うかは DefaultEngine.ini(pak 内)次第で未確定。**

### キャラクター構造(アセットパスから判明)
- 骨格は **MetaHuman**(`MetaHumans/Common/.../metahuman_base_skel`)。UE5 マネキン系のボーン名
  (pelvis, spine_01..05, neck_01/02, head, clavicle_l, upperarm_l, lowerarm_l, hand_l, thigh_l, calf_l, foot_l, ball_l, 指 …)。
- アニメーションは GASP(Game Animation Sample)ベースのモーションマッチング(PoseSearch)。移動は **Mover プラグイン**。
- プレイヤー関連:
  - `/Game/Men/Blueprints/Player/BP_Men_PlayerCharacter`
  - `/Game/Men/Blueprints/Player/BP_Men_ReplicaPlayerCharacter`(他プレイヤー表示用の複製?要調査)
  - `/Game/Men/Blueprints/Player/BP_Men_PlayerController`
  - AnimBP: `/Game/Men/Animation/Mover/ABP_Men_Player_FP`(一人称)と `ABP_Men_Player_TP`(三人称)
    → **一人称用と三人称用でメッシュが別の可能性が高い**。一人称では頭を隠す等の対応が要る。
  - ポストプロセス AnimBP: `/Game/Men/Animation/ABP_Men_Body_PostProcess`, `ABP_Men_Face_PostProcess`
  - `/Game/Men/MasterData/Player/DA_Men_Player_MeshList`
- 既存のプレイヤーカスタマイズ機能あり(衣装・体型スライダー・色): `/Game/Men/Blueprints/UI/Customize/WBP_Men_PlayerCustomize*`, `BP_Men_PlayerCustomize_Locker`
- 公式ロードマップでは「アルバイト(NPC)のキャラクリエイト」が予定。プレイヤーの VRM 対応予定は見当たらない。

### MOD 事情
- 公式の MOD キット・ワークショップ・MOD 方針の明文はなし(公式サイトの規約類は配信ガイドラインのみ)。
- Nexus Mods に既存 MOD あり: 「Ramen Gameplay Adjustments - Time and Patience」(UE4SS + ModMenu 同梱、
  `RealRamenSimulator\Binaries\Win64\` に展開、マルチ動作報告あり)
  → **UE4SS による注入・フックはこのゲームで動作実績あり**。インストーラは既存 UE4SS と共存させること。

---

## 2. 採用方針: 方式C(VRM4U を使わない実行時読み込み)

比較検討の結論:
- 方式A(VRM を UE エディタで事前に pak 化)…確実だがユーザー負担が非常に大きく、共有も面倒 → 不採用
- 方式B(VRM4U を注入)…monolithic ビルドのため実質不可 → 不採用
- **方式C(MOD 内で VRM を自前で読み、ProceduralMesh + CPU スキニングで表示)→ 採用**

### パイプライン
1. **VRM 解析**: VRM は glTF(GLB)。cgltf 等で読み、以下を取得:
   メッシュ(位置/法線/UV/JOINTS_0/WEIGHTS_0/インデックス/モーフ)、skin(joints, inverseBindMatrices)、
   ノード階層と rest 姿勢、テクスチャ(PNG/JPEG)、マテリアル、
   VRM 0.x(`extensions.VRM`: humanoid, secondaryAnimation, blendShapeMaster, meta)/
   VRM 1.0(`VRMC_vrm`, `VRMC_springBone`, `VRMC_materials_mtoon`, meta)。
2. **座標変換**(glTF: 右手系 Y-up m / UE: 左手系 Z-up cm)
   - VRM1(+Z 正面)想定: `UE = (z, -x, y) * 100`(行列式 -1 で掌性反転を含む)
   - VRM0 は -Z 正面のことが多い → 先に Y 軸 180° 回転(x, z を反転)
   - 掌性反転に伴う三角形の巻き順は**設定で切り替え可能**にしておく(UE 側の表/裏は実機で要確認)
3. **リターゲット**(ゲームの MetaHuman 骨格 → VRM ヒューマノイド)
   - 各ヒューマノイドボーンについて、ソースの現在/レスト回転とターゲットのレスト回転から目標回転を計算:
     `Target(h) = SrcCur(h) * SrcRest(h)^-1 * Align(h) * DstRest(h)`
     `Align(h)` = ターゲットのレスト骨方向(ボーン→子)をソースのレスト骨方向へ向ける最小回転
     (ソースは A ポーズ、VRM は T ポーズなので、この補正がないと腕が上がったままになる)
   - hips の平行移動はソース pelvis の移動量を脚長比でスケール
   - ソース姿勢の取得: `GetSocketTransform(BoneName, RTS_Component)` 等の UFUNCTION を ProcessEvent で呼ぶ
     (ボーン 55 本 × 6 人 × 毎フレームでも許容範囲の見込み)
   - ソースのレスト姿勢: `GetRefPosePosition` / `GetRefPoseTransform` / `GetBoneIndex` 等(存在を要確認)
   - **ソースのメッシュ空間の向き**(UE キャラは通常メッシュが -90° Yaw、メッシュ空間で +Y 前方・-X 右・+Z 上)は**設定可能**にする
4. **CPU スキニング**: `v' = Σ w_i (JointGlobal_i * InverseBind_i) v`(ここに表情モーフも加算)→
   `UpdateMeshSection_LinearColor` で毎フレーム更新
5. **表示**: `AActor::AddComponentByClass`(UFUNCTION)で ProceduralMeshComponent を追加し、
   プレイヤーのメッシュに追従させる。元のメッシュは非表示にする(`SetVisibility` / `SetHiddenInGame` 等)。
   UE5 の FVector / FVector2D は double。**UFunction の引数レイアウトはハードコードせず、FProperty のオフセットを実行時に引いて組み立てる**こと。
6. **テクスチャ**: `ImportBufferAsTexture2D` で PNG/JPEG バイト列から UTexture2D を作成
7. **マテリアル**: テクスチャパラメータを持つベースマテリアルから DynamicMaterialInstance を作る。
   - 候補a: ゲーム/エンジン内にある適当なマテリアルを実行時に探す(パラメータ名をダンプして調査)
   - 候補b: トゥーン調の汎用マテリアルを UE5.7 エディタで 1 つだけ作り、MOD 作者が pak として同梱
     (ユーザーごとの変換作業は不要。署名なしなので pak 追加は可能)
8. **揺れもの**: VRM SpringBone を仕様に従い C++ で自前実装(Verlet、stiffness/gravity/drag/hitRadius、球/カプセルコライダー)。
   ワールド空間で計算し、コンポーネントの移動による慣性を反映。
9. **表情**: Phase 1 では解析のみ。後でまばたき等を自前で駆動。

### 負荷対策(後回しでよいが設計に織り込む)
- 読み込み時のポリゴン削減、遠距離プレイヤーの更新間引き
- 将来案: ボーン行列をテクスチャに書き、マテリアル(頂点シェーダー/WPO)で GPU スキニング

### マルチプレイ(Phase 3)
- 動きはゲーム本体が同期済み。各クライアントが「受け取ったキャラのポーズ → 自分の手元の VRM」に変換すれば、動きのための追加通信は不要。
- 必要なのは「SteamID → モデル」の対応と **VRM ファイルの転送**(数十 MB あり得る)。
  ゲーム本体の通信には乗せず、Steamworks の P2P などの別チャンネルで送る。ハッシュでキャッシュし、再送しない。
  ゲームが Steam ロビーを使っているかは未確定(上記)。
- **VRM の meta(利用許諾・再配布可否)を読み、許可されていないモデルは送信しない**仕様にする。
- MOD を入れていない人には元のキャラが見えるだけで、ゲームを壊さないこと。

---

## 3. 推奨リポジトリ構成(案)

```
ramen_vrm_mod/
  CLAUDE.md                 … この引き継ぎメモ
  README.md                 … 利用者向け概要
  docs/
    DESIGN.md               … 上記設計の詳細化
    BUILD.md                … ビルド手順(VS2022 / CMake / UE4SS C++ テンプレート)
    INSTALL.md              … 手動インストール手順(bat で自動化できない部分を含む)
    CREDITS.md              … 参考にした仕様・OSS とライセンス(VRM4U は「参考」として明記)
  core/                     … エンジン非依存の C++17 ライブラリ(VRM 解析・リターゲット・スキニング・SpringBone)
    include/ src/ tests/    … 単体テストは合成モデルで。ゲームなしで検証可能にする
  tools/vrm_inspect/        … CLI: ヒューマノイド対応・メッシュ数・meta(許諾)を表示、OBJ 書き出しで目視確認
  mod/                      … UE4SS C++ MOD(core をリンク。ゲームとの接点だけを持つ)
  scripts/
    install.bat
    uninstall.bat
  third_party/              … cgltf 等(ライセンスファイル込み)
```

---

## 4. install.bat / uninstall.bat の仕様

### インストール先の動的検出(決め打ち禁止)
1. Steam のパスをレジストリから取得:
   `HKCU\Software\Valve\Steam` の `SteamPath`(なければ `HKLM\SOFTWARE\WOW6432Node\Valve\Steam` の `InstallPath`)
2. `<SteamPath>\steamapps\libraryfolders.vdf` を解析して、全ライブラリフォルダを列挙
3. 各ライブラリで `steamapps\appmanifest_4968640.acf` を探し、`"installdir"` を読む
4. `<library>\steamapps\common\<installdir>\RealRamenSimulator\Binaries\Win64\RealRamenSimulator-Win64-Shipping.exe` の存在で確定
5. 見つからない/複数ある場合はユーザーに選ばせる(フォルダ選択ダイアログ or パス入力)。引数でパスを明示指定もできるようにする
   - VDF の解析は bat 単体では厳しいので、bat から PowerShell を呼ぶ形が現実的

### インストール処理
- 既存の UE4SS(Nexus の MOD に同梱されている場合あり)を検出し、**あれば上書きしない**
- UE4SS がなければ導入する(取得方法・バージョンはユーザーと相談。下記の未確認事項参照)
- MOD を UE4SS の Mods フォルダへ配置し有効化(UE4SS のバージョンにより `Win64\Mods` か `Win64\ue4ss\Mods`。`enabled.txt` 方式なら mods.txt を書き換えずに済む)
- 何をどこに置いたかを**インストールマニフェスト**として記録(uninstall で使う)
- 実行前に変更内容を表示して確認を取る

### アンインストール
- マニフェストに記録したファイルだけを削除。UE4SS は**自分で入れた場合のみ**削除
- ユーザーの VRM・設定・キャッシュは削除するか確認する

---

## 5. 未確認事項(実装前・実装中に検証)

- [x] **UE4SS が UE5.7(このゲームの独自ブランチ)で動くか**、どのバージョンを使うか → 同梱の experimental 版で動作確認済み(docs/RESEARCH.md)
      (Nexus の既存 MOD が UE4SS を同梱して動作している → 同梱版のバージョンを確認するのが早い)
- [ ] UE4SS C++ API の詳細(CppUserModBase、毎フレームのコールバックをゲームスレッドで受ける方法、ProcessEvent の呼び出し方、UE5.7 向けのヘッダー生成)
- [x] `BP_Men_PlayerCharacter` のコンポーネント構成 → docs/RESEARCH.md(Replica の役割とマルチ時の構成は未確認)
      → UE4SS のライブビューやダンプで調べる(ゲームの起動はユーザーが行う)
- [x] `GetRefPoseTransform` 等の有無 → あり(親ボーン空間の値を返す)
- [ ] ProceduralMesh の三角形の巻き順(表/裏)(メッシュ空間の向きは確認済み: +Y 前・+X 左・+Z 上)
- [ ] テクスチャパラメータを持つ流用可能なマテリアルの有無
- [ ] 通信が Steam か EOS か(pak 内の `RealRamenSimulator/Config/DefaultEngine.ini`。暗号化なしなので FModel 等で読める)
- [ ] ゲームのアップデートでクラス名・骨格が変わったときの検出と安全な無効化

---

## 6. 進め方の提案(フェーズ)

- **Phase 0**: リポジトリの雛形、docs、`install.bat` / `uninstall.bat`(検出ロジックだけ先に作って、ゲームに書き込まない dry-run で検証)
- **Phase 1**: core ライブラリ(VRM 解析・座標変換・CPU スキニング・リターゲット・SpringBone)と単体テスト、vrm_inspect
- **Phase 2**: UE4SS C++ MOD でシングルプレイ表示(自分のキャラのみ、三人称 → 一人称)
- **Phase 3**: マルチ(モデル転送・キャッシュ・許諾チェック)
- **Phase 4**: 見た目(トゥーンマテリアル、表情、アウトライン)と負荷対策

---

## 7. 参考リンク(調査時に使用)
- Steam ストア: https://store.steampowered.com/app/4968640/
- 公式サイト / クレジット(UE と Wwise 使用の明記): https://wilddog.jp/japaneseramensimulator/credit?lang=ja
- 配信ガイドライン(MOD の記述なし): https://wilddog.jp/guideline.html
- 既存 MOD(UE4SS 使用): https://www.nexusmods.com/japaneseramensimulator/mods/1
- 公式ロードマップ報道: https://news.denfaminicogamer.jp/news/2610162e
- VRM4U(参考のみ・コード流用禁止): https://github.com/ruyo/VRM4U / https://ruyo.github.io/VRM4U/
- UE4SS: https://github.com/UE4SS-RE/RE-UE4SS / https://docs.ue4ss.com
- VRM 仕様: https://github.com/vrm-c/vrm-specification

---

## 8. 進捗(2026-10-26 Claude Code セッション 1)

### 完了
- **Phase 0**: git 初期化、docs(README / DESIGN / BUILD / INSTALL / CREDITS)、
  `scripts/install.bat` / `uninstall.bat` / `ramen_vrm_setup.ps1`(本体は PowerShell 5.1、UTF-8 BOM 付きで保存すること)
  - 検出: レジストリ → libraryfolders.vdf → appmanifest_4968640.acf → exe 確認。実環境で検出成功(読み取りのみ)。
  - 実ゲームへは `-DryRun` のみ実行。書き込みを伴う通し試験は scratchpad の偽ゲームフォルダで実施
    (環境変数 `RVRM_STATE_DIR` / `RVRM_USERDATA_DIR` でマニフェストとユーザーデータの場所を差し替え可能)。
  - **UE4SS は配布物に同梱**(ユーザー決定。Nexus は使わない)。版は experimental-latest(UE 5.7 対応)を SHA-256 で固定。
    付属のチート/コンソール系サンプル MOD は同梱せず mods.txt で無効化。既存の UE4SS があればそちらを使い上書きしない。
    詳細・権利面の整理は `third_party/ue4ss/README.md`。配布物は `scripts/make_package.ps1` で作成。
- **Phase 1**: `core/`(rvrm)、単体テスト 22 件すべて成功、`tools/vrm_inspect`
  - ユーザーの PC にある実在 VRM 20 体(VRM 0.x / 1.0)で読み込み・スキニング・腕下げリターゲットを確認(OBJ を画像化して目視)。
  - 1 フレーム(リターゲット + SpringBone + スキニング)は VRoid 級 12〜17 万頂点で 1.2〜1.9 ms。
  - ビルド: VS 18 2026 Community + CMake 4.2(`docs/BUILD.md`)。

### 実装上の決定(DESIGN.md に詳細)
- VRM はソースのメッシュコンポーネント空間(+Y 前・-X 右・+Z 上・cm)へ変換して保持。ProceduralMesh は相対変換なしで付ける想定。
- リターゲットはグローバル回転の差分 + レスト骨方向の向き合わせ(Align)。ロールの違いは差分で打ち消される。
- VRM 0.x の secondaryAnimation は「最初の子を尾とする直列の鎖」に分解、末端は 7cm の仮想の尾。

### 次にやること(Phase 2)
- 同梱 UE4SS がこのゲームで起動するかの実機確認(ゲームへの導入・起動はユーザー確認が必要)。
  → ユーザーと合意した方針「B」: 途中で 1 回だけ 5 分の確認。調査用 Lua MOD `tools/probe/RamenVRM_Probe` を
    `make_package.ps1 -Probe` で配布物にし(`dist\RamenVRM_probe_dev`)、ユーザーが install → 起動 → ゲーム内へ → 終了。
    結果(`Win64\ue4ss\UE4SS.log` の [RamenVRM-Probe] 行、`UE4SS_ObjectDump.txt`、`CXXHeaderDump\`)を Claude が読み取りで回収する。
  → それ以外の実機確認は最後にまとめて行いたい、というのがユーザーの希望。
- C++ MOD のビルド環境: GitHub(503bad)と Epic の連携は済み。UEPseudo に HTTPS でアクセス可(SSH 鍵は無いので submodule は HTTPS で取得する)。
- ライブビューで `BP_Men_PlayerCharacter` 等のコンポーネント構成を調査。

## 9. 実機調査 1(2026-10-26)
- ユーザーが調査用 MOD を入れてゲームを起動 → 結果は `docs/RESEARCH.md`、生データは `research/probe_20261026/`(git 管理外)。
- 主要な結果: UE4SS 同梱版が動作 / 骨格・メッシュ空間・A ポーズは core の想定どおり / 頭ボーン名は `Head`。
- 実際のプレイ中の姿勢 1 フレームを VRM に適用して破綻がないことをオフラインで確認(`vrm_inspect --source-pose`)。
- 調査用 MOD はゲームから削除してよい(ユーザーが uninstall.bat を実行)。

## 10. Phase 2 実装(2026-10-26)
- `mod/RamenVRM`(UE4SS C++ MOD)を実装。ビルド環境: Rust(winget で rustup 導入)、`external/RE-UE4SS` を
  同梱 UE4SS と同じコミット `cdbc0b66` で取得(git 管理外)、`mod/CMakeLists.txt` → `build_mod/`。手順は docs/BUILD.md。
- 同梱する公式 UE4SS.dll に対して import の整合性を確認済み(46 関数すべて存在、VC++ ランタイムも新しい版を要求しない)。
- 配布物: `scripts/make_package.ps1 -Version 0.1.0` → `dist/RamenVRM_0.1.0(.zip)`。調査用 MOD からの入れ替えも試験済み。
- **未確認(最終の実機確認で見る)**: 実際に表示されるか、マテリアル(Widget3DPassThrough)の見た目、一人称判定、
  元キャラの非表示、負荷。ログは `Win64\ue4ss\UE4SS.log` の `[RamenVRM]` 行。
- Phase 3(マルチ)は未着手。2 人以上での確認が必要。Steam / EOS のどちらを使うかも未確定。

## 11. 実機確認 1 回目の結果と v0.2.0(2026-10-26)
- v0.1.0 で VRM の表示・置き換えは動作(ユーザー確認)。ただし**ほぼ真っ黒**。
  原因の判断: UI 用のアンリット マテリアル(Widget3DPassThrough)は、このゲームの自動露出の下では暗く見える。
  → `material_finder` を追加。ゲーム内の Surface / DefaultLit マテリアルから、ベースカラー用テクスチャパラメータを
    持つものを実行時に探して使う(候補一覧はログに出す。config.ini の material / material_texture で手動指定も可)。
- ユーザー要望「ロッカーから見た目を選びたい」→ カスタマイズ画面(プレビュー用 AMenCustomizePreviewStageActor を
  BeginPlay フックで検出)を開いている間、F5 / F6 で「元の見た目 / models の VRM」を切り替え。プレビューにも VRM を
  適用し、タイトル欄に名前を表示、選択は config.ini に保存。
  ゲームのカスタマイズ一覧に項目を差し込む案は、セーブデータ・レプリケーションに触れる危険が大きいので見送った。
- 最初のインストールで調査用パッケージの install.bat が実行されていた(紛らわしかった)。dist には本番の配布物だけを置く。

## 12. 実機確認 2 回目の結果と v0.3.0(2026-10-26)
- 結果: 色は出るようになった(material_finder が MM_Basic を選択)。ただし**ポリゴンが全て裏返り**、F5 で**クラッシュ**。
- 裏返り: 片面マテリアルになったことで判明。UE の ProceduralMesh では glTF の巻き順のまま(反転しない)が正しい。
  core の `ConvertOptions::flipWinding` の既定を false に変更。config は新しい項目名 `reverse_faces`(旧 `flip_winding` は
  既定値が誤っていたので読まない)。VRM で両面の部分は、片面マテリアルのとき裏向き三角形を足して両面にする。
- クラッシュ: ダンプ(ue4ss\crash_*.dmp)を Python の minidump で解析し、例外時のコンテキストから
  tick → bind → createMaterials → CreateDynamicMaterialInstance と特定。原因は material_finder が生ポインタで持っていた
  Masked の候補(/Game/Explosive_Devices/...)が、ゲーム側でアンロードされていたこと(解放済みオブジェクトの使用)。
  → 候補は FWeakObjectPtr で持ち、使う直前に確認。全部消えたら探し直す。半透明/切り抜きのマスターは透明度の入力が
  不明なので、どのアルファモードでも不透明のマテリアルを優先。
- 以後のクラッシュ解析用に、`make_package.ps1` が `dist\RamenVRM_<版>_symbols\`(main.dll / pdb / map)を残す。
  map で RVA → 関数名を引ける(リンクに /MAP を追加済み)。

## 13. 実機確認 3 回目の結果と v0.4.0(2026-10-27)
- 結果: クラッシュは解消。形状・揺れものは正常。ただし (a) VRM のテクスチャの上に別の模様・凹凸が重なる、
  (b) ロッカーのプレビューに VRM が写らない、(c) F5/F6 で何が起きたか分からない。
- (c) はログ上は正常に切り替わっていた(プレイヤーとプレビュー両方に適用)。(b) のせいで見えなかっただけ。
- (b) プレビューは AMenPreviewStageActor の SceneCaptureComponent(USceneCaptureComponent2D)で撮影しており、
  写す対象が限定されている。→ 作った ProceduralMesh を ShowOnlyComponent で登録(定期的に再登録)。
  毎フレーム撮影でなければ CaptureScene を呼ぶ。
- (a) 選ばれた MM_Basic が持つノーマルマップ等の既定値が重なっていた。→ 528 個の MIC の TextureParameterValues /
  ScalarParameterValues から、マスターごとの入力名を集める。余計なテクスチャ入力の少ないマスターを優先し、
  ノーマル系は DefaultNormal、汚れ・ディテール系は白、発光は黒、数値は metal 0 / rough 0.85 / 詳細・汚れ系 0 に設定。
  候補ごとの入力名はログに出る(次の調整の材料)。
- タイトル欄の表示は失敗していた模様。失敗理由をログに出すようにした。

## 14. 専用マテリアルとマルチプレイ(2026-10-27、v0.5.0 作業中)
- ユーザー決定: 専用マテリアルを pak で同梱する(UE 5.7.1 が `C:\Program Files\Epic Games\UE_5.7` にある)。
  マルチは「相手も同じ VRM ファイルを持っていれば ID だけ渡す」方式(ユーザー提案。ファイル転送はしない)。
- 専用マテリアル: `ue/RamenVRMContent`(コンテンツのみの UE プロジェクト)、`ue/scripts/create_materials.py`(エディタ Python で
  M_RVRM_{Lit,Unlit}_{Opaque,Masked,Translucent} を生成。すべて両面)、`ue/scripts/build_pak.ps1`(生成 → クック →
  /Game/RamenVRM の 6 個だけで IoStore コンテナ RamenVRM_P.{pak,utoc,ucas} を作成。格納先は ../../../RealRamenSimulator/Content/)。
  - Config はゲームの DefaultEngine.ini(pak から UnrealPak -Extract で取得、research/pak_config)のレンダラー設定を写す。
    ただし r.Lumen.Supported.SM5=1 は標準 5.7 だと SM5 のグローバルシェーダーがコンパイルエラーになるので外した。
  - bShareMaterialShaderCode=False(シェーダーをアセット内に持たせる)。
  - インストーラは paks\ を `<ゲーム>\RealRamenSimulator\Content\Paks\~mods\` に置く。アンインストールの対象範囲をゲームフォルダ全体に広げた。
  - MOD は KismetSystemLibrary::LoadAsset_Blocking(TSoftObjectPtr を 40 バイトの生データで組み立て)で読み込む。
    読めなければ material_finder(ゲーム内のマテリアル)に戻る。F7 で lit / unlit(アニメ調)切り替え。
- マルチ: ゲームの通信は EOS(DefaultPlatformService=EOS、NetDriverEOS)。Steam は実績用に初期化済み(steam_api64 v161)。
  → Steam のリッチプレゼンス rvrm_model(ファイル内容の FNV-1a 64bit)と rvrm_player(PlayerState の PlayerNamePrivate)を公開し、
    同じゲームを遊んでいるフレンドの値を読む。GameState.PlayerArray の PlayerNamePrivate と照合し、自分の models に同じ ID の
    ファイルがあればそのキャラに表示。フレンド同士のみ。2 人での実機確認が必要。

## 15. 実機確認 5 回目(v0.5.0)と v0.5.1(2026-10-27)
- 専用マテリアルは**ゲームで正しく読み込まれ、見た目は良好**(ユーザーのスクリーンショットで確認)。
- 不具合: (a) 一度持った物が透明になる、(b) ロッカーのプレビューに VRM が写らない(ログ上は適用・カメラ登録とも成功)。
- (a) 原因: 元キャラを隠す処理が CharacterMesh に取り付けられた部品をすべて対象にしていて、手に持った道具
  (他のアクターの部品)まで描画を止めていた。→ GetOwner が自分のキャラのものだけ隠す。
- (b) 原因は未確定。v0.5.1 で、元メッシュのライティングチャンネルを VRM にコピー(プレビューが専用チャンネルで照らしている
  可能性)し、カメラの設定(PrimitiveRenderMode / ShowOnly* / 撮影タイミング)・位置・チャンネルをログに出す([診断] 行)。

## 16. ロッカーのプレビューは断念、v0.6.0(2026-10-27)
- v0.5.5 の調査版でも「元のキャラだけ写る」= VRM はプレビュー用カメラに撮影されない(ShowOnlyComponents に入れ、
  毎フレーム入れ直して CaptureScene しても写らない。原因は未特定)。
- ユーザー判断: 「VRM 自体は正常に出ているので、F5/F6 で切り替える前提でヨシとする」。
  → プレビューには一切触らない(元のキャラのまま)。F5/F6/F7 はロッカー以外でもいつでも効く。
- ユーザー要望: 配布物の install.bat と同じ場所の models フォルダに置いた VRM を使いたい。
  → install.bat が `%LOCALAPPDATA%\RamenVRM\models_location.txt` に配布物の models の絶対パスを書き、
    MOD はそこ(優先)と `ドキュメント\RamenVRM\models` の両方を読む。コピーはしない。uninstall は記録だけ消す。
