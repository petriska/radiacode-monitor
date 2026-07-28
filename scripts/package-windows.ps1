# Stage radiacode-monitor + Qt + libusb and build a 64-bit NSIS installer.
#
# Usage:
#   .\scripts\package-windows.ps1
#   .\scripts\package-windows.ps1 -BuildBinDir M:\path\to\build\bin -QtDir M:\Qt\6.11.1\msvc2022_64
#   .\scripts\package-windows.ps1 -Version 0.2.1   # optional override
#
# Version defaults to project(... VERSION x.y.z) in CMakeLists.txt (single source of truth).
# The script always passes /DPRODUCT_VERSION=... to makensis.
#
param(
    [string] $BuildBinDir = "",
    [string] $QtDir = "",
    [string] $Version = "",
    [switch] $SkipWindeploy
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$InstallerDir = Join-Path $RepoRoot "installer"
$NsiPath = Join-Path $InstallerDir "radiacode-monitor.nsi"
$DistDir = Join-Path $RepoRoot "dist"
$StageDir = Join-Path $DistDir "stage"

function Get-ProjectVersion {
    $cmake = Join-Path $RepoRoot "CMakeLists.txt"
    if (-not (Test-Path $cmake)) {
        throw "CMakeLists.txt not found: $cmake"
    }
    $text = Get-Content -Raw $cmake
    # project(radiacode-monitor VERSION 0.2.0 LANGUAGES CXX)
    if ($text -match 'project\s*\(\s*radiacode-monitor\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
        return $Matches[1]
    }
    throw "Could not parse project VERSION from CMakeLists.txt (expected: project(radiacode-monitor VERSION x.y.z ...))."
}

function Find-Makensis {
    $candidates = @(
        "${env:ProgramFiles(x86)}\NSIS\makensis.exe",
        "${env:ProgramFiles}\NSIS\makensis.exe",
        "makensis.exe"
    )
    foreach ($c in $candidates) {
        if ($c -eq "makensis.exe") {
            $cmd = Get-Command makensis.exe -ErrorAction SilentlyContinue
            if ($cmd) { return $cmd.Source }
        } elseif (Test-Path $c) {
            return $c
        }
    }
    return $null
}

function Resolve-BuildBinDir([string] $hint) {
    if ($hint -and (Test-Path (Join-Path $hint "radiacode-monitor.exe"))) {
        return (Resolve-Path $hint).Path
    }
    $defaults = @(
        (Join-Path $RepoRoot "build\Desktop_Qt_6_11_1_MSVC2022_64bit_Release\bin"),
        (Join-Path $RepoRoot "build\bin"),
        (Join-Path $RepoRoot "build\Release\bin"),
        (Join-Path $RepoRoot "build\Release")
    )
    foreach ($d in $defaults) {
        if (Test-Path (Join-Path $d "radiacode-monitor.exe")) {
            return (Resolve-Path $d).Path
        }
    }
    # Last resort: search under build/
    $buildRoot = Join-Path $RepoRoot "build"
    if (Test-Path $buildRoot) {
        $found = Get-ChildItem -Path $buildRoot -Filter "radiacode-monitor.exe" -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.DirectoryName -match "Release|bin" } |
            Select-Object -First 1
        if ($found) { return $found.DirectoryName }
    }
    throw "radiacode-monitor.exe not found. Build Release first or pass -BuildBinDir."
}

function Resolve-QtDir([string] $hint) {
    if ($hint) {
        if (-not (Test-Path $hint)) { throw "QtDir not found: $hint" }
        return (Resolve-Path $hint).Path
    }
    if ($env:CMAKE_PREFIX_PATH) {
        $first = ($env:CMAKE_PREFIX_PATH -split ";" | Select-Object -First 1).Trim()
        if ($first -and (Test-Path (Join-Path $first "bin\windeployqt6.exe"))) {
            return (Resolve-Path $first).Path
        }
        if ($first -and (Test-Path (Join-Path $first "bin\windeployqt.exe"))) {
            return (Resolve-Path $first).Path
        }
    }
    $common = @(
        "M:\Qt\6.11.1\msvc2022_64",
        "C:\Qt\6.11.1\msvc2022_64",
        "C:\Qt\6.8.3\msvc2022_64",
        "C:\Qt\6.7.3\msvc2022_64"
    )
    foreach ($q in $common) {
        # Parentheses required: otherwise PowerShell treats "-or" as a Test-Path parameter.
        if ((Test-Path (Join-Path $q "bin\windeployqt6.exe")) -or (Test-Path (Join-Path $q "bin\windeployqt.exe"))) {
            return $q
        }
    }
    $wd = Get-Command windeployqt6.exe, windeployqt.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($wd) {
        return (Resolve-Path (Join-Path (Split-Path $wd.Source -Parent) "..")).Path
    }
    throw "Qt kit not found. Pass -QtDir (e.g. M:\Qt\6.11.1\msvc2022_64)."
}

