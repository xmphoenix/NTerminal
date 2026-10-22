# ============================================================================
# build_libssh.ps1 - 为 NTerminal 构建 Windows/MinGW 版 libssh
#
# 产物布局（NTerminal.pro 依赖此约定）：
#   openssl-deps/   OpenSSL + pthread 运行时（libssh 的编译依赖）
#   libssh-src/     libssh 源码（官方 tarball）
#   libssh-build/   CMake 构建目录（临时，-Clean 可清）
#   libssh/         安装产物：include/ + lib/libssh.dll.a + bin/*.dll（含运行时闭包）
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File build_libssh.ps1
#   powershell -ExecutionPolicy Bypass -File build_libssh.ps1 -Proxy http://127.0.0.1:10809
#   powershell -ExecutionPolicy Bypass -File build_libssh.ps1 -Clean
#
# 说明：
#   - OpenSSL 使用 MSYS2 官方 MinGW64 预编译包（C ABI 与 Qt MinGW 兼容），
#     不从源码编译（约需 30-60 分钟）。
#   - libssh 以 WITH_ZLIB=OFF 构建：zlib1.dll 不在运行时闭包内，
#     且 SSH 压缩在现代服务器上默认关闭，禁用可避免依赖问题。
# ============================================================================

param(
    [ValidateSet("Release", "Debug")]
    [string]$BuildType = "Release",

    [int]$ParallelJobs = 8,

    # HTTP(S) 代理（如 http://127.0.0.1:10809），空则直连
    [string]$Proxy = "",

    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$OpenSSLDir   = Join-Path $ScriptDir "openssl-deps"
$SrcDir       = Join-Path $ScriptDir "libssh-src"
$BuildDir     = Join-Path $ScriptDir "libssh-build"
$InstallDir   = Join-Path $ScriptDir "libssh"

# ---- 资源版本（2026-10 验证可用） ----
$LibsshVersion  = "0.10.6"
$LibsshTarball  = "libssh-$LibsshVersion.tar.xz"
$LibsshUrl      = "https://www.libssh.org/files/0.10/$LibsshTarball"
$MsysRepo       = "https://repo.msys2.org/mingw/mingw64"
$MsysPackages   = @(
    "mingw-w64-x86_64-openssl-3.6.5-1-any.pkg.tar.zst",
    "mingw-w64-x86_64-libwinpthread-12.0.0.r747.g1a99f8514-1-any.pkg.tar.zst"
)

function Write-Log {
    param([string]$Message, [string]$Level = "Info")
    $ts = Get-Date -Format "HH:mm:ss"
    $color = switch ($Level) {
        "Success" { "Green" } "Warning" { "Yellow" } "Error" { "Red" } default { "White" }
    }
    Write-Host "[$ts] $Message" -ForegroundColor $color
}

function Invoke-Tool {
    param([string]$Exe, [string[]]$Arguments, [string]$Cwd = "")
    $prev = $ErrorActionPreference; $ErrorActionPreference = "Continue"
    try {
        if ($Cwd) { Push-Location $Cwd }
        & $Exe @Arguments 2>&1 | ForEach-Object { Write-Host "  $_" }
        return $LASTEXITCODE
    } finally {
        if ($Cwd) { Pop-Location }
        $ErrorActionPreference = $prev
    }
}

function Find-QtTool {
    param([string]$Name)
    foreach ($root in @("C:\Qt\Tools", $env:QT_TOOLS_PATH)) {
        if (-not $root -or -not (Test-Path $root)) { continue }
        $hit = Get-ChildItem -Path $root -Recurse -Filter $Name -ErrorAction SilentlyContinue |
               Where-Object { $_.FullName -match "CMake_64|mingw\d+_\d+\\bin" } |
               Sort-Object FullName -Descending | Select-Object -First 1
        if ($hit) { return $hit.FullName }
    }
    if (Get-Command $Name -ErrorAction SilentlyContinue) { return $Name }
    return $null
}

# ---- 下载（curl.exe，Windows 自带，支持 -x 代理） ----
function Download {
    param([string]$Url, [string]$Out)
    $args = @("-sL", "--retry", "2", "--max-time", "600")
    if ($Proxy) { $args += @("-x", $Proxy) }
    & curl.exe @args "-o" $Out $Url
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $Out)) {
        throw "下载失败: $Url"
    }
}

