# NightBloom

A hand-written **Vulkan / C++20** game engine and editor, plus the game prototypes built on top of it.

NightBloom started as a "render a beautiful night scene" project and grew into a small platform: a renderer with modern lighting and environment systems, an ImGui editor with project and scene files, a standalone game runtime, and a packaging pipeline that ships a playable build.

<!-- HERO GIF: uncomment once the reel is recorded
<p align="center"><img src="docs/media/hero.gif" alt="NightBloom in motion" width="800"></p>
-->

<!-- VIDEO: uncomment once the reel is uploaded
**▶ [Watch the 60-second demo reel](https://www.youtube.com/watch?v=VIDEO_ID)**
-->

## Highlights

**Rendering**
- **Cascaded shadow maps**: 4 cascades in one depth array, stable texel-snapped fitting, PCF with cascade blend bands, live-selectable 1024–4096 resolution
- **HDR pipeline**: linear HDR scene target, bloom, exposure, ACES filmic tonemapping, vignette
- **Planar-reflection water**: mirrored camera into an MSAA-compatible offscreen target, Fresnel, animated normals, below-surface clipping
- **Volumetric clouds**: raymarched in a reduced-resolution compute pass, then composited at full resolution with correct depth occlusion
- **GPU fireflies**: boids flocking simulated in a compute shader, drawn as additive billboards
- **Terrain and grass**: GPU heightmap displacement with slope/height material blending and distance LOD; instanced grass blades
- **Day/night cycle** driving the sun/moon light, reverse-Z infinite projection, camera frustum culling, GPU timing scopes

**Architecture**
- Data-driven draw commands: each system declares its own descriptor bindings, so a new GPU system needs no changes to the command recorder
- Compute dispatch, GPU procedural noise (Perlin / Worley / Perlin-Worley / FBM), VMA-backed memory, staging-buffer pool
- Scenes and projects serialized as JSON (`.nbscene` / `.nbproj`)
- Raycast and line-of-sight queries against scene geometry and terrain
- Unit tests with GoogleTest: **96 tests across 22 suites**

**Tools**
- ImGui editor with dockable panels: scene hierarchy, inspector, lighting, day/night, and panels for terrain, water, grass, clouds and fireflies
- Shader node editor and runtime shader compilation
- One-command build, plus a script that packages a self-contained game zip

## The stealth prototype

`NightbloomGame` is a third-person stealth prototype set in a single hand-built camp at night. **Steal the relic and get out.** The moment any guard fully confirms you, you're caught.

- **Eight guards, eight different jobs**: a gate sentry, counter-rotating perimeter and inner patrols, a relic guard, and others, each with its own vision cone, timing and detection meter
- Guards **investigate** noises and **find bodies**
- **Shadows conceal you**: darkness shortens how far guards can see you, and moving draws attention
- Stand, crouch or go prone; climb ledges
- Throw **stones as distractions**, and use **takedowns** from behind

It's a prototype: one camp, one objective, keyboard and mouse only.

### Controls

| Input | Action |
|---|---|
| W A S D | Move |
| Mouse | Look |
| Shift | Sprint |
| Ctrl (tap) | Toggle crouch |
| Ctrl (hold) | Go prone / crawl |
| Space | Jump / climb ledge |
| Q (hold) | Aim stone, release to throw |
| F (hold) | Survey guards |
| E | Interact / takedown |
| R | Restart mission |
| Esc | Menu (settings, bindings, test range) |

## Also built on NightBloom: Stolen Orbit

A third-person space-combat prototype: dual-mode hover/boost flight, sticky lock-on with lead indicators, homing missiles, rebindable controls, and full Xbox/DualSense controller support. Its source lives in a separate private repository, so it isn't included here.

## Download

Grab the **NightBloom Editor** from the [Releases](../../releases) page, unzip the whole folder, and run `Editor.exe`. The first launch creates a `Projects\Sample` project next to the exe and opens it. Hold the right mouse button and use W A S D to fly; the environment panels are on the right.

**Requirements:** Windows 10/11 x64 and a GPU with up-to-date Vulkan 1.2+ drivers. The exe isn't code-signed, so Windows SmartScreen may warn on first run: choose **More info → Run anyway**.

The stealth prototype isn't in the release yet; build it from source (below).

## Building from source

### Prerequisites
- **Visual Studio 2026** with the C++ desktop workload. `BuildAll.bat` uses the `Visual Studio 18 2026` generator; for VS 2022, run CMake yourself with `-G "Visual Studio 17 2022"`.
- **Vulkan SDK** from [LunarG](https://vulkan.lunarg.com/), with the `VULKAN_SDK` environment variable set. Its `glslc` compiles the shaders during the build.
- **CMake 3.20+**

### Build
```bat
git clone --recursive https://github.com/davidaflores-gamedev/NightBloom.git
cd NightBloom
BuildAll.bat            REM Debug, or: BuildAll.bat Release
```

Then open `Build\NightbloomWorkspace.slnx`. The Editor is the default startup project.

| Output | Path |
|---|---|
| Editor | `Build\Editor\<Config>\Editor.exe` |
| Game | `Build\Game\<Config>\NightbloomGame.exe` |
| Tests | `Build\bin\<Config>\NightbloomTests.exe` |

Run the **Editor once first**. On first launch it creates the bundled `Projects\Sample` project, which the game loads.

## Packaging

```powershell
powershell -ExecutionPolicy Bypass -File Packaging\PackageGame.ps1
```

This builds a Release game and writes a self-contained zip to `dist\`. The zip contains the exe, compiled shaders, assets, and the project, with scene paths rewritten to be relative. Pass `-Project <path.nbproj>` to package a different project.

## Repository layout

```
NightBloom/
├── NightBloom/          Engine static library
│   ├── Engine/          Core, Renderer (+ Vulkan backend), Terrain, Foliage, Hydro, VFX,
│   │                    Gameplay, Physics, Input, Window, Math, Tests
│   ├── Shaders/         GLSL sources, compiled to SPIR-V at build time
│   └── ThirdParty/      GLM, Dear ImGui, VMA, cgltf, stb_image, nlohmann/json, GoogleTest
├── Editor/              Editor application and panels
├── Game/                Stealth prototype runtime
├── Packaging/           Packaging scripts
├── Assets/              Shared models and textures
├── cmake/               Shared CMake helpers
└── BuildAll.bat         Configure and build everything into Build/
```

## Third-party libraries

[Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) · [Dear ImGui](https://github.com/ocornut/imgui) · [GLM](https://github.com/g-truc/glm) · [cgltf](https://github.com/jkuhlmann/cgltf) · [stb_image](https://github.com/nothings/stb) · [nlohmann/json](https://github.com/nlohmann/json) · [GoogleTest](https://github.com/google/googletest)

## License

MIT — see [LICENSE](LICENSE).

## Author

David Flores — [davidafloresgamedev.com](https://davidafloresgamedev.com)
