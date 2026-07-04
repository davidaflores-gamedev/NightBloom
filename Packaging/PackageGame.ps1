<#
.SYNOPSIS
    Assembles a self-contained, distributable NightbloomGame build and zips it.
    (Milestone 3 — see .claude/PACKAGING_ROADMAP.md.)

.DESCRIPTION
    Produces a folder someone can unzip and double-click with no dev environment:

        <Name>/
          NightbloomGame.exe
          Shaders/*.spv
          Assets/                (the global Assets tree — the runtime's asset root)
          Project/
            <name>.nbproj
            Scenes/*.nbscene     (model "source" paths rewritten to project-relative)

    At runtime the packaged Game finds Project/ next to the exe (GameApp::ResolveProjectFile),
    and AssetManager resolves relative scene paths + code-loaded textures against Assets/.

    Portability hinges on the scene storing RELATIVE model paths. New editor saves already do
    (SceneSerializer + AssetManager::ToAssetRelativePath); this script also migrates any legacy
    ABSOLUTE paths in the copied scenes so an older project still packages correctly.

.PARAMETER Project
    Path to the .nbproj to bundle. Default: Projects/Sample/Sample.nbproj.

.PARAMETER Config
    CMake build config. Default: Release (already windowed — no console).

.PARAMETER SkipBuild
    Skip the cmake build step (use the existing build output).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File Packaging\PackageGame.ps1
    powershell -ExecutionPolicy Bypass -File Packaging\PackageGame.ps1 -Project Projects\MyGame\MyGame.nbproj
#>
param(
    [string]$Project,
    [string]$Config = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

# --- Paths -------------------------------------------------------------------
$RepoRoot  = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDir  = Join-Path $RepoRoot "build"
$GlobalAssets = Join-Path $RepoRoot "Assets"

if (-not $Project) {
    $Project = Join-Path $RepoRoot "Projects\Sample\Sample.nbproj"
}
if (-not (Test-Path $Project)) {
    throw "Project file not found: $Project"
}
$Project = (Resolve-Path $Project).Path
$ProjectDir = Split-Path $Project -Parent
$ProjectName = [System.IO.Path]::GetFileNameWithoutExtension($Project)

Write-Host "=== NightBloom packager ===" -ForegroundColor Cyan
Write-Host "  Repo:    $RepoRoot"
Write-Host "  Project: $Project"
Write-Host "  Config:  $Config"

# --- 1) Build the Game -------------------------------------------------------
if (-not $SkipBuild) {
    Write-Host "`n[1/4] Building NightbloomGame ($Config)..." -ForegroundColor Yellow
    & cmake --build $BuildDir --config $Config --target NightbloomGame
    if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)" }
} else {
    Write-Host "`n[1/4] Skipping build (-SkipBuild)." -ForegroundColor Yellow
}

$ExeSrc     = Join-Path $BuildDir "Game\$Config\NightbloomGame.exe"
$ShadersSrc = Join-Path $BuildDir "Game\$Config\Shaders"
if (-not (Test-Path $ExeSrc))     { throw "Game exe not found: $ExeSrc (build it first, or drop -SkipBuild)" }
if (-not (Test-Path $ShadersSrc)) { throw "Compiled shaders not found: $ShadersSrc" }
if (-not (Test-Path $GlobalAssets)) { throw "Global Assets folder not found: $GlobalAssets" }

# --- 2) Stage the package ----------------------------------------------------
$DistRoot = Join-Path $RepoRoot "dist"
$StageName = "NightBloom-$ProjectName"
$Stage = Join-Path $DistRoot $StageName
Write-Host "`n[2/4] Staging -> $Stage" -ForegroundColor Yellow

if (Test-Path $Stage) { Remove-Item $Stage -Recurse -Force }
New-Item -ItemType Directory -Path $Stage -Force | Out-Null

# exe + shaders + the global asset tree (the runtime's fallback asset root)
Copy-Item $ExeSrc     (Join-Path $Stage "NightbloomGame.exe") -Force
Copy-Item $ShadersSrc (Join-Path $Stage "Shaders") -Recurse -Force
Copy-Item $GlobalAssets (Join-Path $Stage "Assets") -Recurse -Force

# the project (.nbproj + Scenes + its own Assets), into Project/
$StageProject = Join-Path $Stage "Project"
New-Item -ItemType Directory -Path $StageProject -Force | Out-Null
Copy-Item (Join-Path $ProjectDir "*") $StageProject -Recurse -Force

# --- 3) Migrate scene asset paths to project-relative ------------------------
# Rewrite any absolute model "source" (".../Assets/Models/..") down to the part
# after "/Assets/" ("Models/.."), which resolves against the bundled Assets tree.
# Done as a targeted text replace so the rest of each scene file stays byte-identical.
Write-Host "`n[3/4] Migrating scene asset paths to relative..." -ForegroundColor Yellow
$sceneFiles = Get-Content -ErrorAction SilentlyContinue -Path (Join-Path $StageProject "Scenes\*.nbscene")
$scenePaths = Get-ChildItem -Path (Join-Path $StageProject "Scenes") -Filter *.nbscene -ErrorAction SilentlyContinue
foreach ($sf in $scenePaths) {
    $text = Get-Content $sf.FullName -Raw
    # "source": "<anything>/Assets/<rest>"  ->  "source": "<rest>"
    $new = [regex]::Replace($text, '("source"\s*:\s*")[^"]*?/[Aa]ssets/', '$1')
    if ($new -ne $text) {
        Set-Content -Path $sf.FullName -Value $new -Encoding UTF8 -NoNewline
        Write-Host "    migrated $($sf.Name)"
    } else {
        Write-Host "    $($sf.Name): already relative (no change)"
    }
}

# --- 4) Zip ------------------------------------------------------------------
$Zip = Join-Path $DistRoot "$StageName.zip"
Write-Host "`n[4/4] Zipping -> $Zip" -ForegroundColor Yellow
if (Test-Path $Zip) { Remove-Item $Zip -Force }
Compress-Archive -Path (Join-Path $Stage "*") -DestinationPath $Zip -Force

$sizeMB = [math]::Round((Get-Item $Zip).Length / 1MB, 1)
Write-Host "`nDone. $Zip ($sizeMB MB)" -ForegroundColor Green
Write-Host "Test it: unzip anywhere and double-click NightbloomGame.exe" -ForegroundColor Green
