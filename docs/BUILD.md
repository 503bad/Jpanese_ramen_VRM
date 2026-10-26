# ビルド手順

## 必要なもの

- Visual Studio 2022 以降(C++ によるデスクトップ開発。開発環境では VS 18 / 2026 Community で確認)
- CMake 3.20 以降
- (Phase 2 以降)UE4SS の C++ MOD 開発環境 — 下記「MOD 本体」を参照

## core / テスト / vrm_inspect

```bat
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
build\core\Release\rvrm_tests.exe
```

VS 2022 の場合はジェネレーター名を `"Visual Studio 17 2022"` にします。

- `rvrm_tests.exe [フィルタ]` … 単体テスト。合成した VRM(GLB)をメモリ上で生成して検証するので、
  実在のモデルやゲームは不要です。
- `vrm_inspect.exe <file.vrm> [--obj out.obj] [--apose 度] [--textures]`
  - ヒューマノイド対応表、メッシュ数、meta(利用許諾)とマルチプレイ送信の可否判定、揺れもの、警告を表示
  - `--obj` で core のパイプライン(UE 空間へ変換 → リターゲット → CPU スキニング → glTF 空間へ戻す)を通した
    結果を OBJ に書き出す。`--apose 50` で腕を 50° 下げたソース姿勢をリターゲットして確認できる

## 配布物の作成

```bat
powershell -ExecutionPolicy Bypass -File scripts\make_package.ps1 -Version 0.1.0
```

- `dist\RamenVRM_<版>\` と `.zip` ができる。MOD 本体が無い段階では `-AllowMissingMod` で試験用に作れる。
- 同梱する UE4SS の zip は `third_party\ue4ss\` に置く(git 管理外)。SHA-256 を検証し、必要なファイルだけを
  `ue4ss_bundle\` に取り出す。詳細と取得コマンドは `third_party/ue4ss/README.md`。

## MOD 本体

UE4SS の C++ MOD としてビルドする。**同梱する UE4SS.dll と同じコミット**(`cdbc0b66`)のソースに対してビルドすること
(MOD は UE4SS.dll が公開する C++ の関数を直接呼ぶため、版が違うと読み込めない)。

### 準備(初回のみ)

```bat
rem Rust(UE4SS の一部 patternsleuth が Rust 製。nightly は自動で入る)
winget install --id Rustlang.Rustup -e

rem UE4SS のソース(git 管理外の external\ に置く)。UEPseudo は Epic 連携済みの GitHub アカウントが必要
git clone https://github.com/UE4SS-RE/RE-UE4SS.git external\RE-UE4SS
cd external\RE-UE4SS
git checkout cdbc0b66
git -c url."https://github.com/".insteadOf="git@github.com:" submodule update --init --recursive
```

### ビルド

```bat
cmake -S mod -B build_mod -G "Visual Studio 18 2026" -A x64
cmake --build build_mod --config Game__Shipping__Win64 --target RamenVRM --parallel
```

出力は `build_mod\RamenVRM\Game__Shipping__Win64\main.dll`(make_package.ps1 の既定の入力)。初回は UE4SS 本体も
ビルドされるので数分かかる。

確認したこと(2026-10-26):
- MOD が UE4SS.dll から使う 46 個の関数は、すべて同梱する公式の UE4SS.dll に存在する。
- C++ ランタイム(MSVCP140 等)で使う関数は、VC++ 2015 以降のランタイムにあるものだけ(新しいランタイムを要求しない)。

RE-UE4SS の README で確認したこと:

- ビルドは CMake(3.22 以上)+ Visual Studio。構成名は `Game__Shipping__Win64` など。
- **GitHub アカウントを Epic Games アカウントに連携する必要がある**(UE の擬似コードのサブモジュール UEPseudo を取得するため)。
  開発者だけの一度きりの作業で、MOD の利用者には関係ない。
- UE 5.7 対応は experimental 版のみ(安定版 v3.0.1 は未対応)。
- 配布物の中では `mod\RamenVRM\dlls\main.dll` に置かれ、UE4SS の `Mods\RamenVRM\dlls\main.dll` に入る。