function Find-Windeploy([string] $qtRoot) {
    $w6 = Join-Path $qtRoot "bin\windeployqt6.exe"
    $w5 = Join-Path $qtRoot "bin\windeployqt.exe"
    if (Test-Path $w6) { return $w6 }
    if (Test-Path $w5) { return $w5 }
    throw "windeployqt not found under $qtRoot\bin"
}

# --- resolve tools & inputs -------------------------------------------------
if (-not $Version) {
    $Version = Get-ProjectVersion
} elseif ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+') {
    throw "Invalid -Version '$Version' (expected x.y.z, e.g. 0.2.0)."
}

$BuildBinDir = Resolve-BuildBinDir $BuildBinDir
$QtDir = Resolve-QtDir $QtDir
$Makensis = Find-Makensis
if (-not $Makensis) {
    throw "makensis.exe not found. Install NSIS 3.x (https://nsis.sourceforge.io/)."
}
if (-not (Test-Path $NsiPath)) {
    throw "NSIS script missing: $NsiPath"
}

$required = @("radiacode-monitor.exe", "QtRadiacode.dll", "libusb-1.0.dll")
foreach ($f in $required) {
    $p = Join-Path $BuildBinDir $f
    if (-not (Test-Path $p)) {
        throw "Missing '$f' in $BuildBinDir (build Release with shared QtRadiacode + deploy runtime)."
    }
}

Write-Host "==> Repo:      $RepoRoot"
Write-Host "==> Build bin: $BuildBinDir"
Write-Host "==> Qt:        $QtDir"
Write-Host "==> Version:   $Version"
Write-Host "==> NSIS:      $Makensis"

# --- clean stage ------------------------------------------------------------
if (Test-Path $StageDir) {
    Remove-Item -Recurse -Force $StageDir
}
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null
New-Item -ItemType Directory -Path $DistDir -Force | Out-Null

# --- copy app + private DLLs ------------------------------------------------
Copy-Item (Join-Path $BuildBinDir "radiacode-monitor.exe") $StageDir
Copy-Item (Join-Path $BuildBinDir "QtRadiacode.dll") $StageDir
Copy-Item (Join-Path $BuildBinDir "libusb-1.0.dll") $StageDir

$licenseSrc = Join-Path $InstallerDir "license.txt"
if (Test-Path $licenseSrc) {
    Copy-Item $licenseSrc (Join-Path $StageDir "LICENSE.txt")
}
$mitSrc = Join-Path $RepoRoot "LICENSE"
if (Test-Path $mitSrc) {
    Copy-Item $mitSrc (Join-Path $StageDir "LICENSE-MIT.txt")
}
$thirdPartySrc = Join-Path $RepoRoot "THIRD_PARTY.md"
if (Test-Path $thirdPartySrc) {
    Copy-Item $thirdPartySrc (Join-Path $StageDir "THIRD_PARTY.md")
}
$readmeSrc = Join-Path $RepoRoot "README.md"
if (Test-Path $readmeSrc) {
    Copy-Item $readmeSrc (Join-Path $StageDir "README.md")
}

# --- Qt runtime -------------------------------------------------------------
if (-not $SkipWindeploy) {
    $windeploy = Find-Windeploy $QtDir
    $exe = Join-Path $StageDir "radiacode-monitor.exe"
    Write-Host "==> windeployqt: $windeploy"
    # --release: strip debug Qt modules; compiler runtime helps machines without VC++ redist
    & $windeploy `
        --release `
        --compiler-runtime `
        --no-translations `
        --dir $StageDir `
        $exe
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed with exit code $LASTEXITCODE"
    }
} else {
    Write-Host "==> Skipping windeployqt"
}

# --- NSIS -------------------------------------------------------------------
# NSIS expects native paths; forward slashes often work, backslashes need care in /D
$stageForNsis = $StageDir -replace "/", "\"
$outForNsis = $DistDir -replace "/", "\"

Write-Host "==> Building installer..."
& $Makensis `
    "/DPRODUCT_VERSION=$Version" `
    "/DSTAGE_DIR=$stageForNsis" `
    "/DOUT_DIR=$outForNsis" `
    $NsiPath

if ($LASTEXITCODE -ne 0) {
    throw "makensis failed with exit code $LASTEXITCODE"
}

$installer = Join-Path $DistDir "RadiaCodeMonitor-$Version-win64.exe"
if (-not (Test-Path $installer)) {
    throw "Expected installer not found: $installer"
}

Write-Host ""
Write-Host "OK: $installer"
Write-Host ("Size: {0:N1} MB" -f ((Get-Item $installer).Length / 1MB))
