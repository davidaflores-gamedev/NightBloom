# NightBloom (engine library)

The engine static library (`NightbloomEngine`): a Vulkan renderer, environment systems, gameplay foundation, and unit tests. It is focused on advanced real-time rendering techniques and is used as the base for several game prototypes.

## Building

Build from the **repository root** with `BuildAll.bat`. That builds the engine together with the editor, the game runtime, and the tests into the single `Build/` tree. See the root `README.txt`.

`Scripts/GenerateProject.bat` still generates an engine-only solution into `NightBloom/Build/`, but it builds neither the editor nor the game. It's normally not what you want.

## Feature status

- [x] Window creation (Win32 only for now)
- [x] Vulkan renderer (CSM shadows, HDR + bloom, planar reflections)
- [x] Volumetric clouds
- [~] Physics: raycasts and line of sight only; no rigid bodies yet
