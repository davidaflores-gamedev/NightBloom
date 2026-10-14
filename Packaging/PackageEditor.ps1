<#
.SYNOPSIS
    Packages a self-contained NightBloom Editor build into a zip for a GitHub release.

.DESCRIPTION
    Output (dist\NightBloomEditor-<Version>-win64.zip):
        Editor.exe
        Shaders\*.spv                (engine shaders, compiled at build time)
        Assets\                      (models + textures the editor's demo scene uses)
        MSVCP140.dll, VCRUNTIME140.dll, VCRUNTIME140_1.dll   (VC++ runtime - see below)
        README.txt, LICENSE

    On first launch the editor creates Projects\Sample next to the exe and opens it.

    Why the DLLs: a Release /MD build links the MSVC runtime dynamically, and a clean
    Windows machine (no Visual Studio, no VC++ redist) may not have it - the #1 cause
    of "it won't start on my PC". They're taken from the Visual Studio redist folder
    that matches the toolset the exe was built with (located with vswhere), not from
    System32, whose copies can be older than the toolset. Everything else Editor.exe
    imports ships with Windows 10/11, and vulkan-1.dll comes with the GPU driver.

.PARAMETER Version
    Release version used in the folder/zip name. Default v0.1.0.

.PARAMETER SkipBuild
    Package the existing Release build instead of building first.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Packaging\PackageEditor.ps1
#>
param(
    [string]$Version = "v0.1.0",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDir = Join-Path $RepoRoot "Build"
$OutDir   = Join-Path $BuildDir "Editor\Release"
Write-Host "Packaging NightBloom Editor $Version" -ForegroundColor Cyan

# --- 1) Build ----------------------------------------------------------------
if (-not $SkipBuild) {
    Write-Host "`n[1/5] Building Editor (Release)..." -ForegroundColor Yellow
    if (Get-Process -Name Editor -ErrorAction SilentlyContinue) {
        throw "Editor.exe is running - close it first (the build can't overwrite a running exe)."
    }
    & cmake --build $BuildDir --config Release --target NightbloomEditor
    if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }
} else {
    Write-Host "`n[1/5] Skipping build (-SkipBuild)." -ForegroundColor Yellow
}

$ExeSrc     = Join-Path $OutDir "Editor.exe"
$ShadersSrc = Join-Path $OutDir "Shaders"
$AssetsSrc  = Join-Path $RepoRoot "Assets"
foreach ($p in $ExeSrc, $ShadersSrc, $AssetsSrc) { if (-not (Test-Path $p)) { throw "Missing: $p" } }

# --- 2) Stage ----------------------------------------------------------------
$DistRoot  = Join-Path $RepoRoot "dist"
$StageName = "NightBloomEditor-$Version-win64"
$Stage     = Join-Path $DistRoot $StageName
Write-Host "`n[2/5] Staging -> $Stage" -ForegroundColor Yellow
if (Test-Path $Stage) { Remove-Item $Stage -Recurse -Force }
New-Item -ItemType Directory -Path $Stage -Force | Out-Null

Copy-Item $ExeSrc     (Join-Path $Stage "Editor.exe")
Copy-Item $ShadersSrc (Join-Path $Stage "Shaders") -Recurse
Copy-Item $AssetsSrc  (Join-Path $Stage "Assets")  -Recurse
Copy-Item (Join-Path $RepoRoot "LICENSE") (Join-Path $Stage "LICENSE")

# --- 3) VC++ runtime from the matching Visual Studio redist ------------------
Write-Host "`n[3/5] Bundling VC++ runtime..." -ForegroundColor Yellow
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found - is Visual Studio installed?" }
$vsPath = & $vswhere -latest -prerelease -products * -property installationPath
$crtDir = Get-ChildItem (Join-Path $vsPath "VC\Redist\MSVC") -Directory |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Get-ChildItem (Join-Path $_.FullName "x64") -Directory -Filter "Microsoft.VC*.CRT" -ErrorAction SilentlyContinue } |
    Select-Object -First 1
if (-not $crtDir) { throw "No x64 Microsoft.VC*.CRT folder under $vsPath\VC\Redist\MSVC" }
foreach ($dll in "MSVCP140.dll", "VCRUNTIME140.dll", "VCRUNTIME140_1.dll") {
    $src = Join-Path $crtDir.FullName $dll
    if (-not (Test-Path $src)) { throw "Missing $dll in $($crtDir.FullName)" }
    Copy-Item $src (Join-Path $Stage $dll)
    Write-Host ("    {0}  {1}" -f $dll, (Get-Item $src).VersionInfo.FileVersion)
}

# --- 4) README ---------------------------------------------------------------
Write-Host "`n[4/5] Writing README.txt" -ForegroundColor Yellow
$readme = @"
NightBloom Editor $Version
==========================

A Vulkan / C++20 game engine editor: terrain, water with planar reflections,
grass, volumetric clouds, GPU fireflies, cascaded shadows and a day/night cycle.

Run:  double-click Editor.exe
      The first launch creates Projects\Sample next to the exe and opens it.

Camera:  hold the right mouse button, then move with W A S D, Q / E down / up,
         Shift to move faster.
Panels:  the View menu opens the Terrain, Water, Grass, Clouds, Fireflies,
         Lighting and Day / Night panels.

Requirements:
  - Windows 10 or 11, 64-bit
  - A GPU with up-to-date Vulkan 1.2 drivers (current NVIDIA, AMD or Intel)

Won't start?
  - Update your graphics drivers (Vulkan comes with the driver).
  - Make sure you unzipped the whole folder; Editor.exe needs the Shaders and
    Assets folders and the DLLs beside it.
  - Still nothing: look at engine.log in this folder for the error.

Source: https://github.com/davidaflores-gamedev/NightBloom   (MIT license)
"@
Set-Content -Path (Join-Path $Stage "README.txt") -Value ($readme -replace "`r?`n", "`r`n") -Encoding UTF8

# --- 5) Zip ------------------------------------------------------------------
$Zip = Join-Path $DistRoot "$StageName.zip"
Write-Host "`n[5/5] Zipping -> $Zip" -ForegroundColor Yellow
if (Test-Path $Zip) { Remove-Item $Zip -Force }
Compress-Archive -Path $Stage -DestinationPath $Zip
$hash = (Get-FileHash $Zip -Algorithm SHA256).Hash
Write-Host ("`nDone. {0}  ({1:N1} MB)" -f $Zip, ((Get-Item $Zip).Length / 1MB)) -ForegroundColor Green
Write-Host "SHA-256: $hash"
