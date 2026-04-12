// EditorUtils.cpp
#include "EditorUtils.hpp"

#include "Engine/Core/Scene.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Renderer/Components/ResourceManager.hpp"
#include "Engine/Renderer/DrawCommandSystem.hpp"
#include "Engine/Core/Logger/Logger.hpp"

namespace Nightbloom
{
    void ApplyPipelineToSelected(EditorContext& ctx, PipelineType pipeline)
    {
        if (!ctx.scene || !ctx.renderer) return;

        SceneObject* selected = ctx.scene->GetSelected();
        if (!selected || !selected->meshDrawable) return;

        auto* vb = ctx.renderer->GetTestVertexBuffer();
        auto* ib = ctx.renderer->GetTestIndexBuffer();
        uint32_t ic = ctx.renderer->GetTestIndexCount();

        if (!vb || !ib || ic == 0) return;

        ResourceManager* resources = ctx.renderer->GetResourceManager();

        auto newDrawable = std::make_unique<MeshDrawable>(vb, ib, ic, pipeline);
        newDrawable->SetTransform(selected->primitiveTransform);

        if (resources)
        {
            const char* textureLookup[] = {
                "uv_checker", "default_white", "default_black", "default_normal"
            };
            int texIdx = selected->textureIndex;
            if (texIdx >= 0 && texIdx < 4)
            {
                VulkanTexture* texture = resources->GetTexture(textureLookup[texIdx]);
                if (texture) newDrawable->AddTexture(texture);
            }
        }

        selected->pipeline = pipeline;
        selected->meshDrawable = std::move(newDrawable);
    }

} // namespace Nightbloom