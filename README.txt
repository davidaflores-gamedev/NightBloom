# Nightbloom Engine

A custom real-time rendering engine built with Vulkan 1.4 and C++20, designed as both a portfolio piece and a foundation for game prototypes.

## Features

### Rendering
- **Vulkan 1.4** core with VMA memory management, 2 frames in flight
- **glTF 2.0** model loading (cgltf) and stb_image textures, with mipmap generation
- **Cascaded shadow maps** (4 cascades, PCF, blend bands, live-tunable resolution)
- Directional + point lights, **reverse-Z** infinite projection
- **HDR pipeline**: ACES tonemapping, bloom, FXAA, exposure, vignette
- Camera **frustum culling**

### Environment systems
- **Terrain** displaced from a GPU-generated heightmap, slope/height material blend, distance LOD
- **Water** with planar reflections, Fresnel and animated normals
- **GPU grass** (instanced blades)
- **Volumetric clouds** raymarched in a low-resolution compute pass
- **Fireflies**: GPU boids flocking simulation
- Day/night cycle

### Compute
- GPU procedural noise generation (Perlin, Worley, Perlin-Worley, FBM) for heightmaps and cloud textures

### Gameplay foundation
- Scene + project serialization (`.nbscene` / `.nbproj` JSON)
- Standalone game runtime (`NightbloomGame`) with a kinematic character controller
- Pawn / vehicle / third-person camera framework
- Raycast and line-of-sight queries; guard AI with vision cones and detection
- Keyboard/mouse, XInput and DualSense input

### Editor
- **ImGui-based interface** with dockable panels
- Scene hierarchy, inspector, lighting, terrain/water/grass/cloud/firefly panels
- **Shader node editor** and runtime shader compilation
- GPU profiler + debug panel

## Controls (Editor)

| Input | Action |
|-------|--------|
| Hold Right Mouse Button | Enable camera look |
| W / A / S / D | Move forward / left / backward / right |
| Mouse Movement (while RMB held) | Look around |

## Running the Pre-built Distribution

1. Download the latest release from the [Releases](../../releases) page
2. Extract the zip file
3. Run the executable

**Requirements:**
- Windows 10/11
- GPU with Vulkan support (most modern NVIDIA, AMD, or Intel GPUs)
- Up-to-date graphics drivers

## Building from Source

### Prerequisites
- **Visual Studio 2026** (`BuildAll.bat` uses the `Visual Studio 18 2026` generator; for VS 2022, run CMake yourself with `-G "Visual Studio 17 2022"`)
- **Vulkan SDK** — install from [LunarG](https://vulkan.lunarg.com/)
  - Ensure the `VULKAN_SDK` environment variable is set (`glslc` is used to compile shaders)
- **CMake 3.20+**

### Build Steps

1. Clone the repository (with submodules, for googletest):
```bash
   git clone --recursive https://github.com/davidaflores-gamedev/NightBloom.git
   cd NightBloom
```

2. Generate and build everything (engine, editor, game runtime, tests):
```batch
   call BuildAll.bat          REM or: BuildAll.bat Release
```

3. Open `Build\NightbloomWorkspace.slnx` in Visual Studio. The Editor is the default startup project.

Everything builds into the single `Build\` tree:
- `Build\Editor\<Config>\Editor.exe`
- `Build\Game\<Config>\NightbloomGame.exe`
- `Build\bin\<Config>\NightbloomTests.exe`

### Packaging a game

`Packaging\PackageGame.ps1` builds a Release runtime plus a project into a zip under `dist\`.

## Project Structure
```
NightBloom/                 # repo root
├── CmakeLists.txt          # root project — the only one you should generate
├── BuildAll.bat
├── cmake/                  # shared CMake helpers (shader compilation)
├── NightBloom/             # engine static library
│   ├── Engine/             # Core, Renderer (+Vulkan, Components), Terrain, Foliage,
│   │                       # Hydro, VFX, Gameplay, Physics, Input, Window, Math, Tests
│   ├── Shaders/            # GLSL sources (.vert .frag .comp), compiled at build time
│   └── ThirdParty/         # GLM, ImGui, VMA, cgltf, stb_image, nlohmann/json, googletest
├── Editor/                 # editor application
├── Game/                   # standalone game runtime
├── Packaging/              # packaging scripts
├── Assets/                 # shared models and textures
└── Build/                  # generated build tree (not in repo)
```

## Third-Party Libraries

- [Vulkan Memory Allocator (VMA)](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
- [Dear ImGui](https://github.com/ocornut/imgui)
- [GLM](https://github.com/g-truc/glm)
- [cgltf](https://github.com/jkuhlmann/cgltf)
- [stb_image](https://github.com/nothings/stb)
- [nlohmann/json](https://github.com/nlohmann/json)
- [GoogleTest](https://github.com/google/googletest)

## License

MIT License — see [LICENSE](LICENSE). (Applies to the engine, editor and game runtime in this repo.)

## Author

David Flores — [davidafloresgamedev.com](https://davidafloresgamedev.com)
