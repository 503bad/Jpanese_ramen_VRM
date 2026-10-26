# mod/ — UE4SS C++ MOD

ゲームとの接点だけを持つ層です。VRM の処理はすべて `core/`(rvrm)に任せます。ビルド方法は [docs/BUILD.md](../docs/BUILD.md)。

| ファイル | 役割 |
|---|---|
| `src/dllmain.cpp` | MOD のエントリ。エンジンの Tick(ゲームスレッド)で自分のキャラクターを探し、VRM の読み込み(ワーカースレッド)と適用を管理 |
| `src/vrm_character.*` | キャラクター 1 体への適用: 骨のレスト姿勢の取得 → ProceduralMesh 作成 → 元の表示を隠す → 毎フレーム更新 |
| `src/ue_call.*` | UFunction を名前で呼ぶヘルパー(引数の位置・サイズは実行時に FProperty から取得して照合) |
| `src/settings.*` | `ドキュメント\RamenVRM\config.ini` と models フォルダ |
| `src/log.*` | UE4SS のログ出力(行頭 `[RamenVRM]`) |

設計上の要点(詳細は [docs/DESIGN.md](../docs/DESIGN.md)、調査結果は [docs/RESEARCH.md](../docs/RESEARCH.md)):
- 自分のキャラクターは `GameEngine → GameViewport → GameInstance → LocalPlayers[0] → PlayerController → Pawn` をたどって探す
  (全オブジェクト走査の `FindAllOf` は重いので使わない)。
- 元のキャラクターは `SetVisibility` ではなく「本体・深度・影の描画」だけを止める。アニメーションは動かしたまま、
  ゲーム側の表示切り替えとも干渉しない。元の設定は記録して元に戻せる。
- マテリアルはエンジン標準の `Widget3DPassThrough` 系(アンリット)。既定ですべて両面表示。
- 一人称はカメラと頭の距離で判定し、VRM の firstPerson 注釈に従って頭の三角形を外す。
- 失敗したらそのキャラクターは元の表示に戻して停止する(ゲームを壊さない)。
