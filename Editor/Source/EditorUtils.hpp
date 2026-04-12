// EditorUtils.hpp
#pragma once

#include "Engine/Renderer/PipelineInterface.hpp"
#include "EditorContext.hpp"

namespace Nightbloom
{
    // Re-creates the selected primitive's MeshDrawable with a new pipeline,
    // preserving its current transform and texture assignment.
    // Safe to call from any panel — does nothing if nothing is selected.
    void ApplyPipelineToSelected(EditorContext& ctx, PipelineType pipeline);

} // namespace Nightbloom