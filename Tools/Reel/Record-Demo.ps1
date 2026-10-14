<#
.SYNOPSIS
    Records scripted demo shots hands-free: launches the app in demo mode, waits
    for the shot to start, records it with Record-Clip.ps1, and lets the app exit.

.DESCRIPTION
    Shot files live in Tools\Reel\demos\. A shot's app is chosen by its file-name
    prefix: nb_* runs the Editor, so_* runs Stolen Orbit, sg_* runs the stealth
    game (NightbloomGame), each with --demo. The app writes a signal file on the
    first frame of the shot (after its warmup), so recording starts on the shot,
    not on the loading screen.

    Capture is window-only (see Record-Clip.ps1), so other windows may cover the
    app; just don't minimize it. Press Esc in the app to abort a shot.

.PARAMETER Shot
    One or more shot names (file names in demos\ without .json), or 'all'.

.PARAMETER Config
    Build configuration whose exes to run. Default Release.

.EXAMPLE
    .\Record-Demo.ps1 -Shot nb_water
    .\Record-Demo.ps1 -Shot all
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string[]]$Shot,
    [string]$Config = 'Release',
    [string]$OutDir,
    [int]$StartTimeout = 90
)

$ErrorActionPreference = 'Stop'
$here     = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo     = (Resolve-Path (Join-Path $here '..\..')).Path
$demoDir  = Join-Path $here 'demos'
if (-not $OutDir) { $OutDir = Join-Path $repo '..\Reel\clips' }

$apps = @{
    'nb' = @{ Exe = Join-Path $repo "Build\Editor\$Config\Editor.exe";           Process = 'Editor' }
    'so' = @{ Exe = Join-Path $repo "Build\StolenOrbit\$Config\StolenOrbit.exe"; Process = 'StolenOrbit' }
    'sg' = @{ Exe = Join-Path $repo "Build\Game\$Config\NightbloomGame.exe";     Process = 'NightbloomGame' }
}

# 'powershell -File' passes "a,b" as one string, so split on commas as well.
$Shot = @($Shot | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($Shot.Count -eq 1 -and $Shot[0] -eq 'all') {
    $Shot = Get-ChildItem $demoDir -Filter *.json | Sort-Object Name | ForEach-Object { $_.BaseName }
}

$results = @()
foreach ($name in $Shot) {
    $file = Join-Path $demoDir "$name.json"
    if (-not (Test-Path $file)) { throw "No shot file: $file" }
    $prefix = ($name -split '_')[0]
    if (-not $apps.ContainsKey($prefix)) { throw "Shot '$name' must start with nb_ (editor), so_ (Stolen Orbit) or sg_ (stealth game)." }
    $app = $apps[$prefix]
    if (-not (Test-Path $app.Exe)) { throw "Not built: $($app.Exe)  (run BuildAll.bat $Config)" }
    if (Get-Process -Name $app.Process -ErrorAction SilentlyContinue) {
        throw "$($app.Process) is already running - close it first so the recorder grabs the right window."
    }

    $json = Get-Content $file -Raw | ConvertFrom-Json
    $duration = if ($json.duration) { [double]$json.duration } else { [double]($json.camera | Select-Object -Last 1).t }

    $signal = Join-Path $env:TEMP "nightbloom_demo_$name.signal"
    if (Test-Path $signal) { Remove-Item -LiteralPath $signal }

    Write-Host "`n=== $name  ($duration s) ===" -ForegroundColor Cyan
    $proc = Start-Process -FilePath $app.Exe -WorkingDirectory (Split-Path $app.Exe) `
        -ArgumentList @('--demo', "`"$file`"", '--demo-signal', "`"$signal`"") -PassThru

    $sw = [Diagnostics.Stopwatch]::StartNew()
    while (-not (Test-Path $signal)) {
        if ($proc.HasExited) { throw "$($app.Process) exited before the shot started (exit $($proc.ExitCode)). Check its engine.log." }
        if ($sw.Elapsed.TotalSeconds -gt $StartTimeout) { Stop-Process -Id $proc.Id -Force; throw "Timed out waiting for '$name' to start." }
        Start-Sleep -Milliseconds 50
    }

    try {
        & (Join-Path $here 'Record-Clip.ps1') -Name $name -Process $app.Process -Duration $duration -OutDir $OutDir
    }
    finally {
        if (-not $proc.WaitForExit(15000)) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
        Remove-Item -LiteralPath $signal -ErrorAction SilentlyContinue
    }
    $results += $name
}
Write-Host "`nRecorded: $($results -join ', ')" -ForegroundColor Green
