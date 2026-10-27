# ============================================================
# scToLocalDesk | scripts/build_windows.ps1  (Doc18 section 7.1)
# One-shot build for a fresh clone: MSVC env + Ninja configure +
# build + windeployqt. Output: build-msvc\scToLocalDesk.exe (QML)
# and build-msvc\scToLocalDeskWidgets.exe with Qt runtime beside.
# Usage: powershell -File scripts\build_windows.ps1 [-Clean] [-SkipDeploy]
# Pre-req: CMake on PATH, VS2019 vcvars64.bat, Qt 6.5.3 msvc2019_64,
#          Qt bundled Ninja (override via -VsVars / -QtPath / -NinjaPath).
# Note: project path must not contain spaces (cmd /s quoting).
# ============================================================
param(
    [switch]$Clean,
    [switch]$SkipDeploy,
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [string]$QtPath = "F:\Apps\QT\6.5.3\msvc2019_64",
    [string]$VsVars = "E:\IDE_FOR_ALL\visual studio 2019 professional\VC\Auxiliary\Build\vcvars64.bat",
    [string]$NinjaPath = "F:\Apps\QT\Tools\Ninja\ninja.exe"
)

$ErrorActionPreference = "Stop"
$build = Join-Path $Root "build-msvc"

if (-not (Test-Path $VsVars)) { Write-Error "vcvars64.bat not found: $VsVars"; exit 1 }
if (-not (Test-Path $NinjaPath)) { Write-Error "ninja.exe not found: $NinjaPath"; exit 1 }
if (-not (Test-Path (Join-Path $QtPath "bin\windeployqt.exe"))) {
    Write-Error "Qt (windeployqt) not found: $QtPath"; exit 1
}

# ---- configure (Ninja single-config, Release per Doc baseline) ----
# Ninja needs cl.exe on PATH: configure must run inside the MSVC env too.
# CMAKE_MAKE_PROGRAM pinned: a broken npm-shim ninja on PATH would fail
# the generator probe ("unknown error" from AppData npm ninja)
if ($Clean -and (Test-Path $build)) { Remove-Item $build -Recurse -Force }
if (-not (Test-Path (Join-Path $build "CMakeCache.txt"))) {
    $cfg = "`"$VsVars`" && cmake -S $Root -B $build -G Ninja" `
        + " -DCMAKE_MAKE_PROGRAM=$NinjaPath" `
        + " -DCMAKE_BUILD_TYPE=Release" `
        + " -DCMAKE_PREFIX_PATH=$QtPath" `
        + " -DSCTOLOCALDESK_BUILD_CORE_ONLY=ON" `
        + " -DSCTOLOCALDESK_BUILD_QT_APP=ON" `
        + " -DSCTOLOCALDESK_BUILD_WIDGETS_APP=ON" `
        + " -DSCTOLOCALDESK_BUILD_TESTS=ON"
    & "$env:ComSpec" /s /d /c $cfg
    if ($LASTEXITCODE -ne 0) { Write-Error "cmake configure failed"; exit 1 }
}

# ---- build inside MSVC env (INCLUDE/LIB unset in a plain shell) ----
$cmd = "`"$VsVars`" && cmake --build $build"
& "$env:ComSpec" /s /d /c $cmd
if ($LASTEXITCODE -ne 0) { Write-Error "build failed"; exit 1 }

# ---- artifacts ----
$exes = @("scToLocalDesk.exe", "scToLocalDeskWidgets.exe")
foreach ($e in $exes) {
    if (-not (Test-Path (Join-Path $build $e))) {
        Write-Error "missing artifact: $e"; exit 1
    }
}

# ---- FFmpeg runtime hint (DLLs auto-copied by CMake from third_party) ----
if (-not (Test-Path (Join-Path $build "avcodec-63.dll"))) {
    Write-Warning "avcodec-63.dll missing: video codec disabled (SCLD_HAS_FFMPEG=0). Put LGPL shared FFmpeg into third_party\ffmpeg\{include,lib,bin} then rebuild."
}

# ---- windeployqt (QML build needs --qmldir to keep QML modules) ----
if (-not $SkipDeploy) {
    $wdep = Join-Path $QtPath "bin\windeployqt.exe"
    & $wdep --release --no-compiler-runtime --qmldir (Join-Path $Root "src\ui\qml") (Join-Path $build "scToLocalDesk.exe")
    if ($LASTEXITCODE -ne 0) { Write-Error "windeployqt (qml) failed"; exit 1 }
    & $wdep --release --no-compiler-runtime (Join-Path $build "scToLocalDeskWidgets.exe")
    if ($LASTEXITCODE -ne 0) { Write-Error "windeployqt (widgets) failed"; exit 1 }
}

Write-Output "build_windows done:"
Get-Item ($exes | ForEach-Object { Join-Path $build $_ }) |
    ForEach-Object { "  {0}  {1:N1} MB" -f $_.Name, ($_.Length / 1MB) }
