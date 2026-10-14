<#
.SYNOPSIS
    Cuts recorded clips into a captioned 1080p reel, plus a README GIF and screenshots.

.DESCRIPTION
    Everything is driven by an edit list (JSON, see reel.example.json):
      - title card, end card
      - segments: source clip, in-point, length, optional speed-up, caption + sub-caption
      - optional music bed (faded out at the end)
      - gif: a short loop for the README (palette-optimized)
      - screenshots: PNG stills pulled from source clips

    Every segment is normalized to the same size / frame rate (letterboxed, never
    stretched), captioned, then the segments are joined with crossfades. Intermediate
    files go to <outDir>\work so a re-run only takes a minute.

    Works in Windows PowerShell 5.1. Needs ffmpeg/ffprobe on PATH
    (winget install Gyan.FFmpeg).

.PARAMETER Config
    Path to the edit list. Relative paths inside it resolve against its folder.

.PARAMETER Only
    Build only some outputs: reel, gif, shots. Default: all three.

.EXAMPLE
    .\Build-Reel.ps1 -Config D:\GitLibrary\Personal\Nightbloom\Reel\reel.json
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Config,
    [ValidateSet('reel', 'gif', 'shots')] [string[]]$Only = @('reel', 'gif', 'shots')
)

$ErrorActionPreference = 'Stop'
foreach ($t in 'ffmpeg', 'ffprobe') {
    if (-not (Get-Command $t -ErrorAction SilentlyContinue)) {
        throw "$t not found on PATH. Install it with:  winget install Gyan.FFmpeg  (then open a new terminal)"
    }
}

# --- helpers -----------------------------------------------------------------
function Invoke-FF([string[]]$ArgList, [string]$What) {
    # 5.1 turns redirected native stderr into ErrorRecords; keep EAP relaxed here.
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try { $log = & ffmpeg -hide_banner -loglevel error -y @ArgList 2>&1 | ForEach-Object { "$_" } }
    finally { $ErrorActionPreference = $old }
    if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed while $What (exit $LASTEXITCODE):`n$($log -join "`n")" }
}
function Get-Duration([string]$File) {
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try { $d = & ffprobe -v error -show_entries format=duration -of csv=p=0 $File 2>$null | Select-Object -First 1 }
    finally { $ErrorActionPreference = $old }
    if (-not $d) { throw "Could not read the duration of $File" }
    [double]::Parse($d, [Globalization.CultureInfo]::InvariantCulture)
}
function Num([double]$v) { $v.ToString('0.###', [Globalization.CultureInfo]::InvariantCulture) }
function Resolve-In([string]$p) {
    if ([string]::IsNullOrWhiteSpace($p)) { return $null }
    if ([IO.Path]::IsPathRooted($p)) { return $p }
    [IO.Path]::GetFullPath((Join-Path $script:BaseDir $p))
}
# Paths inside a filtergraph: forward slashes, and the drive colon escaped.
function FPath([string]$p) { ($p -replace '\\', '/') -replace '^([A-Za-z]):', '$1\:' }
$utf8 = New-Object System.Text.UTF8Encoding($false)   # drawtext would draw a BOM
$script:TextN = 0
function New-TextFile([string]$Text) {
    $script:TextN++
    $f = Join-Path $script:Work ("text_{0:D3}.txt" -f $script:TextN)
    [IO.File]::WriteAllText($f, $Text, $utf8)
    FPath $f
}
# One drawtext filter. Text goes through a file so quotes/colons/commas need no escaping.
function DrawText([string]$Text, [string]$Font, [int]$Size, [string]$Color, [string]$X, [string]$Y,
                  [double]$FadeIn = 0, [double]$Start = 0, [switch]$Box) {
    $alpha = if ($FadeIn -gt 0) { "if(lt(t\,$(Num $Start))\,0\,min(1\,(t-$(Num $Start))/$(Num $FadeIn)))" } else { '1' }
    $f = "drawtext=fontfile='$(FPath $Font)':textfile='$(New-TextFile $Text)':expansion=none" +
         ":fontsize=${Size}:fontcolor=${Color}:x=${X}:y=${Y}:alpha='$alpha'" +
         ":shadowcolor=black@0.6:shadowx=2:shadowy=2"
    if ($Box) { $f += ":box=1:boxcolor=0x0b0d12@0.72:boxborderw=14" }
    $f
}

