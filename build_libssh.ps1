# libssh Build Script for Windows
# Automatically downloads prebuilt OpenSSL/zlib if not present

param(
    [ValidateSet("Release", "Debug")]
    [string]$BuildType = "Release",

    [int]$ParallelJobs = 8,

    [switch]$Clean,
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Find-QtTool {
    param([string]$ToolName)

    if (Get-Command $ToolName -ErrorAction SilentlyContinue) {
        return $ToolName
    }

    $QtToolsPaths = @(
        "C:\Qt\Tools\CMake_64\bin",
        "C:\Qt\Tools\mingw1310_64\bin",
        "C:\Qt\Tools\mingw1230_64\bin",
        "C:\Qt\Tools\mingw1120_64\bin",
        "${env:QT_TOOLS_PATH}\CMake_64\bin",
        "${env:QT_TOOLS_PATH}\mingw1310_64\bin"
    )

    foreach ($Path in $QtToolsPaths) {
        $ToolPath = Join-Path $Path "$ToolName.exe"
        if (Test-Path $ToolPath) {
            return $ToolPath
        }
    }

    return $null
}

function Write-Log {
    param(
        [string]$Message,
        [ValidateSet("Info", "Warning", "Error", "Success", "Header")]
        [string]$Level = "Info"
    )

    $Timestamp = Get-Date -Format "HH:mm:ss"

    switch ($Level) {
        "Info"    { Write-Host "[$Timestamp] $Message" -ForegroundColor White }
        "Warning" { Write-Host "[$Timestamp] WARNING: $Message" -ForegroundColor Yellow }
        "Error"   { Write-Host "[$Timestamp] ERROR: $Message" -ForegroundColor Red }
        "Success" { Write-Host "[$Timestamp] SUCCESS: $Message" -ForegroundColor Green }
        "Header"  { Write-Host ""; Write-Host ("=" * 60) -ForegroundColor Cyan; Write-Host "  $Message" -ForegroundColor Cyan; Write-Host ("=" * 60) -ForegroundColor Cyan; Write-Host "" }
    }
}

function Invoke-ExternalCommand {
    param(
        [string]$Command,
        [array]$Arguments,
        [string]$WorkingDirectory = "",
        [switch]$ShowOutput
    )

    $PrevErrorAction = $ErrorActionPreference
    $ErrorActionPreference = "Continue"

    if ($WorkingDirectory) {
        Push-Location $WorkingDirectory
    }

    try {
        if ($ShowOutput -or $Verbose) {
            & $Command $Arguments 2>&1 | ForEach-Object { Write-Host $_ }
        } else {
            & $Command $Arguments 2>&1 | Out-Null
        }
        $Result = $LASTEXITCODE
    } finally {
        if ($WorkingDirectory) {
            Pop-Location
        }
        $ErrorActionPreference = $PrevErrorAction
    }

    return $Result
}

function Download-PrebuiltDependencies {
    param([string]$TargetDir)

    Write-Log "Downloading prebuilt OpenSSL and zlib..." -Level Info

    # Use MSYS2 packages as source (MinGW64 binaries)
    # OpenSSL and zlib from MSYS2 repo
    $MsysRepo = "https://repo.msys2.org/mingw/mingw64"
    $Packages = @(
        "mingw-w64-x86_64-openssl-3.4.0-1-any.pkg.tar.zst",
        "mingw-w64-x86_64-zlib-1.3.1-1-any.pkg.tar.zst",
        "mingw-w64-x86_64-libgpg-error-1.47-1-any.pkg.tar.zst"
    )

    $TempDir = Join-Path $env:TEMP "libssh-deps-download"
    if (Test-Path $TempDir) { Remove-Item -Recurse -Force $TempDir }
    New-Item -ItemType Directory -Path $TempDir -Force | Out-Null

    # Download packages
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

    foreach ($Package in $Packages) {
        $Url = "$MsysRepo/$Package"
        $Dest = Join-Path $TempDir $Package
        Write-Host "  Downloading: $Package" -ForegroundColor Gray
        
        try {
            Invoke-WebRequest -Uri $Url -OutFile $Dest -UseBasicParsing
        } catch {
            Write-Log "Failed to download $Package" -Level Warning
            # Try alternative source or continue
        }
    }

    # Extract packages (zst format needs special handling)
    Write-Host "  Extracting packages..." -ForegroundColor Gray

    # Create target directories
    $BinDir = Join-Path $TargetDir "bin"
    $LibDir = Join-Path $TargetDir "lib"
    $IncludeDir = Join-Path $TargetDir "include"
    New-Item -ItemType Directory -Path $BinDir -Force | Out-Null
    New-Item -ItemType Directory -Path $LibDir -Force | Out-Null
    New-Item -ItemType Directory -Path $IncludeDir -Force | Out-Null

    # Use tar to extract (Windows 10+ has built-in tar)
    foreach ($Package in $Packages) {
        $Dest = Join-Path $TempDir $Package
        if (Test-Path $Dest) {
            # Extract tar.zst using tar (Windows 10+ supports zstd)
            & tar -xzf $Dest -C $TempDir 2>&1 | Out-Null
        }
    }

    # Copy files from extracted mingw64 directory
    $MingwDir = Join-Path $TempDir "mingw64"
    if (Test-Path $MingwDir) {
        # Copy DLLs
        $SrcBin = Join-Path $MingwDir "bin"
        if (Test-Path $SrcBin) {
            Copy-Item -Path "$SrcBin\libcrypto*.dll" -Destination $BinDir -Force -ErrorAction SilentlyContinue
            Copy-Item -Path "$SrcBin\libssl*.dll" -Destination $BinDir -Force -ErrorAction SilentlyContinue
            Copy-Item -Path "$SrcBin\zlib*.dll" -Destination $BinDir -Force -ErrorAction SilentlyContinue
            Copy-Item -Path "$SrcBin\libgpg-error*.dll" -Destination $BinDir -Force -ErrorAction SilentlyContinue
        }

        # Copy libs
        $SrcLib = Join-Path $MingwDir "lib"
        if (Test-Path $SrcLib) {
            Copy-Item -Path "$SrcLib\libcrypto.dll.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue
            Copy-Item -Path "$SrcLib\libcrypto.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue
            Copy-Item -Path "$SrcLib\libssl.dll.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue
            Copy-Item -Path "$SrcLib\libssl.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue
            Copy-Item -Path "$SrcLib\libz.dll.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue
            Copy-Item -Path "$SrcLib\libz.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue
            Copy-Item -Path "$SrcLib\libgpg-error.dll.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue
        }

        # Copy headers
        $SrcInclude = Join-Path $MingwDir "include"
        if (Test-Path $SrcInclude) {
            Copy-Item -Path "$SrcInclude\openssl" -Destination $IncludeDir -Recurse -Force -ErrorAction SilentlyContinue
            Copy-Item -Path "$SrcInclude\zlib.h" -Destination $IncludeDir -Force -ErrorAction SilentlyContinue
            Copy-Item -Path "$SrcInclude\zconf.h" -Destination $IncludeDir -Force -ErrorAction SilentlyContinue
            Copy-Item -Path "$SrcInclude\gpg-error.h" -Destination $IncludeDir -Force -ErrorAction SilentlyContinue
        }
    }

    # Cleanup
    Remove-Item -Recurse -Force $TempDir -ErrorAction SilentlyContinue

    # Verify
    if (Test-Path "$LibDir\libcrypto.dll.a") {
        Write-Log "Prebuilt dependencies downloaded successfully" -Level Success
        return $true
    } else {
        Write-Log "Failed to download prebuilt dependencies" -Level Error
        return $false
    }
}

Write-Log "libssh Build Script for NTerminal" -Level Header

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$OpenSSLPrebuiltDir = Join-Path $ScriptDir "libssh-prebuilt"
$LibsshSrcDir = Join-Path $ScriptDir "libssh-src"
$LibsshBuildDir = Join-Path $ScriptDir "libssh-build"
$LibsshInstallDir = Join-Path $ScriptDir "libssh"

Write-Log "Configuration:" -Level Info
Write-Host "  Build Type:    $BuildType"
Write-Host "  Parallel Jobs: $ParallelJobs"
Write-Host "  OpenSSL:       $OpenSSLPrebuiltDir (auto-download if missing)"
Write-Host "  libssh src:    $LibsshSrcDir"
Write-Host "  libssh build:  $LibsshBuildDir"
Write-Host "  libssh install: $LibsshInstallDir"
Write-Host ""

# Check or download prebuilt OpenSSL
if (-not (Test-Path "$OpenSSLPrebuiltDir/lib/libcrypto.dll.a")) {
    Write-Log "Prebuilt OpenSSL not found. Downloading..." -Level Warning
    $Result = Download-PrebuiltDependencies -TargetDir $OpenSSLPrebuiltDir
    if (-not $Result) {
        Write-Log "Please manually download OpenSSL from: https://slproweb.com/products/Win32OpenSSL.html" -Level Warning
        Write-Log "Or use MSYS2: pacman -S mingw-w64-x86_64-openssl mingw-w64-x86_64-zlib" -Level Warning
        exit 1
    }
} else {
    Write-Log "Prebuilt OpenSSL found." -Level Success
}
Write-Host ""

Write-Log "Checking prerequisites..." -Level Info

$CMakePath = Find-QtTool "cmake"
$MakePath = Find-QtTool "mingw32-make"
$GitPath = Find-QtTool "git"

if ($MakePath -and -not [System.IO.Path]::IsPathRooted($MakePath)) {
    $MakeCmd = Get-Command $MakePath -ErrorAction SilentlyContinue
    if ($MakeCmd) {
        $MakePath = $MakeCmd.Source
    }
}

$QtMingwPath = $null
$QtMingwPaths = @(
    "C:\Qt\Tools\mingw1310_64\bin",
    "C:\Qt\Tools\mingw1230_64\bin",
    "C:\Qt\Tools\mingw1120_64\bin"
)

foreach ($Path in $QtMingwPaths) {
    if (Test-Path "$Path\gcc.exe") {
        $QtMingwPath = $Path
        break
    }
}

if (-not $QtMingwPath -and $MakePath) {
    $QtMingwPath = Split-Path $MakePath -Parent
}

if ($QtMingwPath) {
    $MakePath = Join-Path $QtMingwPath "mingw32-make.exe"
    Write-Host "Using Qt MinGW from: $QtMingwPath" -ForegroundColor Gray
}

$ToolsFound = @()

if ($CMakePath) {
    $Version = & $CMakePath --version 2>&1 | Select-Object -First 1
    Write-Host "  [OK] cmake: $Version" -ForegroundColor Green
    $ToolsFound += "cmake"
} else {
    Write-Host "  [MISSING] cmake" -ForegroundColor Red
}

if ($MakePath) {
    $Version = & $MakePath --version 2>&1 | Select-Object -First 1
    Write-Host "  [OK] mingw32-make: $Version" -ForegroundColor Green
    $ToolsFound += "make"
} else {
    Write-Host "  [MISSING] mingw32-make" -ForegroundColor Red
}

if ($GitPath) {
    $Version = & $GitPath --version 2>&1 | Select-Object -First 1
    Write-Host "  [OK] git: $Version" -ForegroundColor Green
    $ToolsFound += "git"
} else {
    Write-Host "  [MISSING] git" -ForegroundColor Red
}

if ($ToolsFound.Count -lt 3) {
    Write-Log "Missing prerequisites." -Level Error
    Write-Host ""
    Write-Host "Please install the missing tools:" -ForegroundColor Yellow
    Write-Host "  - cmake: included in Qt Tools" -ForegroundColor White
    Write-Host "  - mingw32-make: included in Qt Tools" -ForegroundColor White
    Write-Host "  - git: https://git-scm.com/download/win" -ForegroundColor White
    exit 1
}

$MingwBinDir = Split-Path $MakePath -Parent
if ($MingwBinDir -and (Test-Path $MingwBinDir)) {
    $env:PATH = "$MingwBinDir;$env:PATH"
    Write-Host "  Added MinGW to PATH: $MingwBinDir" -ForegroundColor Gray
}

Write-Log "All prerequisites found." -Level Success
Write-Host ""

if ($Clean) {
    Write-Log "Cleaning build directories..." -Level Info
    if (Test-Path $LibsshBuildDir) { Remove-Item -Recurse -Force $LibsshBuildDir }
    if (Test-Path $LibsshInstallDir) { Remove-Item -Recurse -Force $LibsshInstallDir }
    Write-Log "Clean completed." -Level Success
    Write-Host ""
}

# ============================================
# Clone libssh source
# ============================================
Write-Log "Preparing libssh source..." -Level Header

if (-not (Test-Path $LibsshSrcDir)) {
    Write-Log "Cloning libssh source (version 0.10.6)..." -Level Info

    $CloneArgs = @(
        "clone",
        "--depth", "1",
        "--branch", "libssh-0.10.6",
        "https://git.libssh.org/projects/libssh.git",
        $LibsshSrcDir
    )

    $Result = Invoke-ExternalCommand -Command $GitPath -Arguments $CloneArgs -ShowOutput
    if ($Result -ne 0) {
        Write-Log "Failed to clone libssh source." -Level Error
        exit 1
    }
    Write-Log "libssh source cloned successfully." -Level Success
} else {
    Write-Log "libssh source directory already exists. Skipping clone." -Level Info
}
Write-Host ""

# ============================================
# Build libssh
# ============================================
Write-Log "Building libssh..." -Level Header

if (Test-Path $LibsshBuildDir) {
    Remove-Item -Recurse -Force $LibsshBuildDir
}
New-Item -ItemType Directory -Path $LibsshBuildDir -Force | Out-Null
Write-Log "Build directory created." -Level Success
Write-Host ""

Write-Log "Configuring CMake..." -Level Info

$CMakeArgs = @(
    "-S", $LibsshSrcDir,
    "-B", $LibsshBuildDir,
    "-G", "MinGW Makefiles",
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DCMAKE_INSTALL_PREFIX=$LibsshInstallDir",
    "-DCMAKE_MAKE_PROGRAM=$MakePath",
    "-DOPENSSL_ROOT_DIR=$OpenSSLPrebuiltDir",
    "-DOPENSSL_INCLUDE_DIR=$OpenSSLPrebuiltDir\include",
    "-DOPENSSL_CRYPTO_LIBRARY=$OpenSSLPrebuiltDir\lib\libcrypto.dll.a",
    "-DOPENSSL_SSL_LIBRARY=$OpenSSLPrebuiltDir\lib\libssl.dll.a",
    "-DZLIB_ROOT=$OpenSSLPrebuiltDir",
    "-DZLIB_INCLUDE_DIR=$OpenSSLPrebuiltDir\include",
    "-DZLIB_LIBRARY=$OpenSSLPrebuiltDir\lib\libz.dll.a",
    "-DWITH_EXAMPLES=OFF",
    "-DWITH_SERVER=OFF",
    "-DWITH_PCAP=OFF",
    "-DWITH_DEBUG_CRYPTO=OFF",
    "-DWITH_DEBUG_CALLTRACE=OFF",
    "-DBUILD_SHARED_LIBS=ON",
    "-DCMAKE_C_FLAGS=-D_FORTIFY_SOURCE=0",
    "-Wno-dev"
)

# Convert paths to forward slashes for CMake/MinGW compatibility
$CMakeArgs = $CMakeArgs | ForEach-Object { $_ -replace '\\', '/' }

if ($Verbose) {
    Write-Host "CMake arguments:" -ForegroundColor Yellow
    $CMakeArgs | ForEach-Object { Write-Host "  $_" }
    Write-Host ""
}

$Result = Invoke-ExternalCommand -Command $CMakePath -Arguments $CMakeArgs -ShowOutput
if ($Result -ne 0) {
    Write-Log "CMake configuration failed." -Level Error
    exit 1
}
Write-Log "CMake configuration completed." -Level Success
Write-Host ""

Write-Log "Building libssh with $ParallelJobs parallel jobs..." -Level Info

$BuildStartTime = Get-Date
$BuildArgs = @("-j", $ParallelJobs)
$Result = Invoke-ExternalCommand -Command $MakePath -Arguments $BuildArgs -WorkingDirectory $LibsshBuildDir -ShowOutput

$BuildDuration = (Get-Date) - $BuildStartTime

if ($Result -ne 0) {
    Write-Log "Build failed." -Level Error
    exit 1
}
Write-Log "Build completed in $($BuildDuration.TotalSeconds.ToString('F1')) seconds." -Level Success
Write-Host ""

Write-Log "Installing libssh..." -Level Info
$InstallArgs = @("--install", $LibsshBuildDir)
$Result = Invoke-ExternalCommand -Command $CMakePath -Arguments $InstallArgs -ShowOutput

if ($Result -ne 0) {
    Write-Log "Installation failed." -Level Error
    exit 1
}
Write-Log "Installation completed." -Level Success
Write-Host ""

# Copy dependency DLLs
Write-Log "Copying dependency DLLs..." -Level Info
$LibsshBinDir = Join-Path $LibsshInstallDir "bin"

if (-not (Test-Path $LibsshBinDir)) {
    New-Item -ItemType Directory -Path $LibsshBinDir -Force | Out-Null
}

$OpenSSLBinDir = Join-Path $OpenSSLPrebuiltDir "bin"
if (Test-Path $OpenSSLBinDir) {
    $Dlls = Get-ChildItem -Path $OpenSSLBinDir -Filter "*.dll" -ErrorAction SilentlyContinue
    foreach ($Dll in $Dlls) {
        Copy-Item -Path $Dll.FullName -Destination $LibsshBinDir -Force
        Write-Host "  Copied: $($Dll.Name)" -ForegroundColor Gray
    }
}

Write-Host ""

# Copy to project bin directory
$ProjectBinDir = Join-Path $ScriptDir "bin"
if (Test-Path $ProjectBinDir) {
    Write-Log "Copying DLLs to project bin directory..." -Level Info
    $Dlls = Get-ChildItem -Path $LibsshBinDir -Filter "*.dll" -ErrorAction SilentlyContinue
    foreach ($Dll in $Dlls) {
        Copy-Item -Path $Dll.FullName -Destination $ProjectBinDir -Force
        Write-Host "  Copied: $($Dll.Name)" -ForegroundColor Gray
    }
}

Write-Host ""

# Summary
Write-Log "Build Summary" -Level Header

Write-Host "Installation Directories:" -ForegroundColor Yellow
Write-Host "  OpenSSL (prebuilt): $OpenSSLPrebuiltDir"
Write-Host "  libssh:             $LibsshInstallDir"
Write-Host ""

Write-Host "libssh Directory Structure:" -ForegroundColor Yellow

$Dirs = @("bin", "lib", "include")
foreach ($Dir in $Dirs) {
    $Path = Join-Path $LibsshInstallDir $Dir
    if (Test-Path $Path) {
        $FileCount = (Get-ChildItem -Path $Path -Recurse -File -ErrorAction SilentlyContinue).Count
        Write-Host "  $Dir/ ($FileCount files)" -ForegroundColor Green
    }
}

Write-Host ""

# List DLL files
Write-Host "DLL Files in bin directory:" -ForegroundColor Yellow
$BinPath = Join-Path $LibsshInstallDir "bin"
if (Test-Path $BinPath) {
    Get-ChildItem -Path $BinPath -Filter "*.dll" | ForEach-Object {
        Write-Host "  $_" -ForegroundColor Green
    }
}

Write-Host ""
Write-Host "Next Steps:" -ForegroundColor Yellow
Write-Host "  1. Open NTerminal project in Qt Creator"
Write-Host "  2. Run qmake to regenerate Makefile"
Write-Host "  3. Build the project"
Write-Host ""

Write-Log "libssh build completed successfully!" -Level Success