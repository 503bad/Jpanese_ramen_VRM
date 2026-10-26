# 配布物(dist\RamenVRM_<版>\ と .zip)を作る。開発者用。
# UTF-8(BOM 付き)で保存すること。
#
#   powershell -ExecutionPolicy Bypass -File scripts\make_package.ps1 [-Version 0.1.0] [-ModDll <path>] [-AllowMissingMod] [-Probe]
#   -Probe : 調査用 MOD(tools\probe\RamenVRM_Probe)だけを入れた配布物を作る(Phase 2 前の 5 分確認用)

[CmdletBinding()]
param(
    [string]$Version = 'dev',
    # MOD 本体(Phase 2 のビルド成果物)
    [string]$ModDll = '',
    # MOD 本体が無くても作る(インストーラの試験用)
    [switch]$AllowMissingMod,
    # 調査用 MOD だけを入れる
    [switch]$Probe
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$Repo = Split-Path -Parent $PSScriptRoot
$UE4SSZipName = 'UE4SS_v3.0.1-1183-gcdbc0b66.zip'
$UE4SSSha256 = 'e4a47e23e2eeb8379c4fc388d1b1686f01c6c5930d84a2ee4a6a9574b274e681'
$UE4SSZip = Join-Path $Repo "third_party\ue4ss\$UE4SSZipName"
# zip から取り出すもの(それ以外の付属 MOD 等は入れない。理由は third_party\ue4ss\README.md)
$UE4SSKeep = @('dwmapi.dll', 'ue4ss/UE4SS.dll', 'ue4ss/UE4SS-settings.ini', 'ue4ss/LICENSE')

if ($Probe) { $Out = Join-Path $Repo "dist\RamenVRM_probe_$Version" } else { $Out = Join-Path $Repo "dist\RamenVRM_$Version" }
$OutZip = "$Out.zip"

function Copy-To([string]$src, [string]$dstRel) {
    $dst = Join-Path $Out $dstRel
    $dir = Split-Path $dst -Parent
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
    Copy-Item -LiteralPath $src -Destination $dst
}

# ---- UE4SS の検証
if (-not (Test-Path -LiteralPath $UE4SSZip)) {
    throw "UE4SS の zip がありません: $UE4SSZip`n" +
          "  gh release download experimental-latest -R UE4SS-RE/RE-UE4SS -p $UE4SSZipName -D third_party\ue4ss`n" +
          "  (experimental-latest は差し替わるため、同じファイルが取れない場合は third_party\ue4ss\README.md の版を更新する)"
}
$hash = (Get-FileHash -LiteralPath $UE4SSZip -Algorithm SHA256).Hash.ToLowerInvariant()
if ($hash -ne $UE4SSSha256) { throw "UE4SS の zip の SHA-256 が一致しません: $hash" }

# ---- 出力先を作り直す
if (Test-Path -LiteralPath $Out) { Remove-Item -LiteralPath $Out -Recurse -Force }
if (Test-Path -LiteralPath $OutZip) { Remove-Item -LiteralPath $OutZip -Force }
New-Item -ItemType Directory -Path $Out | Out-Null

# ---- インストーラ
Copy-To (Join-Path $Repo 'scripts\install.bat') 'install.bat'
Copy-To (Join-Path $Repo 'scripts\uninstall.bat') 'uninstall.bat'
Copy-To (Join-Path $Repo 'scripts\ramen_vrm_setup.ps1') 'scripts\ramen_vrm_setup.ps1'

# ---- UE4SS(必要なファイルだけ)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::OpenRead($UE4SSZip)
try {
    foreach ($name in $UE4SSKeep) {
        $e = $zip.GetEntry($name)
        if (-not $e) { throw "UE4SS の zip に $name がありません" }
        $dst = Join-Path $Out ('ue4ss_bundle\' + ($name -replace '/', '\'))
        $dir = Split-Path $dst -Parent
        if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e, $dst, $false)
    }
} finally { $zip.Dispose() }
$modsTxt = Join-Path $Out 'ue4ss_bundle\ue4ss\Mods\mods.txt'
New-Item -ItemType Directory -Path (Split-Path $modsTxt -Parent) | Out-Null
# UE4SS 付属のサンプル MOD(チート・コンソール有効化など)は同梱せず、すべて無効にしておく。
# Ramen VRM は自分のフォルダの enabled.txt で有効になるので、ここに書く必要はない。
[System.IO.File]::WriteAllText($modsTxt,
    "; Ramen VRM MOD bundle: UE4SS sample mods are not included and are disabled.`r`n" +
    "CheatManagerEnablerMod : 0`r`nConsoleCommandsMod : 0`r`nConsoleEnablerMod : 0`r`nSplitScreenMod : 0`r`n" +
    "LineTraceMod : 0`r`nBPML_GenericFunctions : 0`r`nBPModLoaderMod : 0`r`nKeybinds : 0`r`n")

# ---- MOD 本体
if ($Probe) {
    foreach ($f in (Get-ChildItem -LiteralPath (Join-Path $Repo 'tools\probe\RamenVRM_Probe') -Recurse -File)) {
        $rel = $f.FullName.Substring((Join-Path $Repo 'tools\probe\').Length)
        Copy-To $f.FullName ("mod\" + $rel)
    }
} else {
    # 既定は mod\CMakeLists.txt のビルド結果(docs\BUILD.md)
    if (-not $ModDll) { $ModDll = Join-Path $Repo 'build_mod\RamenVRM\Game__Shipping__Win64\main.dll' }
    if (Test-Path -LiteralPath $ModDll) {
        Copy-To $ModDll 'mod\RamenVRM\dlls\main.dll'
        # クラッシュ解析用のシンボル(配布物には入れず、dist の横に保管する)
        $symDir = "$Out`_symbols"
        if (Test-Path -LiteralPath $symDir) { Remove-Item -LiteralPath $symDir -Recurse -Force }
        New-Item -ItemType Directory -Path $symDir | Out-Null
        foreach ($ext in @('.pdb', '.map')) {
            $f = [System.IO.Path]::ChangeExtension($ModDll, $ext)
            if (Test-Path -LiteralPath $f) { Copy-Item -LiteralPath $f -Destination $symDir }
        }
        Copy-Item -LiteralPath $ModDll -Destination $symDir
    } elseif ($AllowMissingMod) {
        Write-Host "[注意] MOD 本体がありません($ModDll)。インストーラ試験用の配布物として作ります。" -ForegroundColor Yellow
    } else {
        throw "MOD 本体がありません: $ModDll(試験用なら -AllowMissingMod)"
    }
}

# ---- VRM を置くフォルダ(install.bat の隣。MOD はここを直接読む)
$modelsDir = Join-Path $Out 'models'
New-Item -ItemType Directory -Path $modelsDir -Force | Out-Null
[System.IO.File]::WriteAllText((Join-Path $modelsDir 'ここに VRM を置いてください.txt'),
    "このフォルダに .vrm ファイルを置くと、ゲーム中に F5 / F6 で選べるようになります。`r`n" +
    "(install.bat を実行したあとに追加しても大丈夫です)`r`n`r`n" +
    "・F5 / F6: 見た目を切り替え(元の見た目 → VRM をファイル名順)`r`n" +
    "・F7: 表示方法を切り替え(ライティングあり / アニメ調)`r`n`r`n" +
    "このフォルダ(配布物)を移動した場合は、install.bat を実行し直してください。`r`n" +
    "マルチプレイでは、相手も同じ VRM ファイルを持っていて、Steam のフレンドであれば、お互いの VRM が見えます。`r`n",
    (New-Object System.Text.UTF8Encoding($true)))

# ---- 専用マテリアル(ue\scripts\build_pak.ps1 の出力)
$pakDir = Join-Path $Repo 'ue\build\paks'
if (-not $Probe -and (Test-Path -LiteralPath $pakDir)) {
    foreach ($f in (Get-ChildItem -LiteralPath $pakDir -File)) { Copy-To $f.FullName ("paks\" + $f.Name) }
} elseif (-not $Probe) {
    Write-Host '[注意] 専用マテリアルの pak がありません(ue\scripts\build_pak.ps1 を実行)。ゲーム内のマテリアルで代用されます' -ForegroundColor Yellow
}

# ---- ツール
$inspect = Join-Path $Repo 'build\tools\vrm_inspect\Release\vrm_inspect.exe'
if (Test-Path -LiteralPath $inspect) { Copy-To $inspect 'tools\vrm_inspect.exe' }
else { Write-Host '[注意] vrm_inspect.exe が未ビルドのため入れません' -ForegroundColor Yellow }

# ---- ドキュメントとライセンス
Copy-To (Join-Path $Repo 'README.md') 'README.md'
Copy-To (Join-Path $Repo 'docs\INSTALL.md') 'docs\INSTALL.md'
Copy-To (Join-Path $Repo 'docs\CREDITS.md') 'docs\CREDITS.md'
Copy-To (Join-Path $Repo 'LICENSE') 'LICENSE.txt'
Copy-To (Join-Path $Repo 'third_party\cgltf\LICENSE') 'licenses\cgltf.txt'
Copy-To (Join-Path $Repo 'third_party\ue4ss\LICENSE') 'licenses\UE4SS.txt'
Copy-To (Join-Path $Repo 'third_party\ue4ss\README.md') 'licenses\UE4SS_bundle_README.md'
foreach ($f in (Get-ChildItem -LiteralPath (Join-Path $Repo 'third_party\ue4ss\licenses') -File)) {
    Copy-To $f.FullName ("licenses\UE4SS_deps\" + $f.Name)
}

Compress-Archive -Path (Join-Path $Out '*') -DestinationPath $OutZip
Write-Host "作成しました: $Out"
Write-Host "             $OutZip"