# --- config ------------------------------------------------------------------
$Config = (Resolve-Path $Config).Path
$script:BaseDir = Split-Path $Config
$cfg = Get-Content $Config -Raw -Encoding UTF8 | ConvertFrom-Json

$W   = if ($cfg.width)  { [int]$cfg.width }  else { 1920 }
$H   = if ($cfg.height) { [int]$cfg.height } else { 1080 }
$FPS = if ($cfg.fps)    { [int]$cfg.fps }    else { 60 }
$Fade = if ($null -ne $cfg.fade) { [double]$cfg.fade } else { 0.4 }
$Bg  = if ($cfg.background) { $cfg.background } else { '0x0b0d12' }
$Accent = if ($cfg.accent) { $cfg.accent } else { '0x8fb8ff' }
$SubColor = if ($cfg.subColor) { $cfg.subColor } else { '0xd6e4ff' }
$FontReg  = if ($cfg.font)     { $cfg.font }     else { 'C:/Windows/Fonts/segoeui.ttf' }
$FontBold = if ($cfg.fontBold) { $cfg.fontBold } else { 'C:/Windows/Fonts/segoeuib.ttf' }
foreach ($f in $FontReg, $FontBold) { if (-not (Test-Path $f)) { throw "Font not found: $f" } }

$OutDir = Resolve-In $(if ($cfg.outDir) { $cfg.outDir } else { 'out' })
$script:Work = Join-Path $OutDir 'work'
New-Item -ItemType Directory -Force $OutDir, $script:Work | Out-Null
Get-ChildItem $script:Work -Filter 'text_*.txt' -ErrorAction SilentlyContinue | Remove-Item

$encArgs = @('-c:v', 'libx264', '-preset', 'medium', '-crf', '16', '-pix_fmt', 'yuv420p', '-r', "$FPS")
$normalize = "fps=$FPS,scale=${W}:${H}:force_original_aspect_ratio=decrease:flags=lanczos," +
             "pad=${W}:${H}:(ow-iw)/2:(oh-ih)/2:color=$Bg,setsar=1"

# --- cards -------------------------------------------------------------------
function New-Card([object]$Card, [string]$Out) {
    $dur = if ($Card.duration) { [double]$Card.duration } else { 3 }
    $lines = @($Card.lines)
    $filters = @()
    # Line 0 = headline, line 1 = accent subtitle, the rest = small detail lines.
    $sizes = @(96, 46) + (@(34) * [Math]::Max(0, $lines.Count - 2))
    $total = 0; for ($i = 0; $i -lt $lines.Count; $i++) { $total += $sizes[$i] * 1.45 }
    $y = ($H - $total) / 2
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $font  = if ($i -eq 0) { $FontBold } else { $FontReg }
        $color = if ($i -eq 1) { $Accent } elseif ($i -eq 0) { 'white' } else { '0xc9d1dc' }
        $filters += DrawText $lines[$i] $font $sizes[$i] $color '(w-text_w)/2' ([int]$y) -FadeIn 0.5 -Start (0.15 * $i)
        $y += $sizes[$i] * 1.45
    }
    $vf = ($filters -join ',') + ",fade=in:st=0:d=0.4"
    Invoke-FF (@('-f', 'lavfi', '-i', "color=c=${Bg}:s=${W}x${H}:r=${FPS}:d=$(Num $dur)", '-vf', $vf) + $encArgs + @($Out)) "rendering a card"
}

