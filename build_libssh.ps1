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
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

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

    Write-Log "Setting up prebuilt OpenSSL and zlib for MinGW64..." -Level Info

    # Create target directories
    $BinDir = Join-Path $TargetDir "bin"
    $LibDir = Join-Path $TargetDir "lib"
    $IncludeDir = Join-Path $TargetDir "include"
    New-Item -ItemType Directory -Path $BinDir -Force | Out-Null
    New-Item -ItemType Directory -Path $LibDir -Force | Out-Null
    New-Item -ItemType Directory -Path $IncludeDir -Force | Out-Null

    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

    # Attempt 1: Check for existing MSYS2 installation
    $MsysPaths = @(
        "C:\msys64\mingw64",
        "D:\msys64\mingw64",
        "${env:MSYS64_ROOT}\mingw64"
    )

    foreach ($MsysPath in $MsysPaths) {
        if (Test-Path "$MsysPath\lib\libcrypto.dll.a") {
            Write-Host "  Found MSYS2 installation at: $MsysPath" -ForegroundColor Green
            Write-Host "  Copying files from MSYS2..." -ForegroundColor Gray

            try {
                # Copy DLLs
                Get-ChildItem -Path "$MsysPath\bin" -Filter "*.dll" -ErrorAction SilentlyContinue | Where-Object {
                    $_.Name -match "^(libcrypto|libssl|zlib)" -or $_.Name -match "-3-x64\.dll$"
                } | ForEach-Object {
                    Copy-Item -Path $_.FullName -Destination $BinDir -Force
                    Write-Host "    Copied: $($_.Name)" -ForegroundColor Gray
                }

                # Copy libs
                Copy-Item -Path "$MsysPath\lib\libcrypto.dll.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue
                Copy-Item -Path "$MsysPath\lib\libssl.dll.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue
                Copy-Item -Path "$MsysPath\lib\libz.dll.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue
                Copy-Item -Path "$MsysPath\lib\libcrypto.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue
                Copy-Item -Path "$MsysPath\lib\libssl.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue
                Copy-Item -Path "$MsysPath\lib\libz.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue

                # Copy headers
                if (Test-Path "$MsysPath\include\openssl") {
                    Copy-Item -Path "$MsysPath\include\openssl" -Destination $IncludeDir -Recurse -Force
                }
                Copy-Item -Path "$MsysPath\include\zlib.h" -Destination $IncludeDir -Force -ErrorAction SilentlyContinue
                Copy-Item -Path "$MsysPath\include\zconf.h" -Destination $IncludeDir -Force -ErrorAction SilentlyContinue

                if (Test-Path "$LibDir\libcrypto.dll.a") {
                    Write-Log "Files copied from MSYS2 successfully." -Level Success
                    return $true
                }
            } catch {
                Write-Host "  Failed to copy from MSYS2: $($_.Exception.Message)" -ForegroundColor Gray
            }
        }
    }

    # Attempt 2: Download from GitHub releases
    $TempDir = Join-Path $env:TEMP "libssh-deps-download"
    if (Test-Path $TempDir) { Remove-Item -Recurse -Force $TempDir }
    New-Item -ItemType Directory -Path $TempDir -Force | Out-Null

    $DownloadSuccess = $false

    # Try curl-for-win prebuilt binaries (reliable source)
    $DownloadUrls = @(
        @{
            Name = "curl-for-win OpenSSL"
            Url = "https://curl.se/windows/latest.cgi?p=mingw64/openssl-3.4.0-win64-mingw.zip"
            File = "openssl.zip"
        },
        @{
            Name = "GitHub mirror"
            Url = "https://github.com/AntonyBlare/openssl-mingw64/releases/download/v3.4.0/openssl-3.4.0-mingw64.zip"
            File = "openssl.zip"
        }
    )

    foreach ($Source in $DownloadUrls) {
        if ($DownloadSuccess) { break }

        Write-Host "  Trying: $($Source.Name)..." -ForegroundColor Gray
        $DestFile = Join-Path $TempDir $Source.File

        try {
            Invoke-WebRequest -Uri $Source.Url -OutFile $DestFile -UseBasicParsing -TimeoutSec 60

            if (Test-Path $DestFile) {
                Expand-Archive -Path $DestFile -DestinationPath $TempDir -Force
                $DownloadSuccess = $true
                Write-Host "  Download successful from $($Source.Name)" -ForegroundColor Green
            }
        } catch {
            Write-Host "  Failed: $($_.Exception.Message)" -ForegroundColor Gray
        }
    }

    # Attempt 3: Extract from MSYS2 .zst packages (requires zstd or 7zip)
    if (-not $DownloadSuccess) {
        Write-Host "  Trying: MSYS2 repository (zst packages)..." -ForegroundColor Gray

        $HasZstd = Get-Command "zstd" -ErrorAction SilentlyContinue

        # Check for 7zip in PATH or local portable version
        $Has7zip = Get-Command "7z" -ErrorAction SilentlyContinue
        $Local7zip = Join-Path $ScriptDir "7zr.exe"
        if (-not $Has7zip -and (Test-Path $Local7zip)) {
            $Has7zip = $Local7zip
        }

        if ($HasZstd -or $Has7zip) {
            $MsysRepo = "https://repo.msys2.org/mingw/mingw64"

            # Get latest package versions dynamically
            try {
                $PackageList = Invoke-WebRequest -Uri "$MsysRepo/" -UseBasicParsing -TimeoutSec 30
                $OpenSSLMatch = [regex]::Match($PackageList.Content, 'href="(mingw-w64-x86_64-openssl-[^"]+\.zst)"')
                $ZlibMatch = [regex]::Match($PackageList.Content, 'href="(mingw-w64-x86_64-zlib-[^"]+\.zst)"')

                $Packages = @()
                if ($OpenSSLMatch.Success) { $Packages += $OpenSSLMatch.Groups[1].Value }
                if ($ZlibMatch.Success) { $Packages += $ZlibMatch.Groups[1].Value }
            } catch {
                # Fallback to known versions
                $Packages = @(
                    "mingw-w64-x86_64-openssl-3.4.0-1-any.pkg.tar.zst",
                    "mingw-w64-x86_64-zlib-1.3.1-1-any.pkg.tar.zst"
                )
            }

            foreach ($Package in $Packages) {
                $Url = "$MsysRepo/$Package"
                $Dest = Join-Path $TempDir $Package
                Write-Host "    Downloading: $Package" -ForegroundColor Gray

                try {
                    Invoke-WebRequest -Uri $Url -OutFile $Dest -UseBasicParsing -TimeoutSec 120

                    if ($HasZstd) {
                        & zstd -d $Dest -o "$Dest.tar" 2>&1 | Out-Null
                        & tar -xf "$Dest.tar" -C $TempDir 2>&1 | Out-Null
                        $DownloadSuccess = $true
                    } elseif ($Has7zip) {
                        $7zipCmd = if ($Has7zip -is [string]) { $Has7zip } else { "7z" }
                        & $7zipCmd x $Dest -o"$TempDir" -y 2>&1 | Out-Null
                        $DownloadSuccess = $true
                    }
                } catch {
                    Write-Host "    Failed: $($_.Exception.Message)" -ForegroundColor Gray
                }
            }
        } else {
            Write-Host "  Skipping MSYS2 packages (zstd/7zip not found)" -ForegroundColor Gray
        }
    }

    # Copy extracted files to target directory
    if ($DownloadSuccess) {
        $ExtractedDirs = Get-ChildItem -Path $TempDir -Directory | Where-Object {
            $_.Name -like "*mingw*" -or $_.Name -like "*openssl*" -or $_.Name -eq "mingw64"
        }

        foreach ($ExtractedDir in $ExtractedDirs) {
            Write-Host "  Processing: $($ExtractedDir.Name)" -ForegroundColor Gray

            # Copy DLLs
            $SrcBin = Join-Path $ExtractedDir.FullName "bin"
            if (Test-Path $SrcBin) {
                Get-ChildItem -Path $SrcBin -Filter "*.dll" | Where-Object {
                    $_.Name -match "^(libcrypto|libssl|zlib)" -or $_.Name -match "-3-x64\.dll$"
                } | ForEach-Object {
                    Copy-Item -Path $_.FullName -Destination $BinDir -Force
                    Write-Host "    Copied DLL: $($_.Name)" -ForegroundColor Gray
                }
            }

            # Copy libs
            $SrcLib = Join-Path $ExtractedDir.FullName "lib"
            if (Test-Path $SrcLib) {
                Copy-Item -Path "$SrcLib\libcrypto.dll.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue
                Copy-Item -Path "$SrcLib\libcrypto.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue
                Copy-Item -Path "$SrcLib\libssl.dll.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue
                Copy-Item -Path "$SrcLib\libssl.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue
                Copy-Item -Path "$SrcLib\libz.dll.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue
                Copy-Item -Path "$SrcLib\libz.a" -Destination $LibDir -Force -ErrorAction SilentlyContinue
                Write-Host "    Copied library files" -ForegroundColor Gray
            }

            # Copy headers
            $SrcInclude = Join-Path $ExtractedDir.FullName "include"
            if (Test-Path $SrcInclude) {
                if (Test-Path "$SrcInclude\openssl") {
                    Copy-Item -Path "$SrcInclude\openssl" -Destination $IncludeDir -Recurse -Force
                }
                Copy-Item -Path "$SrcInclude\zlib.h" -Destination $IncludeDir -Force -ErrorAction SilentlyContinue
                Copy-Item -Path "$SrcInclude\zconf.h" -Destination $IncludeDir -Force -ErrorAction SilentlyContinue
                Write-Host "    Copied header files" -ForegroundColor Gray
            }
        }
    }

    # Cleanup
    Remove-Item -Recurse -Force $TempDir -ErrorAction SilentlyContinue

    # Verify
    if (Test-Path "$LibDir\libcrypto.dll.a") {
        Write-Log "Prebuilt dependencies setup successfully." -Level Success
        return $true
    }

    # If all else failed, show manual instructions
    Write-Log "Failed to setup prebuilt OpenSSL automatically." -Level Warning
    Write-Host ""
    Write-Host "Please setup OpenSSL manually:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  Option 1: Install MSYS2 (recommended)" -ForegroundColor White
    Write-Host "    1. Download from https://www.msys2.org/" -ForegroundColor Gray
    Write-Host "    2. Run in MSYS2 terminal:" -ForegroundColor Gray
    Write-Host "       pacman -S mingw-w64-x86_64-openssl mingw-w64-x86_64-zlib" -ForegroundColor Cyan
    Write-Host "    3. Copy files from C:\msys64\mingw64 to:" -ForegroundColor Gray
    Write-Host "       $TargetDir" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "  Option 2: Install zstd or 7zip for automatic extraction" -ForegroundColor White
    Write-Host "    choco install zstd" -ForegroundColor Gray
    Write-Host "    or" -ForegroundColor Gray
    Write-Host "    choco install 7zip" -ForegroundColor Gray
    Write-Host ""
    Write-Host "  Required files in openssl-deps:" -ForegroundColor Yellow
    Write-Host "    bin/libcrypto-3-x64.dll" -ForegroundColor Gray
    Write-Host "    bin/libssl-3-x64.dll" -ForegroundColor Gray
    Write-Host "    bin/zlib1.dll" -ForegroundColor Gray
    Write-Host "    lib/libcrypto.dll.a" -ForegroundColor Gray
    Write-Host "    lib/libssl.dll.a" -ForegroundColor Gray
    Write-Host "    lib/libz.dll.a" -ForegroundColor Gray
    Write-Host "    include/openssl/*.h" -ForegroundColor Gray

    return $false
}

Write-Log "libssh Build Script for NTerminal" -Level Header

$OpenSSLPrebuiltDir = Join-Path $ScriptDir "openssl-deps"
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
Write-Host "  OpenSSL (auto-download): $OpenSSLPrebuiltDir"
Write-Host "  libssh:                  $LibsshInstallDir"
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