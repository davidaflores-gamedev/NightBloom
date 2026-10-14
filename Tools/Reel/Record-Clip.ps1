<#
.SYNOPSIS
    Records one clip of a running app window with ffmpeg's ddagrab (Desktop Duplication).

.DESCRIPTION
    Finds the target window by process name or window title, crops the capture to its
    client area (no title bar), and records at 60 fps to an H.264 MP4.

    ddagrab is used instead of gdigrab because gdigrab reads the window through GDI,
    which often returns BLACK frames for Vulkan/DirectX swapchains. ddagrab captures
    the composed desktop, so the window must be visible and not covered. Use windowed
    or borderless mode, not exclusive fullscreen.

    Encoder: h264_nvenc when an NVIDIA GPU is available (cheap, so the game keeps its
    frame rate), otherwise libx264 'ultrafast'. Both are near-lossless; the reel
    builder re-encodes for delivery anyway.

    Works in Windows PowerShell 5.1. Needs ffmpeg on PATH
    (winget install Gyan.FFmpeg).

.PARAMETER Name
    Clip name. The file is written to <OutDir>\<Name>.mp4.

.PARAMETER Process
    Process name of the window to capture, e.g. Editor, StolenOrbit, NightbloomGame.

.PARAMETER Title
    Alternative to -Process: a substring of the window title.

.PARAMETER Duration
    Seconds to record. 0 (default) records until you press 'q' in this console.

.PARAMETER Delay
    Seconds to wait before recording starts. Gives you time to click into the game.

.PARAMETER Monitor
    Override the ddagrab output index. Normally auto-detected from the window position.

.EXAMPLE
    .\Record-Clip.ps1 -Name so_lockon -Process StolenOrbit -Duration 20 -Delay 3
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Name,
    [string]$Process,
    [string]$Title,
    [double]$Duration = 0,
    [double]$Delay = 0,
    [int]$Fps = 60,
    [int]$Monitor = -1,
    [switch]$ShowCursor,
    [string]$OutDir    # default: <repo>\..\Reel\clips (the reel workspace beside the repo)
)

$ErrorActionPreference = 'Stop'

# $PSScriptRoot is empty inside param() defaults on Windows PowerShell 5.1, so the
# default output folder is resolved here instead.
if (-not $OutDir) { $OutDir = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) '..\..\..\Reel\clips' }

# Runs a native tool and returns its combined output as strings. Windows PowerShell
# 5.1 wraps native stderr lines in ErrorRecords when redirected, which would trip
# ErrorActionPreference=Stop even on success, so relax it for the call.
function Invoke-Native([string]$Exe, [string[]]$ArgList) {
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try { & $Exe @ArgList 2>&1 | ForEach-Object { "$_" } } finally { $ErrorActionPreference = $old }
}

if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
    throw "ffmpeg not found on PATH. Install it with:  winget install Gyan.FFmpeg  (then open a new terminal)"
}
if (-not $Process -and -not $Title) { throw "Pass -Process <name> or -Title <text> to pick the window." }

# --- Win32 interop: DPI-aware window/client rectangles -----------------------
if (-not ('ReelWin32' -as [type])) {
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ReelWin32 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
}
"@
}
[void][ReelWin32]::SetProcessDPIAware()   # physical pixels, matching what ddagrab sees
Add-Type -AssemblyName System.Windows.Forms

$proc = Get-Process | Where-Object {
    $_.MainWindowHandle -ne [IntPtr]::Zero -and
    ( ($Process -and $_.ProcessName -eq $Process) -or ($Title -and $_.MainWindowTitle -like "*$Title*") )
} | Select-Object -First 1
if (-not $proc) { throw "No visible window found for $(if ($Process) { "process '$Process'" } else { "title '*$Title*'" }). Is it running?" }
$hwnd = $proc.MainWindowHandle
if ([ReelWin32]::IsIconic($hwnd)) { throw "The window is minimized - restore it first." }

$rc = New-Object ReelWin32+RECT
[void][ReelWin32]::GetClientRect($hwnd, [ref]$rc)
$pt = New-Object ReelWin32+POINT
[void][ReelWin32]::ClientToScreen($hwnd, [ref]$pt)

