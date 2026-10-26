# Ramen VRM MOD のインストール / アンインストール本体。install.bat / uninstall.bat から呼ばれる。
# Windows PowerShell 5.1 で動くように書くこと(?? や三項演算子は使わない)。
# このファイルは UTF-8(BOM 付き)で保存すること(5.1 が日本語を正しく読むため)。

[CmdletBinding()]
param(
    [ValidateSet('Install', 'Uninstall', 'Detect')]
    [string]$Mode = 'Detect',
    # ゲームのフォルダ(ルート or Binaries\Win64)を明示指定する
    [string]$GamePath = '',
    # 何も書き込まずに予定だけ表示する
    [switch]$DryRun,
    # 確認を省略する
    [switch]$Yes,
    # アンインストール時にユーザーデータ(VRM・設定・キャッシュ)も削除する
    [switch]$RemoveUserData
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$AppId = '4968640'
$ExeRelative = 'RealRamenSimulator\Binaries\Win64\RealRamenSimulator-Win64-Shipping.exe'
$PackageRoot = Split-Path -Parent $PSScriptRoot
# 同梱の UE4SS(Win64 フォルダと同じ並びで置いてある。scripts/make_package.ps1 が作る)
$BundleDir = Join-Path $PackageRoot 'ue4ss_bundle'
$StateDir = Join-Path $env:LOCALAPPDATA 'RamenVRM'
$UserDataDir = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'RamenVRM'
# テスト用の上書き(通常は使わない)
if ($env:RVRM_STATE_DIR) { $StateDir = $env:RVRM_STATE_DIR }
if ($env:RVRM_USERDATA_DIR) { $UserDataDir = $env:RVRM_USERDATA_DIR }
$ManifestPath = Join-Path $StateDir 'install_manifest.json'
$ModelsLocation = Join-Path $StateDir 'models_location.txt'
$PackageModels = Join-Path $PackageRoot 'models'

function Write-Info([string]$m) { Write-Host $m }
function Write-Warn2([string]$m) { Write-Host "[注意] $m" -ForegroundColor Yellow }
function Write-Err2([string]$m) { Write-Host "[エラー] $m" -ForegroundColor Red }

function Confirm-Action([string]$question) {
    if ($Yes) { return $true }
    $ans = Read-Host "$question [y/N]"
    return ($ans -match '^(y|yes|はい)$')
}

# ---------------------------------------------------------------- 検出

function Get-SteamRoots {
    $roots = New-Object System.Collections.Generic.List[string]
    $keys = @(
        @{ Path = 'HKCU:\Software\Valve\Steam'; Name = 'SteamPath' },
        @{ Path = 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam'; Name = 'InstallPath' },
        @{ Path = 'HKLM:\SOFTWARE\Valve\Steam'; Name = 'InstallPath' }
    )
    foreach ($k in $keys) {
        try {
            $v = (Get-ItemProperty -Path $k.Path -Name $k.Name -ErrorAction Stop).($k.Name)
            if ($v) {
                $p = [System.IO.Path]::GetFullPath(($v -replace '/', '\'))
                if ((Test-Path -LiteralPath $p) -and -not ($roots -contains $p)) { $roots.Add($p) }
            }
        } catch { }
    }
    return ,$roots
}

# libraryfolders.vdf から "path" をすべて取り出す(VDF の厳密な解析は不要)
function Get-SteamLibraries([string[]]$steamRoots) {
    $libs = New-Object System.Collections.Generic.List[string]
    foreach ($root in $steamRoots) {
        if (-not ($libs -contains $root)) { $libs.Add($root) }
        $vdf = Join-Path $root 'steamapps\libraryfolders.vdf'
        if (-not (Test-Path -LiteralPath $vdf)) { continue }
        $text = [System.IO.File]::ReadAllText($vdf)
        foreach ($m in [regex]::Matches($text, '"path"\s+"((?:[^"\\]|\\.)*)"')) {
            $p = $m.Groups[1].Value -replace '\\\\', '\'
            try { $p = [System.IO.Path]::GetFullPath($p) } catch { continue }
            if ((Test-Path -LiteralPath $p) -and -not ($libs -contains $p)) { $libs.Add($p) }
        }
    }
    return ,$libs
}

function Resolve-GameRoot([string]$path) {
    # ルート / RealRamenSimulator / Binaries\Win64 のどれを渡されてもルートに直す
    if (-not $path) { return $null }
    try { $p = [System.IO.Path]::GetFullPath($path.Trim('"')) } catch { return $null }
    $cands = @($p, (Split-Path $p -Parent), (Split-Path (Split-Path (Split-Path $p -Parent) -Parent) -Parent),
               (Split-Path (Split-Path (Split-Path (Split-Path $p -Parent) -Parent) -Parent) -Parent))
    foreach ($c in $cands) {
        if ($c -and (Test-Path -LiteralPath (Join-Path $c $ExeRelative))) { return $c }
    }
    return $null
}

function Find-GameInstalls {
    $found = New-Object System.Collections.Generic.List[string]
    $libs = Get-SteamLibraries (Get-SteamRoots)
    foreach ($lib in $libs) {
        $acf = Join-Path $lib "steamapps\appmanifest_$AppId.acf"
        if (-not (Test-Path -LiteralPath $acf)) { continue }
        $text = [System.IO.File]::ReadAllText($acf)
        $m = [regex]::Match($text, '"installdir"\s+"([^"]+)"')
        if (-not $m.Success) { continue }
        $root = Join-Path $lib ("steamapps\common\" + $m.Groups[1].Value)
        $r = Resolve-GameRoot $root
        if ($r -and -not ($found -contains $r)) { $found.Add($r) }
    }
    return ,$found
}

function Select-GameRoot {
    if ($GamePath) {
        $r = Resolve-GameRoot $GamePath
        if (-not $r) { throw "指定されたフォルダにゲームの実行ファイルが見つかりません: $GamePath" }
        return $r
    }
    $found = Find-GameInstalls
    if ($found.Count -eq 1) { return $found[0] }
    if ($found.Count -gt 1) {
        Write-Info 'ゲームが複数の場所で見つかりました:'
        for ($i = 0; $i -lt $found.Count; $i++) { Write-Info ("  [{0}] {1}" -f ($i + 1), $found[$i]) }
        if ($Yes) { throw '複数見つかったため自動では選べません。-GamePath で指定してください。' }
        $n = Read-Host '番号を入力してください'
        $idx = 0
        if ([int]::TryParse($n, [ref]$idx) -and $idx -ge 1 -and $idx -le $found.Count) { return $found[$idx - 1] }
        throw '選択が不正です。'
    }
    Write-Warn2 'Steam のライブラリからゲームを見つけられませんでした。'
    if ($Yes) { throw '-GamePath でゲームのフォルダを指定してください。' }
    $sel = $null
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = New-Object System.Windows.Forms.FolderBrowserDialog
        $dlg.Description = 'Japanese Ramen Simulator のフォルダを選んでください'
        if ($dlg.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { $sel = $dlg.SelectedPath }
    } catch { }
    if (-not $sel) { $sel = Read-Host 'ゲームのフォルダのパスを入力してください' }
    $r = Resolve-GameRoot $sel
    if (-not $r) { throw "ゲームの実行ファイルが見つかりません: $sel" }
    return $r
}

function Get-UE4SSInfo([string]$win64) {
    $info = [ordered]@{ Installed = $false; Dll = $null; ModsDir = $null; Layout = $null }
    $newDll = Join-Path $win64 'ue4ss\UE4SS.dll'
    $oldDll = Join-Path $win64 'UE4SS.dll'
    if (Test-Path -LiteralPath $newDll) {
        $info.Installed = $true; $info.Dll = $newDll; $info.Layout = 'ue4ss サブフォルダ形式'
        $info.ModsDir = Join-Path $win64 'ue4ss\Mods'
    } elseif (Test-Path -LiteralPath $oldDll) {
        $info.Installed = $true; $info.Dll = $oldDll; $info.Layout = 'Win64 直下形式'
        $info.ModsDir = Join-Path $win64 'Mods'
    }
    return $info
}

# ---------------------------------------------------------------- マニフェスト

function Read-Manifest {
    if (-not (Test-Path -LiteralPath $ManifestPath)) { return $null }
    return (Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json)
}

function Write-Manifest($obj) {
    if (-not (Test-Path -LiteralPath $StateDir)) { New-Item -ItemType Directory -Path $StateDir | Out-Null }
    $json = $obj | ConvertTo-Json -Depth 6
    [System.IO.File]::WriteAllText($ManifestPath, $json, (New-Object System.Text.UTF8Encoding($false)))
}

function Test-UnderPath([string]$child, [string]$parent) {
    $c = [System.IO.Path]::GetFullPath($child).TrimEnd('\') + '\'
    $p = [System.IO.Path]::GetFullPath($parent).TrimEnd('\') + '\'
    return $c.StartsWith($p, [System.StringComparison]::OrdinalIgnoreCase)
}

# ---------------------------------------------------------------- 各モード

function Invoke-Detect {
    $roots = Get-SteamRoots
    Write-Info '== Steam =='
    if ($roots.Count -eq 0) { Write-Warn2 'レジストリに Steam の場所がありません' }
    foreach ($r in $roots) { Write-Info "  $r" }
    Write-Info '== ライブラリ =='
    foreach ($l in (Get-SteamLibraries $roots)) { Write-Info "  $l" }
    Write-Info '== ゲーム =='
    $found = Find-GameInstalls
    if ($found.Count -eq 0) { Write-Warn2 '見つかりません' }
    foreach ($g in $found) {
        $win64 = Join-Path $g 'RealRamenSimulator\Binaries\Win64'
        $u = Get-UE4SSInfo $win64
        Write-Info "  $g"
        if ($u.Installed) { Write-Info "    UE4SS: あり($($u.Layout)) Mods: $($u.ModsDir)" }
        else { Write-Info '    UE4SS: なし' }
    }
    $m = Read-Manifest
    Write-Info '== このMODのインストール記録 =='
    if ($m) { Write-Info "  $($m.installedAt)  $($m.gameRoot)" } else { Write-Info '  なし' }
}

function Invoke-Install {
    $root = Select-GameRoot
    $win64 = Join-Path $root 'RealRamenSimulator\Binaries\Win64'
    Write-Info "ゲーム: $root"

    $prev = Read-Manifest
    if ($prev -and ($prev.gameRoot -ne $root)) {
        Write-Warn2 "別の場所($($prev.gameRoot))にインストール済みの記録があります。先にアンインストールしてください。"
        throw 'インストールを中止しました。'
    }

    $plan = New-Object System.Collections.Generic.List[object]   # @{ Src; Dst }
    $ue4ss = Get-UE4SSInfo $win64
    $ue4ssByUs = $false
    $ue4ssFiles = @()
    if ($prev -and $prev.ue4ssInstalledByUs) { $ue4ssByUs = $true; $ue4ssFiles = @($prev.ue4ssFiles) }

    if (-not $ue4ss.Installed) {
        if (-not (Test-Path -LiteralPath (Join-Path $BundleDir 'ue4ss\UE4SS.dll'))) {
            throw "同梱の UE4SS が見つかりません: $BundleDir(配布物が壊れている可能性があります)"
        }
        foreach ($f in (Get-ChildItem -LiteralPath $BundleDir -Recurse -File)) {
            $rel = $f.FullName.Substring($BundleDir.Length).TrimStart('\')
            $dst = Join-Path $win64 $rel
            if (Test-Path -LiteralPath $dst) { throw "既存のファイルを上書きしてしまうため中止します: $dst" }
            $plan.Add(@{ Src = $f.FullName; Dst = $dst; Kind = 'ue4ss' })
        }
        $ue4ssByUs = $true
        $modsDir = Join-Path $win64 'ue4ss\Mods'
        Write-Info "UE4SS: 同梱の UE4SS を導入します($($plan.Count) ファイル)"
    } else {
        $modsDir = $ue4ss.ModsDir
        Write-Info "UE4SS: 既存のものを使います($($ue4ss.Layout))。上書きしません。"
    }

    # 配布物の mod\ 以下の各フォルダを 1 つの UE4SS MOD として置く(本体 RamenVRM、調査用 RamenVRM_Probe など)
    $modRoot = Join-Path $PackageRoot 'mod'
    $modDirs = @()
    if (Test-Path -LiteralPath $modRoot) { $modDirs = @(Get-ChildItem -LiteralPath $modRoot -Directory) }
    if ($modDirs.Count -eq 0) {
        $msg = "MOD が見つかりません: $modRoot"
        if ($DryRun) { Write-Warn2 $msg } else { throw $msg }
    }
    foreach ($md in $modDirs) {
        $dstMod = Join-Path $modsDir $md.Name
        Write-Info "MOD: $($md.Name)"
        foreach ($f in (Get-ChildItem -LiteralPath $md.FullName -Recurse -File)) {
            $rel = $f.FullName.Substring($md.FullName.Length).TrimStart('\')
            $plan.Add(@{ Src = $f.FullName; Dst = (Join-Path $dstMod $rel); Kind = 'mod' })
        }
        $enabled = Join-Path $dstMod 'enabled.txt'
        if (-not (@($plan | Where-Object { $_.Dst -eq $enabled }).Count)) { $plan.Add(@{ Text = ''; Dst = $enabled; Kind = 'mod' }) }
    }

    # 配布物の paks\(専用マテリアル)をゲームの Content\Paks\~mods に置く
    $pakSrc = Join-Path $PackageRoot 'paks'
    if (Test-Path -LiteralPath $pakSrc) {
        $pakDst = Join-Path $root 'RealRamenSimulator\Content\Paks\~mods'
        foreach ($f in (Get-ChildItem -LiteralPath $pakSrc -File)) {
            $plan.Add(@{ Src = $f.FullName; Dst = (Join-Path $pakDst $f.Name); Kind = 'mod' })
        }
        Write-Info "専用マテリアル: $pakDst"
    }

    # 既存ファイルの確認: 前回このMODが置いたもの以外は上書きしない
    $prevFiles = @()
    if ($prev) { $prevFiles = @($prev.files) }
    $planDsts = @($plan | ForEach-Object { $_.Dst })
    $stale = @($prevFiles | Where-Object { $_ -and -not ($planDsts -contains $_) -and (Test-Path -LiteralPath $_) -and (Test-UnderPath $_ $root) })
    foreach ($p in $plan) {
        if ((Test-Path -LiteralPath $p.Dst) -and -not ($prevFiles -contains $p.Dst) -and -not ($p.Kind -eq 'ue4ss')) {
            throw "このMODが置いたものではないファイルが既にあるため中止します: $($p.Dst)"
        }
    }

    Write-Info ''
    Write-Info '== 以下を行います =='
    foreach ($p in $plan) { Write-Info "  書き込み: $($p.Dst)" }
    foreach ($t in $stale) { Write-Info "  削除    : $t(前回のインストールにあって今回の配布物に無いもの)" }
    Write-Info "  作成    : $PackageModels(VRM を置くフォルダ。この配布物の install.bat の隣)"
    Write-Info "  記録    : $ModelsLocation(上のフォルダの場所。MOD が読む)"
    Write-Info "  記録    : $ManifestPath"
    if ($DryRun) { Write-Info ''; Write-Info '(DryRun のため何も書き込みませんでした)'; return }
    if (-not (Confirm-Action '実行しますか?')) { Write-Info '中止しました。'; return }

    $written = New-Object System.Collections.Generic.List[string]
    $createdDirs = New-Object System.Collections.Generic.List[string]
    if ($prev) { foreach ($d in @($prev.createdDirs)) { $createdDirs.Add($d) } }
    function Ensure-Dir([string]$d) {
        $stack = @()
        $cur = $d
        while ($cur -and -not (Test-Path -LiteralPath $cur)) { $stack = @($cur) + $stack; $cur = Split-Path $cur -Parent }
        foreach ($s in $stack) { New-Item -ItemType Directory -Path $s | Out-Null; if (-not ($createdDirs -contains $s)) { $createdDirs.Add($s) } }
    }

    foreach ($t in $stale) { Remove-Item -LiteralPath $t -Force }
    foreach ($p in $plan) {
        Ensure-Dir (Split-Path $p.Dst -Parent)
        if ($p.ContainsKey('Src')) { Copy-Item -LiteralPath $p.Src -Destination $p.Dst -Force }
        else { [System.IO.File]::WriteAllText($p.Dst, $p.Text) }
        if ($p.Kind -eq 'ue4ss') { $ue4ssFiles += $p.Dst } else { $written.Add($p.Dst) }
    }

    foreach ($d in @("$UserDataDir", "$UserDataDir\models", $PackageModels, $StateDir)) {
        if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d | Out-Null }
    }
    # MOD は、ここに書いたフォルダの VRM を読む(配布物を移動したら install.bat を実行し直す)
    [System.IO.File]::WriteAllText($ModelsLocation, $PackageModels, (New-Object System.Text.UTF8Encoding($false)))

    Write-Manifest ([ordered]@{
        version = 1
        installedAt = (Get-Date).ToString('s')
        gameRoot = $root
        win64 = $win64
        modsDir = $modsDir
        files = @($written)
        createdDirs = @($createdDirs)
        ue4ssInstalledByUs = $ue4ssByUs
        ue4ssFiles = @($ue4ssFiles)
        userDataDir = $UserDataDir
    })
    Write-Info ''
    Write-Info 'インストールしました。'
    Write-Info "VRM ファイルを次のフォルダに置いてください(ゲーム中に足しても F5 / F6 で選べます):"
    Write-Info "  $PackageModels"
}

function Invoke-Uninstall {
    $m = Read-Manifest
    if (-not $m) {
        Write-Warn2 "インストール記録がありません($ManifestPath)。このMODが置いたファイルを特定できないため、何もしません。"
        Write-Info '  手動で削除する場合は docs\INSTALL.md の「手動アンインストール」を参照してください。'
        return
    }
    $targets = @($m.files)
    if ($m.ue4ssInstalledByUs) { $targets += @($m.ue4ssFiles) }
    # 記録されたゲームフォルダの外は絶対に消さない
    $targets = @($targets | Where-Object { $_ -and (Test-UnderPath $_ $m.gameRoot) })
    # UE4SS を自分で入れた場合、自分が作った ue4ss フォルダはログ・ダンプごと消す
    $ue4ssDir = Join-Path $m.win64 'ue4ss'
    $removeUe4ssDir = $m.ue4ssInstalledByUs -and (@($m.createdDirs) -contains $ue4ssDir) -and (Test-Path -LiteralPath $ue4ssDir)

    Write-Info "ゲーム: $($m.gameRoot)"
    Write-Info '== 以下を削除します =='
    foreach ($t in $targets) { if (Test-Path -LiteralPath $t) { Write-Info "  削除: $t" } }
    if ($removeUe4ssDir) { Write-Info "  削除: $ue4ssDir(フォルダごと。UE4SS が作ったログ・ダンプを含む)" }
    if ($m.ue4ssInstalledByUs) { Write-Info '  (UE4SS はこのインストーラが導入したものなので削除します)' }
    else { Write-Info '  (UE4SS は元から入っていたので残します)' }

    $removeUser = $RemoveUserData
    if (-not $removeUser -and -not $Yes -and -not $DryRun -and (Test-Path -LiteralPath $m.userDataDir)) {
        $removeUser = Confirm-Action "ユーザーデータ($($m.userDataDir):VRM・設定)と キャッシュ($StateDir)も削除しますか?"
    }
    if ($removeUser) { Write-Info "  削除: $($m.userDataDir)(ユーザーデータ)"; Write-Info "  削除: $StateDir(キャッシュ)" }

    if ($DryRun) { Write-Info ''; Write-Info '(DryRun のため何も削除しませんでした)'; return }
    if (-not (Confirm-Action '実行しますか?')) { Write-Info '中止しました。'; return }

    foreach ($t in $targets) { if (Test-Path -LiteralPath $t) { Remove-Item -LiteralPath $t -Force } }
    if (Test-Path -LiteralPath $ModelsLocation) { Remove-Item -LiteralPath $ModelsLocation -Force }
    if ($removeUe4ssDir) { Remove-Item -LiteralPath $ue4ssDir -Recurse -Force }
    # 作ったフォルダは空なら消す(深いものから)
    foreach ($d in (@($m.createdDirs) | Sort-Object { $_.Length } -Descending)) {
        if ($d -and (Test-UnderPath $d $m.gameRoot) -and (Test-Path -LiteralPath $d) -and
            -not (Get-ChildItem -LiteralPath $d -Force | Select-Object -First 1)) {
            Remove-Item -LiteralPath $d -Force
        }
    }
    if ($removeUser) {
        if (Test-Path -LiteralPath $m.userDataDir) { Remove-Item -LiteralPath $m.userDataDir -Recurse -Force }
        if (Test-Path -LiteralPath $StateDir) { Remove-Item -LiteralPath $StateDir -Recurse -Force }
    } else {
        Remove-Item -LiteralPath $ManifestPath -Force
    }
    Write-Info 'アンインストールしました。'
}

try {
    switch ($Mode) {
        'Detect' { Invoke-Detect }
        'Install' { Invoke-Install }
        'Uninstall' { Invoke-Uninstall }
    }
    exit 0
} catch {
    Write-Err2 $_.Exception.Message
    exit 1
}