# ============================================================================
# 0. 幂等检查
# ============================================================================
if ($Clean) {
    Write-Log "清理构建目录..." "Warning"
    Remove-Item $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
}

if ((Test-Path "$InstallDir\include\libssh\libssh.h") -and
    (Test-Path "$InstallDir\lib\libssh.dll.a") -and
    (Test-Path "$InstallDir\bin\libssh.dll")) {
    Write-Log "libssh 已安装于 $InstallDir，跳过构建（-Clean 后重跑可强制重建）。" "Success"
    exit 0
}

# ============================================================================
# 1. 工具链检查
# ============================================================================
$CMake = Find-QtTool "cmake.exe"
$Make  = Find-QtTool "mingw32-make.exe"
if (-not $CMake -or -not $Make) {
    Write-Log "未找到 cmake.exe / mingw32-make.exe（Qt Tools）。" "Error"
    exit 1
}
$MingwBin = Split-Path -Parent $Make
$env:PATH = "$MingwBin;$env:PATH"
Write-Log "CMake: $CMake"
Write-Log "Make : $Make"

# ============================================================================
# 2. OpenSSL / pthread 运行时（MSYS2 预编译包）
# ============================================================================
if (-not (Test-Path "$OpenSSLDir\lib\libcrypto.dll.a")) {
    Write-Log "准备 OpenSSL 运行时（MSYS2 预编译包）..." "Header"
    $tmp = Join-Path $ScriptDir "deps-tmp"
    New-Item -ItemType Directory $tmp -Force | Out-Null
    foreach ($pkg in $MsysPackages) {
        $file = Join-Path $tmp $pkg
        if (-not (Test-Path $file)) {
            Write-Log "下载 $pkg ..."
            Download -Url "$MsysRepo/$pkg" -Out $file
        }
        # bsdtar（Windows 10 1803+ 自带）支持 zstd 解压
        tar -xf $file -C $tmp
        if ($LASTEXITCODE -ne 0) { Write-Log "解压失败: $pkg" "Error"; exit 1 }
    }
    foreach ($sub in @("include", "lib", "bin")) {
        $src = Join-Path $tmp "mingw64\$sub"
        $dst = Join-Path $OpenSSLDir $sub
        New-Item -ItemType Directory $dst -Force | Out-Null
        Copy-Item "$src\*" $dst -Recurse -Force
    }
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
    if (-not (Test-Path "$OpenSSLDir\lib\libcrypto.dll.a")) {
        Write-Log "OpenSSL 布置失败。" "Error"; exit 1
    }
    Write-Log "OpenSSL 运行时就绪。" "Success"
} else {
    Write-Log "OpenSSL 运行时已就绪，跳过。" "Success"
}

