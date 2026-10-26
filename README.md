# Ramen VRM MOD

Japanese Ramen Simulator(Steam AppID 4968640)のプレイヤーキャラクターを、
**各プレイヤーが自分で用意した VRM モデル**で表示する非公式 MOD です(開発中)。

- VRM 0.x / 1.0 に対応(読み込みは MOD 内で自前実装。エンジンプラグインは不要)
- ゲーム本体のアニメーションを VRM の骨格へリターゲットし、CPU スキニングで表示
- 揺れもの(VRM SpringBone)・表情に対応予定
- マルチプレイ: VRM の利用許諾(再配布可否)を確認し、許可されたモデルだけを他プレイヤーへ送る予定

## 現在の状態

| フェーズ | 内容 | 状態 |
|---|---|---|
| Phase 0 | リポジトリ雛形、install/uninstall スクリプト、配布物作成(UE4SS 同梱) | 完了(ゲーム検出・dry-run・偽フォルダでの通し試験済み) |
| Phase 1 | core ライブラリ(VRM 解析・座標変換・スキニング・リターゲット・SpringBone)、vrm_inspect | 完了(単体テスト 22 件、実在の VRM 20 体で読み込み確認) |
| Phase 2 | UE4SS C++ MOD で自分のキャラクターを VRM 表示 | 実装済み・**実機での最終確認待ち** |
| Phase 3 | マルチプレイ(モデル転送・キャッシュ・許諾チェック) | 未着手 |
| Phase 4 | 見た目(トゥーン・表情・アウトライン)と負荷対策 | 未着手 |

## 使い方(利用者向け)

[docs/INSTALL.md](docs/INSTALL.md) の手順で `install.bat` を実行し、`ドキュメント\RamenVRM\models` に VRM を置きます。
現在は**自分のキャラクターだけ**が VRM になります(他プレイヤーへの表示は Phase 3)。

## 開発者向け

- 設計: [docs/DESIGN.md](docs/DESIGN.md)
- ビルド: [docs/BUILD.md](docs/BUILD.md)
- 出典・ライセンス: [docs/CREDITS.md](docs/CREDITS.md)
- 引き継ぎメモ: [CLAUDE.md](CLAUDE.md)

```
core/            エンジン非依存の C++17 ライブラリ(rvrm)と単体テスト
tools/vrm_inspect VRM の中身表示・OBJ 書き出し(core の動作確認用)
mod/             UE4SS C++ MOD(Phase 2 で実装)
scripts/         install.bat / uninstall.bat / ramen_vrm_setup.ps1 / make_package.ps1
third_party/     cgltf(MIT)、同梱する UE4SS の情報とライセンス(MIT ほか)
```

## 注意

- 非公式 MOD です。ゲームの開発元とは関係ありません。
- VRM モデルの利用は、各モデルの利用条件(VRM の meta に記載)に従ってください。
