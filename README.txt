# Nightbloom Engine

A custom real-time rendering engine built with Vulkan 1.4 and C++, designed as both a portfolio piece and foundation for game development.

## Features

### Rendering
- **Vulkan 1.4** core with VMA memory management
- **PBR Materials** with glTF 2.0 model loading
- **Shadow Mapping** for directional lights
- **Point Lights** support
- **Reverse-Z Depth** for improved precision
- **Mipmap Generation**

### Compute Pipeline
- GPU-based **procedural noise generation** (Perlin, Worley, Perlin-Worley, FBM)
- Two-tier memory/disk caching for generated textures
- Foundation for volumetric clouds, GPU culling, and particle systems

### Editor
- **ImGui-based interface** with dockable panels
- Scene hierarchy and inspector
- **Shader node editor** with GLSL/SPIR-V hot-reload
- Debug visualization panel

## Controls

| Input | Action |
|-------|--------|
| Hold Right Mouse Button | Enable camera look |
| W / A / S / D | Move forward / left / backward / right |
| Mouse Movement (while RMB held) | Look around |

## Running the Pre-built Distribution

1. Download the latest release from the [Releases](../../releases) page
2. Extract the zip file
3. Run `Editor.exe`

**Requirements:**
- Windows 10/11
- GPU with Vulkan support (most modern NVIDIA, AMD, or Intel GPUs)
- Up-to-date graphics drivers

## Building from Source

### Prerequisites
- **Visual Studio 2022** or **Visual Studio 2026**
- **Vulkan SDK** — install from [LunarG](https://vulkan.lunarg.com/)
  - Ensure the `VULKAN_SDK` environment variable is set
- **CMake 3.20+**

### Build Steps

1. Clone the repository:
```bash
   git clone https://github.com/davidaflores-gamedev/Nightbloom.git
   cd Nightbloom/NightBloom
```

2. Generate and build:
```batch
   call BuildAll.bat
```

3. Open the solution in Visual Studio:
   - VS 2022: `Build\NightbloomWorkspace.sln`
   - VS 2026: `Build\NightbloomWorkspace.slnx`

4. Set **Editor** as the startup project and run (F5)

### Creating a Distribution

To package a standalone release build:
```batch
call CreateDistribution.bat
```

The distributable files will be in the `Distribution` folder.

## Project Structure
```
Nightbloom/
├── NightBloom/
│   ├── Engine/           # Core engine library
│   │   ├── Core/         # Application, logging, input
│   │   ├── Math/         # Vec2/3/4, Mat44, Quaternion
│   │   └── Renderer/     # Vulkan backend, materials, models
│   ├── ThirdParty/       # Dependencies (ImGui, VMA, GLM, cgltf)
│   └── Build/            # Generated build files
├── Editor/
│   ├── Source/           # Editor application code
│   ├── Shaders/          # GLSL shaders (.vert, .frag, .comp)
│   └── Assets/           # Models, textures
└── Distribution/         # Packaged release builds (not in repo)
```

## Third-Party Libraries

- [Vulkan Memory Allocator (VMA)](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator)
- [Dear ImGui](https://github.com/ocornut/imgui)
- [GLM](https://github.com/g-truc/glm)
- [cgltf](https://github.com/jkuhlmann/cgltf)
- [stb_image](https://github.com/nothings/stb)

## License

MIT License — see [LICENSE](LICENSE) for details.

## Author

David Flores — [davidafloresgamedev.com](https://davidafloresgamedev.com)