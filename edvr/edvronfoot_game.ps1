# EDVRonFOOT: поставить нашу сборку EDVR в игру или вернуть официальную.
#
#   powershell -ExecutionPolicy Bypass -File edvronfoot_game.ps1 -Action Install
#   powershell -ExecutionPolicy Bypass -File edvronfoot_game.ps1 -Action Restore
#   ... -Action Status                  (что сейчас стоит, ничего не меняет)
#   ... -GameDir "<папка игры>"         (если игра не в C:\Steam\...)
#
# Install: при первом запуске сохраняет ОФИЦИАЛЬНЫЕ файлы EDVR в
#   <папка игры>\edvr_backup_edvronfoot\official-v0.19.1\ вместе со списком SHA-256
#   (manifest.txt), затем ставит нашу сборку штатным tools\install_edvr.py
#   (он сам отказывается при запущенной игре, сверяет хеши, не трогает edvr.ini).
# Restore: возвращает файлы из слепка и сверяет хеши. Файлы, которых в слепке не было
#   (например, квитанция нашей установки), не удаляются, а переносятся в
#   edvr_backup_edvronfoot\removed-<дата>\.
# edvr.ini не трогается никогда.

param(
    [Parameter(Mandatory = $true)][ValidateSet('Install', 'Restore', 'Status')][string]$Action,
    [string]$GameDir = 'C:\Steam\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64'
)
$ErrorActionPreference = 'Stop'

$Repo = Split-Path -Parent $MyInvocation.MyCommand.Path
$Files = @(
    'd3d11.dll', 'nvngx_dlss.dll', 'edvr_profile.ini', 'edvr_native_receipt.json',
    'Openvr\win64\openvr_api.dll', 'Openvr\win64\openxr_loader.dll',
    'Openvr\win64\OPENXR-LOADER-LICENSE.txt', 'Openvr\win64\edvr_openxr.ini'
)
$Snapshot = Join-Path $GameDir 'edvr_backup_edvronfoot\official-v0.19.1'
$Manifest = Join-Path $Snapshot 'manifest.txt'

function Say([string]$text) { Write-Host "[edvronfoot] $text" }
function Fail([string]$text) { Write-Host "[edvronfoot] ОШИБКА: $text" -ForegroundColor Red; exit 1 }
function Hash([string]$path) { (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash }
function Version([string]$path) {
    if (Test-Path -LiteralPath $path) { (Get-Item -LiteralPath $path).VersionInfo.ProductVersion } else { '(нет файла)' }
}

if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'EliteDangerous64.exe'))) { Fail "в '$GameDir' нет EliteDangerous64.exe; укажи -GameDir" }

$graphics = Join-Path $GameDir 'd3d11.dll'
Say "папка игры: $GameDir"
Say "сейчас стоит d3d11.dll версии $(Version $graphics)"
if ($Action -eq 'Status') {
    if (Test-Path -LiteralPath $Manifest) { Say "слепок официальной версии есть: $Snapshot" } else { Say 'слепка официальной версии ещё нет' }
    exit 0
}

if (Get-Process -Name EliteDangerous64 -ErrorAction SilentlyContinue) { Fail 'игра запущена -- закрой её и повтори' }

if ($Action -eq 'Install') {
    if (-not (Test-Path -LiteralPath $Manifest)) {
        $version = Version $graphics
        if ($version -notmatch '^v0\.19\.1$') { Fail "слепок официальной версии делается только с официального v0.19.1, а стоит '$version'" }
        New-Item -ItemType Directory -Force -Path $Snapshot | Out-Null
        $lines = @()
        foreach ($f in $Files) {
            $src = Join-Path $GameDir $f
            if (-not (Test-Path -LiteralPath $src)) { continue }
            $dst = Join-Path $Snapshot $f
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
            Copy-Item -LiteralPath $src -Destination $dst
            if ((Hash $src) -ne (Hash $dst)) { Fail "копия $f не совпала с оригиналом" }
            $lines += "$(Hash $src)  $f"
        }
        [IO.File]::WriteAllLines($Manifest, $lines, (New-Object Text.UTF8Encoding $false))
        Say "сохранён слепок официальной версии ($($lines.Count) файлов): $Snapshot"
    } else {
        Say "слепок официальной версии уже есть: $Snapshot"
    }
    $python = Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe'
    if (-not (Test-Path -LiteralPath $python)) { $python = 'python' }
    & $python (Join-Path $Repo 'tools\install_edvr.py') --target $GameDir --all --tag edvronfoot
    if ($LASTEXITCODE -ne 0) { Fail "install_edvr.py завершился с кодом $LASTEXITCODE -- смотри строки выше" }
    Say "готово: стоит d3d11.dll версии $(Version $graphics)"
    exit 0
}

# Restore
if (-not (Test-Path -LiteralPath $Manifest)) { Fail "слепка официальной версии нет ($Snapshot) -- откатывать не к чему" }
$known = @{}
foreach ($line in [IO.File]::ReadAllLines($Manifest)) {
    $hash, $f = $line -split '  ', 2
    $known[$f] = $hash
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
foreach ($f in $Files) {
    $live = Join-Path $GameDir $f
    if ($known.ContainsKey($f)) {
        Copy-Item -LiteralPath (Join-Path $Snapshot $f) -Destination $live -Force
        if ((Hash $live) -ne $known[$f]) { Fail "после отката $f не совпал с официальным" }
    } elseif (Test-Path -LiteralPath $live) {
        $aside = Join-Path $GameDir "edvr_backup_edvronfoot\removed-$stamp\$f"
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $aside) | Out-Null
        Move-Item -LiteralPath $live -Destination $aside
        Say "$f не было в официальной версии -- перенесён в $aside"
    }
}
Say "откат выполнен, хеши совпали; стоит d3d11.dll версии $(Version $graphics)"
exit 0
