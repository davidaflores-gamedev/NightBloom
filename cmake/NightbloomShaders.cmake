#------------------------------------------------------------------------------
# cmake/NightbloomShaders.cmake
#
# Reusable GLSL -> SPIR-V compilation for any Nightbloom executable. The engine's
# shaders are engine-owned (NightBloom/Shaders), NOT tied to the Editor — so the
# Editor (now) and the standalone Game runtime (Milestone 2) both deploy them by
# calling nightbloom_compile_shaders(<target>). This is the P4 fix from
# .claude/RENDERER_ASSESSMENT.md: previously the Editor target owned the shader
# compile step, so a standalone game exe would have had no shaders.
#------------------------------------------------------------------------------

find_program(NIGHTBLOOM_GLSLC glslc HINTS
    $ENV{VULKAN_SDK}/Bin
    $ENV{VULKAN_SDK}/Bin32
)

# Absolute path to the engine's shared shader sources.
set(NIGHTBLOOM_ENGINE_SHADER_DIR "${CMAKE_SOURCE_DIR}/NightBloom/Shaders"
    CACHE INTERNAL "Engine-owned GLSL shader sources")

# nightbloom_compile_shaders(<target> [SHADER_DIR <dir>])
#
# Adds POST_BUILD steps to <target> that compile every .vert/.frag/.comp/.geom/.tesc/
# .tese under SHADER_DIR (default: the engine shaders) to
# <target_output_dir>/Shaders/<name>.spv, with SHADER_DIR/Include on the include path.
# If glslc is missing, emits a warning and just creates the output Shaders/ folder
# (matches the previous inline behaviour).
function(nightbloom_compile_shaders TARGET_NAME)
    cmake_parse_arguments(ARG "" "SHADER_DIR" "" ${ARGN})
    if(NOT ARG_SHADER_DIR)
        set(ARG_SHADER_DIR "${NIGHTBLOOM_ENGINE_SHADER_DIR}")
    endif()

    # Always ensure the output Shaders/ directory exists.
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders"
    )

    if(NOT NIGHTBLOOM_GLSLC)
        message(WARNING "glslc not found! Shaders for ${TARGET_NAME} won't be compiled automatically.")
        return()
    endif()

    file(GLOB SHADER_SOURCES
        CONFIGURE_DEPENDS
        "${ARG_SHADER_DIR}/*.vert"
        "${ARG_SHADER_DIR}/*.frag"
        "${ARG_SHADER_DIR}/*.comp"
        "${ARG_SHADER_DIR}/*.geom"
        "${ARG_SHADER_DIR}/*.tesc"
        "${ARG_SHADER_DIR}/*.tese"
    )

    foreach(SHADER ${SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${NIGHTBLOOM_GLSLC} "${SHADER}"
                    -I "${ARG_SHADER_DIR}/Include"
                    -o "$<TARGET_FILE_DIR:${TARGET_NAME}>/Shaders/${SHADER_NAME}.spv"
            COMMENT "Compiling shader ${SHADER_NAME} for ${TARGET_NAME}"
            VERBATIM
        )
    endforeach()
endfunction()
