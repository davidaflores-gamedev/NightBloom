# Reel tools

Scripts for recording NightBloom footage and cutting it into a demo reel. They run in Windows PowerShell 5.1 and use [ffmpeg](https://ffmpeg.org/); install it with `winget install Gyan.FFmpeg`.

| Script | What it does |
|---|---|
| `Record-Clip.ps1` | Records one app window to MP4 with `ddagrab` (Desktop Duplication), cropped to the window's client area, at 60 fps. Uses NVENC when available. Warns if the clip came out black. |
| `Build-Reel.ps1` | Turns an edit list into a captioned 1080p reel with title and end cards and crossfades. It also exports a palette-optimized README GIF and PNG screenshots. |

`ddagrab` is used rather than `gdigrab` because GDI capture often records Vulkan and DirectX windows as black. Keep the window visible, and use windowed or borderless mode, not exclusive fullscreen.

## Record

```powershell
.\Record-Clip.ps1 -Name water -Process Editor -Duration 20 -Delay 3
```

`-Process` takes the exe name without `.exe`; `-Title` matches part of the window title instead. With no `-Duration`, recording stops when you press `q`. Clips default to `..\Reel\clips\`, next to the repository.

## Build

```powershell
.\Build-Reel.ps1 -Config path\to\reel.json            # reel + gif + screenshots
.\Build-Reel.ps1 -Config path\to\reel.json -Only gif  # just one output
```

See `reel.example.json` for the format. Relative paths resolve against the config file's folder. Each segment takes:

| Key | Meaning |
|---|---|
| `file` | Source clip |
| `start` | In-point, in seconds |
| `duration` | Length on screen, in seconds |
| `speed` | Optional speed-up, e.g. `5` for a time-lapse. Reads `duration × speed` seconds of source. |
| `crop` | Optional `w:h:x:y` crop in source pixels, to zoom in |
| `caption`, `sub` | Optional lower-third text |

Clips of any size or frame rate are scaled to fit, letterboxed rather than stretched, and converted to 60 fps. Intermediate files go to `<outDir>\work`.
