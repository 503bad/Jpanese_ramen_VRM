# 同梱する UE4SS

| 項目 | 値 |
|---|---|
| 配布元 | https://github.com/UE4SS-RE/RE-UE4SS/releases/tag/experimental-latest |
| ファイル | `UE4SS_v3.0.1-1183-gcdbc0b66.zip`(2026-10-25 ビルド) |
| SHA-256 | `e4a47e23e2eeb8379c4fc388d1b1686f01c6c5930d84a2ee4a6a9574b274e681` |
| ライセンス | MIT(`LICENSE`) |
| 選定理由 | 安定版 v3.0.1(2024-02)は UE 5.7 に未対応。changelog で「Added support for UE Version 5.7」があるのは experimental 版のみ |

- `experimental-latest` は同じタグのまま中身が差し替わるため、**zip は版を固定してこのフォルダに保存**する
  (サイズが大きいので git には入れない。`.gitignore` 済み)。取り直すときは `scripts/make_package.ps1` が SHA-256 を検証する。
- このゲームで実際に動くかは未確認(Phase 2 で実機確認)。

## 同梱するもの / しないもの

`scripts/make_package.ps1` が zip から次だけを取り出して `ue4ss_bundle/` に置く:

| ファイル | 同梱 | 理由 |
|---|---|---|
| `dwmapi.dll` | する | UE4SS をゲームに読み込ませるプロキシ |
| `ue4ss/UE4SS.dll` | する | 本体 |
| `ue4ss/UE4SS-settings.ini` | する | 設定(既定のまま。コンソール・GUI は既定で無効) |
| `ue4ss/LICENSE` | する | MIT の条件(著作権表示の同梱) |
| `ue4ss/Mods/mods.txt` | **差し替え** | 既定では `CheatManagerEnablerMod` / `ConsoleEnablerMod` などが有効。マルチプレイのゲームでチート機能を有効にして配るのは不適切なので、付属 MOD はすべて無効の内容に差し替える |
| `ue4ss/Mods/*`(付属の Lua MOD) | しない | 同上。この MOD の動作に不要 |
| `ue4ss/UE4SS_SDK_Backends/` | しない | C++ ヘッダー生成用(開発者向け) |

## UE4SS.dll に含まれる第三者ライブラリ

UE4SS の `deps/` から確認した、UE4SS.dll に組み込まれるライブラリ。ライセンス本文は `licenses/` に置き、
配布物の `licenses/` にもそのまま入れる(公式 zip には UE4SS 自身の LICENSE しか入っていないため、こちらで補う)。

| ライブラリ | ライセンス |
|---|---|
| GLFW | zlib/libpng |
| PolyHook 2 | MIT |
| asmjit / asmtk(PolyHook の依存) | zlib |
| Zydis / Zycore | MIT |
| fmt | MIT |
| glad | MIT(生成コード部分は `glad.txt` 参照) |
| Dear ImGui | MIT |
| raw_pdb | BSD-2-Clause |
| Tracy | BSD-3-Clause |
| Lua | MIT |
| patternsleuth | MIT OR Apache-2.0(MIT を選択。`patternsleuth.txt`) |
| Droid Sans Fallback(フォント) | Apache-2.0(`DroidSansFallback_NOTICE.txt`) |

ライセンス本文は各ライブラリの公式リポジトリの現行版から取得した(2026-10-26)。
UE4SS が固定している版と著作権表示の年が異なる場合がある。

## 権利面の判断(メモ)

- UE4SS は MIT。著作権表示とライセンス文を同梱すれば再配布・同梱が可能。
- 第三者ライブラリはすべて MIT / BSD / zlib / Apache-2.0 で、ライセンス文の同梱で再配布可能。
- UE4SS のビルドには Epic Games 連携が必要なサブモジュール(UEPseudo)が使われるが、**ビルド済みバイナリは
  UE4SS プロジェクト自身が GitHub で一般公開している**。こちらはそれを改変せずに同梱するだけで、UEPseudo のソースは含まない。
- 法的な助言ではない。配布前に気になる点があれば改めて確認する。