# --- segments ----------------------------------------------------------------
function New-Segment([object]$Seg, [int]$Index, [string]$Out) {
    $src = Resolve-In $Seg.file
    if (-not (Test-Path $src)) { throw "Segment $Index source not found: $src" }
    $start = if ($Seg.start) { [double]$Seg.start } else { 0 }
    $speed = if ($Seg.speed) { [double]$Seg.speed } else { 1 }
    $len   = [double]$Seg.duration             # length ON SCREEN, after speed-up
    $srcLen = Get-Duration $src
    if ($start + $len * $speed -gt $srcLen + 0.05) {
        throw ("Segment {0} ({1}) asks for {2}s at {3}x from {4}s, but the clip is only {5:N2}s long." -f $Index, $Seg.file, $len, $speed, $start, $srcLen)
    }
    $vf = @()
    if ($Seg.crop) { $vf += "crop=$($Seg.crop)" }           # e.g. "1600:900:160:90" to zoom in
    if ($speed -ne 1) { $vf += "setpts=PTS/$(Num $speed)" }
    $vf += $normalize
    $margin = [int]($H * 0.074)
    # Keep captions inside the picture when a narrower clip is pillarboxed.
    $mx = $margin
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $dims = (& ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 $src 2>$null | Select-Object -First 1) -split ','
    $ErrorActionPreference = $old
    if ($dims.Count -eq 2) {
        $sw = [double]$dims[0]; $sh = [double]$dims[1]
        if ($Seg.crop) { $c = "$($Seg.crop)" -split ':'; $sw = [double]$c[0]; $sh = [double]$c[1] }
        $padX = ($W - [Math]::Min($W, $sw * $H / $sh)) / 2
        $mx += [int][Math]::Max(0, $padX)
    }
    if ($Seg.caption) {
        $capY = if ($Seg.sub) { "h-$($margin + 104)" } else { "h-$($margin + 60)" }
        $vf += DrawText $Seg.caption $FontBold 46 'white' "$mx" $capY -FadeIn 0.35 -Start 0.2 -Box
    }
    if ($Seg.sub) {
        $vf += DrawText $Seg.sub $FontReg 32 $SubColor "$mx" "h-$($margin + 36)" -FadeIn 0.35 -Start 0.35 -Box
    }
    $ffa = @('-ss', (Num $start), '-t', (Num ($len * $speed)), '-i', $src, '-an', '-vf', ($vf -join ',')) + $encArgs + @('-t', (Num $len), $Out)
    Invoke-FF $ffa "rendering segment $Index ($($Seg.file))"
}

