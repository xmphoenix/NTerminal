# ============================================================================
# 打包 NTerminal 自动更新包并生成 update.json
#
# 用法示例（在仓库根目录）：
#   powershell -ExecutionPolicy Bypass -File tools\package_update.ps1 `
#       -Version 2.1.0 -BaseUrl https://example.com/nterminal `
#       -Notes "修复若干问题" -DeployQt -QtBin C:\Qt\6.10.2\mingw_64\bin `
#       -Minimal
#
# -Minimal 只部署程序实际用到的 Qt 库、插件和中文翻译，并除去未被引用的
# OpenSSL libssl；压缩时优先使用 Python zlib level 9，未安装 Python 时
# 回退到 Compress-Archive。可显著减小发布包体积。
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
    [switch]$Minimal,
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

# 优先使用 Python 的 zlib level 9 压缩（比 Compress-Archive 略小）；
# 未安装 Python 时回退到 Compress-Archive，保持脚本零依赖可用。
function New-SmallestZip([string]$SourceDir, [string]$Destination) {
    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($python) {
        $pythonScript = @'
import os
import sys
import zipfile

source, destination = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(destination, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
    for root, dirs, files in os.walk(source):
        dirs.sort()
        for name in sorted(files):
            path = os.path.join(root, name)
            archive.write(path, os.path.relpath(path, source))
'@
        $scriptPath = Join-Path ([IO.Path]::GetTempPath()) `
            ("nt-zip-" + [guid]::NewGuid().ToString("N") + ".py")
        [IO.File]::WriteAllText($scriptPath, $pythonScript,
            (New-Object System.Text.UTF8Encoding($false)))
        try {
            & $python.Source -I $scriptPath $SourceDir $Destination
            if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $Destination)) {
                return
            }
            Write-Warning "Python zip failed with exit code $LASTEXITCODE."
        } catch {
            Write-Warning "Python zip failed: $($_.Exception.Message)"
        } finally {
            Remove-Item -LiteralPath $scriptPath -Force -ErrorAction SilentlyContinue
        }
        Remove-Item -LiteralPath $Destination -Force -ErrorAction SilentlyContinue
        Write-Warning 'Falling back to Compress-Archive.'
    }
    Compress-Archive -Path (Join-Path $SourceDir '*') -DestinationPath $Destination -Force
}

if ($Minimal -and -not $DeployQt) {
    throw '-Minimal requires -DeployQt.'
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
    -not ($Minimal -and $_.Name -in @('plugins', 'libssl-3-x64.dll')) -and
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
    $deployArgs = @('--release')
    if ($Minimal) {
        # 不部署 windeployqt 默认的全套插件/翻译；Qt Quick、软件 OpenGL、
        # D3D/DXC 编译器、FFmpeg 都与本程序无关。
        $deployArgs += @(
            '--no-plugins',
            '--no-translations',
            '--no-opengl-sw',
            '--no-system-d3d-compiler',
            '--no-system-dxc-compiler',
            '--no-ffmpeg',
            '--compiler-runtime'
        )
    }
    $deployArgs += (Join-Path $stage 'NTerminal.exe')
    & $windeployqt @deployArgs
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed with exit code $LASTEXITCODE"
    }

    if ($Minimal) {
        Write-Step 'Deploying minimal Qt plugins and translations'
        $qtRoot = Split-Path -Parent (Split-Path -Parent $windeployqt)
        $qtPlugins = Join-Path $qtRoot 'plugins'

        # 程序必需：平台插件、SVG 图标引擎、SVG 图片格式、TLS（更新走 HTTPS）。
        # 可选：Windows 11 风格、网络状态后端；缺失时 Qt 会自行回退。
        $pluginSet = @(
            @{ Type = 'platforms';          Name = 'qwindows.dll';             Required = $true },
            @{ Type = 'iconengines';        Name = 'qsvgicon.dll';             Required = $true },
            @{ Type = 'imageformats';       Name = 'qsvg.dll';                 Required = $true },
            @{ Type = 'tls';                Name = 'qschannelbackend.dll';     Required = $true },
            @{ Type = 'styles';             Name = 'qmodernwindowsstyle.dll';  Required = $false },
            @{ Type = 'networkinformation'; Name = 'qnetworklistmanager.dll';  Required = $false }
        )
        foreach ($plugin in $pluginSet) {
            $source = Join-Path $qtPlugins (Join-Path $plugin.Type $plugin.Name)
            if (-not (Test-Path -LiteralPath $source)) {
                if ($plugin.Required) {
                    throw "Required Qt plugin not found: $source"
                }
                Write-Warning "Skipping optional Qt plugin: $source"
                continue
            }
            $targetDir = Join-Path $stage $plugin.Type
            New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
            Copy-Item -LiteralPath $source -Destination $targetDir -Force
        }

        # 中文界面依赖 Qt 基础控件翻译；只带 qtbase，不带全部语言和模块。
        $translation = Join-Path $qtRoot 'translations\qtbase_zh_CN.qm'
        if (Test-Path -LiteralPath $translation) {
            $targetDir = Join-Path $stage 'translations'
            New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
            Copy-Item -LiteralPath $translation -Destination $targetDir -Force
        } else {
            Write-Warning "Qt Chinese translation not found: $translation"
        }

        # libssl 未被程序或 libssh 引用（libssh 只用 libcrypto），不必随包分发。
        Remove-Item -LiteralPath (Join-Path $stage 'libssl-3-x64.dll') `
            -Force -ErrorAction SilentlyContinue
    }
}

$stagedBytes = (Get-ChildItem -LiteralPath $stage -Recurse -File |
    Measure-Object -Property Length -Sum).Sum
Write-Host ("Staged payload: {0:N0} bytes ({1:N2} MB)" -f $stagedBytes, ($stagedBytes / 1MB))

$zipName = "NTerminal-$Version-win64.zip"
$zipPath = Join-Path $OutDir $zipName
Write-Step "Creating $zipName"
New-SmallestZip $stage $zipPath

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
