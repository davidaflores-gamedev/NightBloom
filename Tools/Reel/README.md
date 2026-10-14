# Reel tools

Scripts for recording NightBloom footage and cutting it into a demo reel. They run in Windows PowerShell 5.1 and use [ffmpeg](https://ffmpeg.org/); install it with `winget install Gyan.FFmpeg`.

| Script | What it does |
|---|---|
| `Record-Demo.ps1` | **Hands-free.** Launches the editor or Stolen Orbit in demo mode with a scripted shot from `demos\`, records exactly the shot, and lets the app quit. `-Shot all` re-records everything. |
| `Record-Clip.ps1` | Records one app window to MP4 at 60 fps, cropped to its client area. Uses NVENC when available. Warns if the clip came out black. |
| `Build-Reel.ps1` | Turns an edit list into a captioned 1080p reel with title and end cards and crossfades. It also exports a palette-optimized README GIF and PNG screenshots. |

Capture uses ffmpeg's `gfxcapture` (Windows.Graphics.Capture) by default. It records **only the target window**, even while other windows cover it, and never the rest of the screen. The window must not be minimized. `-Method Desktop` falls back to `ddagrab`, which records that region of the screen, so the window must stay uncovered. `gdigrab` isn't used: it records Vulkan and DirectX windows as black.

## Demo mode (scripted shots)

`Editor.exe --demo <shot.json>` and `StolenOrbit.exe --demo <shot.json>` play a scripted shot, then quit. They never capture the mouse or save settings.

- **Editor shots** (`nb_*.json`) set a keyframed camera path. Positions can be relative to the terrain height. They also set time of day, debug cascades, live terrain edits, cloud and water slider values, and which panels show. See the header of `Editor/Source/EditorDemo.hpp` for every field.
- **Stolen Orbit shots** (`so_*.json`) pick an autopilot program: `flight`, `lockon` or `drift`. The autopilot drives the real flight, lock-on and weapon code.

```powershell
.\Record-Demo.ps1 -Shot nb_water        # one shot
.\Record-Demo.ps1 -Shot all             # every shot in demos\
```

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
