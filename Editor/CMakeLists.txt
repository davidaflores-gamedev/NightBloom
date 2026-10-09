#------------------------------------------------------------------------------
# Editor/CMakeLists.txt
#
# Nightbloom Editor - Development tools and content creation
# NOTE: This is built as part of the root CMakeLists.txt, not standalone
#------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)
project(NightbloomEditor VERSION 0.1.0 LANGUAGES CXX)

# Gather source files
file(GLOB_RECURSE EDITOR_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp"
)

# Create executable
add_executable(NightbloomEditor ${EDITOR_SOURCES})

# Set working directory
set_target_properties(NightbloomEditor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:NightbloomEditor>"
)

# Set output name
set_target_properties(NightbloomEditor PROPERTIES OUTPUT_NAME "Editor")

if(WIN32)
    set_target_properties(NightbloomEditor PROPERTIES
        LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE"
        LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
    )
endif()

#------------------------------------------------------------------------------
# Link with engine - direct target reference (not find_package)
#------------------------------------------------------------------------------
target_link_libraries(NightbloomEditor
    PRIVATE
        NightbloomEngine
        $<$<PLATFORM_ID:Windows>:comdlg32.lib>   # native file Open/Save dialogs
)

# nlohmann/json single-header — the editor serializes panel state (DayNight) into
# the scene file's opaque "editor" blob. Engine links it PRIVATE, so it doesn't
# propagate here; add the include dir directly.
target_include_directories(NightbloomEditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../NightBloom/ThirdParty/nlohmann/single_include
)

#------------------------------------------------------------------------------
# Copy the repo-root Assets/ tree to the output directory. In a dev checkout
# AssetManager finds the repo root (via .git/Setup.bat) and reads Assets/ there
# directly; this copy is what a packaged editor (CreateDistribution.bat) uses.
#------------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../Assets")
    add_custom_command(TARGET NightbloomEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/../Assets
        $<TARGET_FILE_DIR:NightbloomEditor>/Assets
    )
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/EditorResources")
    add_custom_command(TARGET NightbloomEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/EditorResources
        $<TARGET_FILE_DIR:NightbloomEditor>/EditorResources
    )
endif()

#------------------------------------------------------------------------------
# Shader Compilation — deploys the engine-owned shaders (NightBloom/Shaders) next
# to the editor exe. Shared with the future Game runtime; see
# cmake/NightbloomShaders.cmake (P4 in .claude/RENDERER_ASSESSMENT.md).
#------------------------------------------------------------------------------
# The helper module is included by the ROOT CMakeLists, so a root build already
# has this command. Generating this target standalone (Editor/GenerateEditor.bat,
# and therefore BuildAll.bat) never runs the root, so without this guarded
# include the call below dies with 'Unknown CMake command'.
if(NOT COMMAND nightbloom_compile_shaders)
    include("${CMAKE_CURRENT_LIST_DIR}/../cmake/NightbloomShaders.cmake")
endif()

nightbloom_compile_shaders(NightbloomEditor)

# Group files in IDE
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EDITOR_SOURCES})
