# ============================================================================
# 打包 NTerminal 自动更新包并生成 update.json
#
# 用法示例（在仓库根目录）：
#   powershell -ExecutionPolicy Bypass -File tools\package_update.ps1 `
#       -Version 2.1.0 -BaseUrl https://example.com/nterminal `
#       -Notes "修复若干问题" -DeployQt -QtBin C:\Qt\6.10.2\mingw_64\bin
#
# 产物：
#   dist\NTerminal-2.1.0-win64.zip
#   dist\update.json
#
# 把两个文件上传到静态 HTTPS 站点（或 GitHub Release），然后把
# update.json 的地址填到 NTerminal「设置 -> 更新设置」即可。
# ============================================================================

param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$BinDir = '',
    [string]$OutDir = '',
    [string]$BaseUrl = '',
    [string]$Platform = 'windows-x86_64',
    [string]$Notes = '',
    [switch]$DeployQt,
    [string]$QtBin = ''
)

$ErrorActionPreference = 'Stop'

$scriptRoot = $PSScriptRoot
if ([string]::IsNullOrEmpty($scriptRoot)) {
    $scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
}
if ([string]::IsNullOrEmpty($BinDir)) {
    $BinDir = Join-Path $scriptRoot '..\bin'
}
if ([string]::IsNullOrEmpty($OutDir)) {
    $OutDir = Join-Path $scriptRoot '..\dist'
}

function Write-Step([string]$Message) {
    Write-Host "==> $Message" -ForegroundColor Cyan
}

if (-not (Test-Path -LiteralPath $BinDir)) {
    throw "BinDir not found: $BinDir"
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stage = Join-Path $OutDir ("stage-" + $Version)
if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Write-Step "Copying runtime files from $BinDir"
# 只打包程序文件；排除调试目录、日志和接收到的临时文件，避免把用户数据带进更新包
$items = Get-ChildItem -LiteralPath $BinDir -Force | Where-Object {
    $_.Name -ne 'debug' -and
    $_.Name -notlike 'received.*' -and
    $_.Name -notlike 'NTerminal-update*' -and
    $_.Extension -notin @('.log', '.png', '.pdb')
}
foreach ($item in $items) {
    Copy-Item -LiteralPath $item.FullName -Destination $stage -Recurse -Force
}

if ($DeployQt) {
    Write-Step 'Deploying Qt runtime'
    $windeployqt = ''
    if ($QtBin) {
        $candidate = Join-Path $QtBin 'windeployqt.exe'
        if (Test-Path -LiteralPath $candidate) {
            $windeployqt = $candidate
        }
    }
    if (-not $windeployqt) {
        $command = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
        if ($command) {
            $windeployqt = $command.Source
        }
    }
    if (-not $windeployqt) {
        throw 'windeployqt.exe not found. Pass -QtBin or add Qt bin to PATH.'
    }
    & $windeployqt --release (Join-Path $stage 'NTerminal.exe')
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed with exit code $LASTEXITCODE"
    }
}

$zipName = "NTerminal-$Version-win64.zip"
$zipPath = Join-Path $OutDir $zipName
Write-Step "Creating $zipName"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zipPath -Force

$hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLower()
$size = (Get-Item -LiteralPath $zipPath).Length

$url = $zipName
if ($BaseUrl) {
    $url = $BaseUrl.TrimEnd('/') + '/' + $zipName
}

$manifest = [ordered]@{
    version   = $Version
    published = (Get-Date).ToUniversalTime().ToString('o')
    notes     = $Notes
    platforms = [ordered]@{
        $Platform = [ordered]@{
            url    = $url
            sha256 = $hash
            size   = $size
        }
    }
}

$manifestPath = Join-Path $OutDir 'update.json'
$json = $manifest | ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText(
    $manifestPath,
    $json,
    (New-Object System.Text.UTF8Encoding($false))
)

Remove-Item -LiteralPath $stage -Recurse -Force

Write-Step 'Done'
Write-Host "Package : $zipPath"
Write-Host "Manifest: $manifestPath"
Write-Host "SHA-256 : $hash"
Write-Host "Size    : $size bytes"
Write-Host ''
Write-Host 'Upload both files to a static HTTPS location, then set the update.json URL in NTerminal.'