# ============================================================================
# 3. libssh 源码（官方 tarball，约 0.5MB；失败时回退 git clone）
# ============================================================================
if (-not (Test-Path "$SrcDir\CMakeLists.txt")) {
    Write-Log "获取 libssh $LibsshVersion 源码..." "Header"
    $tarball = Join-Path $ScriptDir $LibsshTarball
    try {
        Download -Url $LibsshUrl -Out $tarball
        tar -xf $tarball -C $ScriptDir
        Rename-Item (Join-Path $ScriptDir "libssh-$LibsshVersion") $SrcDir -ErrorAction SilentlyContinue
        Remove-Item $tarball -Force
    } catch {
        Write-Log "tarball 下载失败（$($_.Exception.Message)），回退 git clone..." "Warning"
        $gitArgs = @("clone", "--depth", "1", "--branch", "libssh-$LibsshVersion",
                     "https://git.libssh.org/projects/libssh.git", $SrcDir)
        if ($Proxy) { $gitArgs = @("-c", "http.proxy=$Proxy") + $gitArgs }
        $r = Invoke-Tool -Exe "git" -Arguments $gitArgs
        if ($r -ne 0) { Write-Log "git clone 失败。" "Error"; exit 1 }
    }
    if (-not (Test-Path "$SrcDir\CMakeLists.txt")) {
        Write-Log "libssh 源码就位失败。" "Error"; exit 1
    }
    Write-Log "libssh 源码就绪。" "Success"
} else {
    Write-Log "libssh 源码已存在，跳过。" "Success"
}

# ============================================================================
# 4. CMake 配置 + 编译 + 安装
# ============================================================================
Write-Log "配置并编译 libssh（$BuildType, -j$ParallelJobs）..." "Header"
New-Item -ItemType Directory $BuildDir -Force | Out-Null

$cmakeArgs = @(
    "-S", $SrcDir, "-B", $BuildDir,
    "-G", "MinGW Makefiles",
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DCMAKE_INSTALL_PREFIX=$InstallDir",
    "-DCMAKE_MAKE_PROGRAM=$Make",
    "-DOPENSSL_ROOT_DIR=$OpenSSLDir",
    "-DOPENSSL_CRYPTO_LIBRARY=$OpenSSLDir\lib\libcrypto.dll.a",
    "-DOPENSSL_SSL_LIBRARY=$OpenSSLDir\lib\libssl.dll.a",
    "-DWITH_ZLIB=OFF",          # zlib1.dll 不在运行时闭包，禁用压缩
    "-DWITH_SERVER=OFF",        # 仅客户端
    "-DWITH_EXAMPLES=OFF",
    "-DWITH_GSSAPI=OFF",
    "-DWITH_NACL=OFF",
    "-DWITH_PCAP=OFF",
    "-DBUILD_SHARED_LIBS=ON",
    "-Wno-dev"
) | ForEach-Object { $_ -replace '\\', '/' }

if ((Invoke-Tool -Exe $CMake -Arguments $cmakeArgs) -ne 0) {
    Write-Log "CMake 配置失败。" "Error"; exit 1
}
if ((Invoke-Tool -Exe $Make -Arguments @("-j", $ParallelJobs) -Cwd $BuildDir) -ne 0) {
    Write-Log "编译失败。" "Error"; exit 1
}
if ((Invoke-Tool -Exe $CMake -Arguments @("--install", $BuildDir)) -ne 0) {
    Write-Log "安装失败。" "Error"; exit 1
}

# ============================================================================
# 5. 合并运行时闭包到 libssh/bin（NTerminal.pro 的 QMAKE_POST_LINK 依赖此约定）
# ============================================================================
Write-Log "合并运行时 DLL 到 libssh/bin ..." "Info"
$binDir = Join-Path $InstallDir "bin"
Copy-Item "$OpenSSLDir\bin\*.dll" $binDir -Force
$libgcc = Join-Path $MingwBin "libgcc_s_seh-1.dll"   # libssh.dll 的 GCC 运行时依赖
if (Test-Path $libgcc) { Copy-Item $libgcc $binDir -Force }

# ============================================================================
# 6. 完成
# ============================================================================
Write-Log "完成。libssh 安装于 $InstallDir" "Success"
Write-Host ""
Write-Host "libssh/bin:" -ForegroundColor Yellow
Get-ChildItem $binDir -Filter "*.dll" | ForEach-Object { Write-Host "  $($_.Name)" }
Write-Host ""
Write-Host "现在可以构建 NTerminal：qmake NTerminal.pro && mingw32-make" -ForegroundColor Cyan
