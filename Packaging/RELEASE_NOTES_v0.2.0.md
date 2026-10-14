## NightBloom Editor v0.2.0

A major update to **NightBloom**, a Vulkan / C++20 game engine and editor written from scratch. v0.1.0 (March 2026) was an early editor with a test scene; v0.2.0 adds the full environment and lighting stack, projects and scenes, and a much better first run. This release ships the **editor** for Windows.

**Download:** `NightBloomEditor-v0.2.0-win64.zip`. Unzip the whole folder and run `Editor.exe`.

### What's in it
- **Rendering:** 4-cascade shadow maps, an HDR pipeline (bloom, FXAA, exposure, ACES tonemapping), planar-reflection water, volumetric clouds raymarched in a compute pass, GPU boids fireflies, GPU-noise terrain with LOD and instanced grass, and a day/night cycle.
- **Editor:** projects and scenes saved as JSON, a scene hierarchy and inspector, and live panels for terrain, water, grass, clouds, fireflies, lighting and day/night.
- **First run:** creates a `Projects\Sample` project next to the exe and opens it.

### Controls
- Hold the **right mouse button** and use **W A S D** to fly, **Q / E** to move down / up, and **Shift** to move faster.
- The **View** menu opens and closes every panel.

### Requirements
- Windows 10 or 11, 64-bit.
- A GPU with up-to-date **Vulkan 1.2** drivers (current NVIDIA, AMD or Intel). Vulkan comes with the graphics driver.
- The Visual C++ runtime is bundled; nothing else needs installing.

### Known limitations
- Windows only.
- **The exe isn't code-signed,** so Windows SmartScreen may warn on first launch: choose **More info → Run anyway**.
- **There's no sky renderer yet:** the background is a solid color.
- **Clouds have no self-shadowing yet,** so dense cloud cover looks flat.
- **Environment settings aren't saved** in scene files yet: terrain, water, grass, clouds and fireflies reset to their defaults each launch.
- **The stealth prototype and Stolen Orbit aren't included.** The stealth prototype builds from source.

### Verifying the download
SHA-256 of `NightBloomEditor-v0.2.0-win64.zip`:
`AEC40B13212AC6984F35E3D610913DBF2703B4BB45F949C00DCA69B14D71A020`
