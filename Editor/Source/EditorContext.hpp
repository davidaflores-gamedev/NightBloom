// EditorContext.hpp
#pragma once

#include <string>
#include <filesystem>

namespace Nightbloom
{
    class Scene;
    class Renderer;

    struct EditorContext
    {
        Scene* scene = nullptr;
        Renderer* renderer = nullptr;

        // Project info (read-only from panels)
        const std::string* projectName = nullptr;
        const std::filesystem::path* projectPath = nullptr;
    };

} // namespace Nightbloom