# --- reel --------------------------------------------------------------------
$reelOut = Join-Path $OutDir $(if ($cfg.reelName) { $cfg.reelName } else { 'reel.mp4' })
if ($Only -contains 'reel') {
    $parts = @()
    $sw = [Diagnostics.Stopwatch]::StartNew()
    if ($cfg.titleCard) { $p = Join-Path $script:Work 'card_title.mp4'; New-Card $cfg.titleCard $p; $parts += $p; Write-Host "  title card" }
    $i = 0
    foreach ($seg in @($cfg.segments)) {
        $i++
        $p = Join-Path $script:Work ("seg_{0:D2}.mp4" -f $i)
        New-Segment $seg $i $p; $parts += $p
        Write-Host ("  segment {0,2}: {1}" -f $i, $(if ($seg.caption) { $seg.caption } else { $seg.file }))
    }
    if ($cfg.endCard) { $p = Join-Path $script:Work 'card_end.mp4'; New-Card $cfg.endCard $p; $parts += $p; Write-Host "  end card" }
    if ($parts.Count -eq 0) { throw "Nothing to build: the config has no cards or segments." }

    # Chain crossfades. Each xfade starts $Fade before the running total ends.
    $inputs = @(); foreach ($p in $parts) { $inputs += @('-i', $p) }
    $durs = $parts | ForEach-Object { Get-Duration $_ }
    $fc = @(); $prev = '[0:v]'; $acc = $durs[0]
    for ($k = 1; $k -lt $parts.Count; $k++) {
        $off = $acc - $Fade
        $lbl = "[x$k]"
        $fc += "$prev[${k}:v]xfade=transition=fade:duration=$(Num $Fade):offset=$(Num $off)$lbl"
        $prev = $lbl; $acc = $off + $durs[$k]
    }
    $total = $acc
    $fc += "${prev}fade=out:st=$(Num ($total - 0.6)):d=0.6,format=yuv420p[vout]"

    $ffa = $inputs
    $music = Resolve-In $cfg.music
    if ($music) {
        if (-not (Test-Path $music)) { throw "Music file not found: $music" }
        $ffa += @('-i', $music)
        $fc += "[$($parts.Count):a]atrim=0:$(Num $total),afade=in:d=0.5,afade=out:st=$(Num ($total - 2)):d=2[aout]"
    }
    $ffa += @('-filter_complex', ($fc -join ';'), '-map', '[vout]')
    if ($music) { $ffa += @('-map', '[aout]', '-c:a', 'aac', '-b:a', '192k') }
    $ffa += @('-c:v', 'libx264', '-preset', 'slow', '-crf', '18', '-pix_fmt', 'yuv420p', '-r', "$FPS", '-movflags', '+faststart', $reelOut)
    Invoke-FF $ffa "joining the reel"
    Write-Host ("Reel: {0}  ({1:N1} s, {2:N1} MB, {3:N0} s to build)" -f $reelOut, (Get-Duration $reelOut), ((Get-Item $reelOut).Length / 1MB), $sw.Elapsed.TotalSeconds) -ForegroundColor Green
}

# --- README gif --------------------------------------------------------------
if ($Only -contains 'gif' -and $cfg.gif) {
    $g = $cfg.gif
    $src = if ($g.file) { Resolve-In $g.file } else { $reelOut }
    if (-not (Test-Path $src)) { throw "GIF source not found: $src (build the reel first, or set gif.file)" }
    $gw   = if ($g.width) { [int]$g.width } else { 800 }
    $gfps = if ($g.fps)   { [int]$g.fps }   else { 15 }
    $gout = Join-Path $OutDir $(if ($g.name) { $g.name } else { 'hero.gif' })
    $pal  = Join-Path $script:Work 'palette.png'
    $base = "fps=$gfps,scale=${gw}:-1:flags=lanczos"
    if ($g.crop) { $base = "crop=$($g.crop),$base" }
    $cut = @('-ss', (Num ([double]$g.start)), '-t', (Num ([double]$g.duration)), '-i', $src)
    Invoke-FF ($cut + @('-vf', "$base,palettegen=max_colors=256:stats_mode=diff", $pal)) "building the GIF palette"
    Invoke-FF ($cut + @('-i', $pal, '-lavfi', "$base[x];[x][1:v]paletteuse=dither=sierra2_4a:diff_mode=rectangle", '-loop', '0', $gout)) "encoding the GIF"
    Write-Host ("GIF : {0}  ({1:N1} MB)" -f $gout, ((Get-Item $gout).Length / 1MB)) -ForegroundColor Green
    if ((Get-Item $gout).Length -gt 10MB) { Write-Warning "GIF is over 10 MB - GitHub READMEs load slowly. Lower gif.width, gif.fps or gif.duration." }
}

# --- screenshots ---------------------------------------------------------------
if ($Only -contains 'shots' -and $cfg.screenshots) {
    $shotDir = Join-Path $OutDir 'screenshots'
    New-Item -ItemType Directory -Force $shotDir | Out-Null
    foreach ($s in @($cfg.screenshots)) {
        $src = if ($s.file) { Resolve-In $s.file } else { $reelOut }
        $png = Join-Path $shotDir "$($s.name).png"
        Invoke-FF @('-ss', (Num ([double]$s.time)), '-i', $src, '-frames:v', '1', $png) "grabbing screenshot $($s.name)"
        Write-Host "Shot: $png"
    }
}
