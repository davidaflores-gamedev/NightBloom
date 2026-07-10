<#
.SYNOPSIS
    Assembles a self-contained, distributable StolenOrbit build and zips it.

.DESCRIPTION
    StolenOrbit is a standalone prototype target: it builds its "reference field"
    scene procedurally and uses only in-memory engine textures (default_white,
    uv_checker), so - unlike NightbloomGame - it needs NO project/scene/Assets tree.
    The whole package is just:

        StolenOrbit/
          StolenOrbit.exe
          Shaders/*.spv               (compiled by nightbloom_compile_shaders)
          MSVCP140.dll                (VC++ 2015-2022 runtime - see below)
          VCRUNTIME140.dll
          VCRUNTIME140_1.dll
          README.txt

    Why the DLLs: a Release /MD build links the MSVC runtime dynamically, and a
    clean Windows machine (no Visual Studio / no VC++ redist) won't have it - the
    #1 cause of "it won't start on my friend's PC". Bundling them makes it run on
    any stock Win10/11. Everything else it needs (vulkan-1.dll from the GPU driver,
    xinput1_4.dll, the api-ms-win-crt-* Universal CRT) already ships with the OS.

.PARAMETER Config
    CMake build config. Default: Release (windowed, no console).

.PARAMETER SkipBuild
    Skip the cmake build step and package the existing build output.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Packaging\PackageStolenOrbit.ps1
#>
param(
    [string]$Config = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

# --- Paths -------------------------------------------------------------------
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDir = Join-Path $RepoRoot "build"
$OutDir   = Join-Path $BuildDir "StolenOrbit\$Config"

Write-Host "=== StolenOrbit packager ===" -ForegroundColor Cyan
Write-Host "  Repo:   $RepoRoot"
Write-Host "  Config: $Config"

# --- 1) Build ----------------------------------------------------------------
if (-not $SkipBuild) {
    Write-Host "`n[1/4] Building StolenOrbit ($Config)..." -ForegroundColor Yellow
    # Free the exe if a previous run is still holding the file lock.
    Get-Process StolenOrbit -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    & cmake --build $BuildDir --config $Config --target StolenOrbit
    if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }
} else {
    Write-Host "`n[1/4] Skipping build (-SkipBuild)." -ForegroundColor Yellow
}

$ExeSrc     = Join-Path $OutDir "StolenOrbit.exe"
$ShadersSrc = Join-Path $OutDir "Shaders"
if (-not (Test-Path $ExeSrc))     { throw "Exe not found: $ExeSrc (build it first, or drop -SkipBuild)" }
if (-not (Test-Path $ShadersSrc)) { throw "Compiled shaders not found: $ShadersSrc" }

# --- 2) Stage ----------------------------------------------------------------
$DistRoot  = Join-Path $RepoRoot "dist"
$StageName = "StolenOrbit"
$Stage     = Join-Path $DistRoot $StageName
Write-Host "`n[2/4] Staging -> $Stage" -ForegroundColor Yellow

if (Test-Path $Stage) { Remove-Item $Stage -Recurse -Force }
New-Item -ItemType Directory -Path $Stage -Force | Out-Null

Copy-Item $ExeSrc     (Join-Path $Stage "StolenOrbit.exe") -Force
Copy-Item $ShadersSrc (Join-Path $Stage "Shaders") -Recurse -Force

# --- 3) Bundle the VC++ runtime ----------------------------------------------
# Prefer the VS redist (matches the toolset exactly); fall back to System32 on
# the dev machine (same x64 arch as the exe).
Write-Host "`n[3/4] Bundling VC++ runtime..." -ForegroundColor Yellow
$crtDlls = @("MSVCP140.dll", "VCRUNTIME140.dll", "VCRUNTIME140_1.dll")

$redistDir = $null
if ($env:VCToolsRedistDir) {
    $cand = Join-Path $env:VCToolsRedistDir "x64\Microsoft.VC143.CRT"
    if (Test-Path $cand) { $redistDir = $cand }
}
foreach ($dll in $crtDlls) {
    $src = $null
    if ($redistDir) { $c = Join-Path $redistDir $dll; if (Test-Path $c) { $src = $c } }
    if (-not $src)  { $c = Join-Path $env:SystemRoot "System32\$dll"; if (Test-Path $c) { $src = $c } }
    if ($src) {
        Copy-Item $src (Join-Path $Stage $dll) -Force
        Write-Host "    bundled $dll"
    } else {
        Write-Warning "    could not find $dll - recipients may need the VC++ 2015-2022 x64 redist"
    }
}

# A short README so a tester knows what to do.
$readmeLines = @(
    'STOLEN ORBIT - flight/combat prototype',
    '======================================',
    '',
    'Run:  double-click StolenOrbit.exe',
    '',
    'Controls (Xbox / XInput controller recommended):',
    '  Hold RT ............ boost mode        Double-tap RT ... super boost',
    '  LT ................. brake / burnout',
    '  Left stick ......... yaw / pitch       Right stick ..... roll (X), throttle (Y)',
    '  RB ................. gun                LB (hold) ....... lock-on',
    '  A .................. missile (homes onto a locked target)',
    '  D-pad L/R .......... strafe            D-pad U/D ....... vertical',
    '  Y .................. toggle camera      F5 .............. toggle shadows',
    '  Esc ................ release mouse',
    '',
    'Keyboard/mouse also works (WASD + mouse; LMB gun, RMB lock-on, C missile).',
    '',
    'Wont start?',
    '  - Update your graphics drivers (the game needs Vulkan; the driver provides it).',
    '  - If it still wont launch, install the Microsoft Visual C++ 2015-2022',
    '    Redistributable (x64) from Microsoft.'
)
Set-Content -Path (Join-Path $Stage "README.txt") -Value ($readmeLines -join "`r`n") -Encoding UTF8

# --- 4) Zip ------------------------------------------------------------------
$Zip = Join-Path $DistRoot "$StageName.zip"
Write-Host "`n[4/4] Zipping -> $Zip" -ForegroundColor Yellow
if (Test-Path $Zip) { Remove-Item $Zip -Force }
Compress-Archive -Path (Join-Path $Stage "*") -DestinationPath $Zip -Force

$sizeMB = [math]::Round((Get-Item $Zip).Length / 1MB, 1)
Write-Host "`nDone. $Zip ($sizeMB MB)" -ForegroundColor Green
Write-Host "Test it: unzip anywhere and double-click StolenOrbit.exe" -ForegroundColor Green
