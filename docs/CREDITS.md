# ライセンス・クレジット

同梱物・外部のソフトウェアには、それぞれのライセンスが適用されます。

## 同梱しているソフトウェア

| 名前 | 用途 | ライセンス | ライセンス文 |
|---|---|---|---|
| [cgltf](https://github.com/jkuhlmann/cgltf) v1.15(Copyright (c) 2018-2021 Johannes Kuhlmann) | glTF / VRM ファイルの解析(MOD 本体に組み込み) | MIT | `third_party/cgltf/LICENSE`、配布物の `licenses\cgltf.txt` |
| [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS)(RE-UE4SS、experimental `v3.0.1-1183-gcdbc0b66`) | MOD をゲームに読み込ませる土台(配布物に同梱) | MIT | `third_party/ue4ss/LICENSE`、配布物の `licenses\UE4SS.txt` |

UE4SS.dll には次のライブラリが組み込まれています。ライセンス文は `third_party/ue4ss/licenses/`
(配布物の `licenses\UE4SS_deps\`)にあります。詳細は [third_party/ue4ss/README.md](../third_party/ue4ss/README.md)。

| ライブラリ | ライセンス |
|---|---|
| GLFW | zlib/libpng |
| PolyHook 2 | MIT |
| asmjit / asmtk | zlib |
| Zydis / Zycore | MIT |
| fmt | MIT |
| glad | MIT |
| Dear ImGui | MIT |
| raw_pdb | BSD-2-Clause |
| Tracy | BSD-3-Clause |
| Lua | MIT |
| patternsleuth | MIT OR Apache-2.0(MIT を選択) |
| Droid Sans Fallback(フォント) | Apache-2.0 |

## 専用マテリアルについて

専用マテリアル(`RamenVRM_P.pak`)は、この MOD のために Unreal Engine 5.7 で作成したものです。
Unreal® is a trademark or registered trademark of Epic Games, Inc. in the United States of America and elsewhere.
Unreal® Engine, Copyright 1998 – 2026, Epic Games, Inc. All rights reserved.

## 実行時に使うもの(配布物には含まない)

- ゲーム本体(Japanese Ramen Simulator)の機能・アセット。専用マテリアルが読めない場合は、ゲーム内のマテリアルを借りて表示する。
- Steamworks API: ゲームが読み込んでいる `steam_api64.dll` を通じて、フレンド間でのモデル ID の共有(リッチプレゼンス)に使う。
- MOD は UE4SS の C++ API を使い、UE4SS のソース(同じコミット)に対してビルドしている。

## 実装の根拠にした仕様

- glTF 2.0(Khronos Group): https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
- VRM 仕様(VRM コンソーシアム): https://github.com/vrm-c/vrm-specification
  - VRM 1.0: `VRMC_vrm`、`VRMC_springBone`、`VRMC_materials_mtoon`
  - VRM 0.x: `extensions.VRM`(humanoid / meta / blendShapeMaster / secondaryAnimation / firstPerson / materialProperties)
  - 揺れもの(SpringBone)は VRMC_springBone 1.0 仕様の説明に従って実装した(`core/src/springbone.cpp`)。

## 参考にしたもの(コードは使っていない)

- [VRM4U](https://github.com/ruyo/VRM4U)(ruyo): UE で VRM を表示するという全体像の参考。
- [UniVRM](https://github.com/vrm-c/UniVRM)(VRM コンソーシアム): VRM 0.x の揺れものの挙動(末端の仮想の尾、座標系の扱い)を合わせる際の参考。
