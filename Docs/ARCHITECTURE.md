\# Nightbloom Renderer



\## Goal: Beautiful Nighttime Environments

Not a general engine - a specialized night scene renderer for now.



\## Core Systems:

1\. SceneRenderer - High-level rendering interface

2\. RenderGraph - Frame sequencing (shadow → main → post)

3\. ResourceCache - Automatic GPU resource management

4\. EffectSystems - Specialized renderers (water, volumetrics, etc.)



\## Data Flow:

Scene Description → RenderGraph → Command Recording → GPU



\## Key Decisions:

\- Vulkan-only (no abstraction needed)

\- Forward+ rendering for transparency

\- Deferred for opaque geometry

\- Fixed descriptor set layout across pipelines

