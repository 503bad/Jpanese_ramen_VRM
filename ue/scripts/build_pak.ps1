# 専用マテリアルの pak(RamenVRM_P.pak / .utoc / .ucas)を作る。開発者用。UTF-8(BOM 付き)で保存すること。
#
#   powershell -ExecutionPolicy Bypass -File ue\scripts\build_pak.ps1 [-EngineDir "C:\Program Files\Epic Games\UE_5.7"] [-SkipCook]
#
# 1. エディタの Python でマテリアルを生成(ue\scripts\create_materials.py)
# 2. BuildCookRun でクック(シェーダーはマテリアル内に持たせる設定。ue\RamenVRMContent\Config)
# 3. クック結果のうち /Game/RamenVRM の 6 個だけで IoStore コンテナを作る
#    (UAT が作るコンテナにはエンジンの既定アセットも入り、ゲーム側の同名アセットを上書きしてしまうため使わない)
#    ゲームでは /Game が RealRamenSimulator/Content に割り当てられているので、格納先の名前もそれに合わせる
# 出力: ue\build\paks\

[CmdletBinding()]
param(
    [string]$EngineDir = 'C:\Program Files\Epic Games\UE_5.7',
    [switch]$SkipCook
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$Project = Join-Path $Root 'ue\RamenVRMContent\RamenVRMContent.uproject'
$Editor = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$Uat = Join-Path $EngineDir 'Engine\Build\BatchFiles\RunUAT.bat'
$UnrealPak = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealPak.exe'
$Cooked = Join-Path $Root 'ue\RamenVRMContent\Saved\Cooked\Windows'
$Meta = Join-Path $Cooked 'RamenVRMContent\Metadata'
$Work = Join-Path $Root 'ue\build\work'
$Out = Join-Path $Root 'ue\build\paks'
$ContainerName = 'RamenVRM_P'

foreach ($p in @($Editor, $Uat, $UnrealPak)) { if (-not (Test-Path -LiteralPath $p)) { throw "見つかりません: $p" } }

if (-not $SkipCook) {
    Write-Host '== マテリアルを生成'
    & $Editor $Project -run=pythonscript -script="$(Join-Path $Root 'ue\scripts\create_materials.py')" -unattended -nop4 -nosplash -NoSound -stdout *> (Join-Path $Root 'ue\create.log')
    if ($LASTEXITCODE -ne 0) { throw "マテリアルの生成に失敗しました(ue\create.log)" }

    Write-Host '== クック(初回はシェーダーのコンパイルで時間がかかる)'
    & $Uat BuildCookRun -project="$Project" -noP4 -platform=Win64 -clientconfig=Shipping -cook -stage -pak -iostore -skipbuild -nocompileeditor -unattended -utf8output -stagingdirectory="$(Join-Path $Root 'ue\build\staged')" *> (Join-Path $Root 'ue\cook.log')
    if ($LASTEXITCODE -ne 0) { throw "クックに失敗しました(ue\cook.log)" }
}

Write-Host '== RamenVRM だけのコンテナを作成'
if (Test-Path -LiteralPath $Work) { Remove-Item -LiteralPath $Work -Recurse -Force }
if (Test-Path -LiteralPath $Out) { Remove-Item -LiteralPath $Out -Recurse -Force }
New-Item -ItemType Directory -Path $Work, $Out | Out-Null

$assets = @(Get-ChildItem -LiteralPath (Join-Path $Cooked 'RamenVRMContent\Content\RamenVRM') -Filter '*.uasset')
if ($assets.Count -eq 0) { throw 'クック済みのマテリアルがありません' }
$list = Join-Path $Work 'iostore_list.txt'
$lines = foreach ($a in $assets) { '"{0}" "../../../RealRamenSimulator/Content/RamenVRM/{1}" -compress' -f $a.FullName, $a.Name }
[System.IO.File]::WriteAllLines($list, [string[]]$lines)

$commands = Join-Path $Work 'iostore_commands.txt'
[System.IO.File]::WriteAllText($commands, ('-Output="{0}" -ContainerName={1} -ResponseFile="{2}"' -f (Join-Path $Out "$ContainerName.utoc"), $ContainerName, $list))

& $UnrealPak $Project -CreateGlobalContainer="$(Join-Path $Work 'global.utoc')" -PackageStoreManifest="$(Join-Path $Meta 'packagestore.manifest')" `
    -CookedDirectory="$Cooked" -Commands="$commands" -ScriptObjects="$(Join-Path $Meta 'scriptobjects.bin')" `
    -compressionformats=Oodle -compresslevel=4 -compressmethod=Kraken -WriteBackMetadataToAssetRegistry=Disabled -platform=Windows -unattended *> (Join-Path $Work 'unrealpak_iostore.log')
if ($LASTEXITCODE -ne 0) { throw "IoStore コンテナの作成に失敗しました($Work\unrealpak_iostore.log)" }

# IoStore コンテナは同名の .pak と組で読み込まれる。中身は説明のテキストだけでよい
$readme = Join-Path $Work 'RamenVRM_README.txt'
[System.IO.File]::WriteAllText($readme, "Ramen VRM MOD materials (/Game/RamenVRM). unofficial mod`r`n")
$pakList = Join-Path $Work 'pak_list.txt'
[System.IO.File]::WriteAllText($pakList, ('"{0}" "../../../RealRamenSimulator/Content/RamenVRM/RamenVRM_README.txt"' -f $readme))
& $UnrealPak (Join-Path $Out "$ContainerName.pak") -create="$pakList" -platform=Windows -unattended *> (Join-Path $Work 'unrealpak_pak.log')
if ($LASTEXITCODE -ne 0) { throw ".pak の作成に失敗しました($Work\unrealpak_pak.log)" }

Get-ChildItem -LiteralPath $Out | ForEach-Object { Write-Host ("  {0}  {1:N0} bytes" -f $_.Name, $_.Length) }
Write-Host "作成しました: $Out"