# Pick the monitor that contains the client area's centre. ddagrab's output_idx
# follows DXGI output order, which matches Screen.AllScreens on typical setups;
# if a multi-monitor capture comes out wrong, pass -Monitor explicitly.
$cx = $pt.X + [int]($rc.Right / 2); $cy = $pt.Y + [int]($rc.Bottom / 2)
$screens = [System.Windows.Forms.Screen]::AllScreens
$idx = 0
for ($i = 0; $i -lt $screens.Count; $i++) {
    if ($screens[$i].Bounds.Contains($cx, $cy)) { $idx = $i; break }
}
if ($Monitor -ge 0) { $idx = $Monitor }
$scr = $screens[$idx].Bounds

# Offsets are relative to that monitor; clamp to it and keep sizes even (H.264).
$x = [Math]::Max(0, $pt.X - $scr.X); $y = [Math]::Max(0, $pt.Y - $scr.Y)
$w = [Math]::Min($rc.Right,  $scr.Width  - $x); $h = [Math]::Min($rc.Bottom, $scr.Height - $y)
$w -= $w % 2; $h -= $h % 2
if ($w -lt 16 -or $h -lt 16) { throw "Window client area is too small or off-screen ($w x $h)." }

New-Item -ItemType Directory -Force $OutDir | Out-Null
$out = Join-Path (Resolve-Path $OutDir) "$Name.mp4"

# Encoder choice: NVENC if this ffmpeg build + GPU support it.
$enc = @('-c:v', 'libx264', '-preset', 'ultrafast', '-crf', '14')
$null = Invoke-Native ffmpeg @('-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', 'color=black:s=256x256:d=0.1', '-c:v', 'h264_nvenc', '-f', 'null', '-')
if ($LASTEXITCODE -eq 0) { $enc = @('-c:v', 'h264_nvenc', '-preset', 'p4', '-tune', 'hq', '-rc', 'vbr', '-cq', '16', '-b:v', '0') }

$grab = "ddagrab=output_idx=${idx}:framerate=${Fps}:offset_x=${x}:offset_y=${y}:video_size=${w}x${h}:draw_mouse=$([int][bool]$ShowCursor)"
$ffArgs = @('-hide_banner', '-loglevel', 'warning', '-stats', '-y',
          '-f', 'lavfi', '-i', $grab,
          '-vf', 'hwdownload,format=bgra,format=yuv420p') + $enc
if ($Duration -gt 0) { $ffArgs += @('-t', "$Duration") }
$ffArgs += @('-movflags', '+faststart', $out)

Write-Host ("Window : {0} (pid {1})  '{2}'" -f $proc.ProcessName, $proc.Id, $proc.MainWindowTitle)
Write-Host ("Capture: monitor {0}, {1}x{2} at +{3}+{4}, {5} fps, {6}" -f $idx, $w, $h, $x, $y, $Fps, $enc[1])
Write-Host ("Output : {0}" -f $out)
if ($Delay -gt 0) { Write-Host "Starting in $Delay s..."; Start-Sleep -Milliseconds ([int]($Delay * 1000)) }
if ($Duration -le 0) { Write-Host "Recording - press 'q' in this window to stop." -ForegroundColor Yellow }

$old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
& ffmpeg @ffArgs
$ErrorActionPreference = $old
if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed (exit $LASTEXITCODE)." }

# Sanity check: a capture that is (almost) entirely black usually means the window
# was covered, minimized, or in exclusive fullscreen.
$black = Invoke-Native ffmpeg @('-hide_banner', '-i', $out, '-vf', 'blackdetect=d=0.5:pix_th=0.05', '-an', '-f', 'null', '-') | Select-String 'black_start'
if ($black) { Write-Warning "Black frames detected:`n$($black -join "`n")`nIf the whole clip is black, use windowed/borderless mode and keep the window uncovered." }
$len = Invoke-Native ffprobe @('-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', $out) | Select-Object -First 1
Write-Host ("Done: {0}  ({1:N1} s, {2:N1} MB)" -f $out, [double]$len, ((Get-Item $out).Length / 1MB)) -ForegroundColor Green
