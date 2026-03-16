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
)

#------------------------------------------------------------------------------
# Copy assets to output directory
#------------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Assets")
    add_custom_command(TARGET NightbloomEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/Assets
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
# Shader Compilation
#------------------------------------------------------------------------------
find_program(GLSLC glslc HINTS 
    $ENV{VULKAN_SDK}/Bin 
    $ENV{VULKAN_SDK}/Bin32
)

if(NOT GLSLC)
    message(WARNING "glslc not found! Shaders won't be compiled automatically.")
else()
    message(STATUS "Found glslc: ${GLSLC}")
    
    file(GLOB SHADER_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.frag"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.comp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.geom"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.tesc"
        "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.tese"
    )
    
message(STATUS "Found shaders: ${SHADER_SOURCES}")

    if(SHADER_SOURCES)
        foreach(SHADER ${SHADER_SOURCES})
            get_filename_component(SHADER_NAME ${SHADER} NAME)
            add_custom_command(TARGET NightbloomEditor POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:NightbloomEditor>/Shaders"
                COMMAND ${GLSLC} "${SHADER}" -o "$<TARGET_FILE_DIR:NightbloomEditor>/Shaders/${SHADER_NAME}.spv"
                COMMENT "Compiling shader ${SHADER_NAME}"
                VERBATIM
            )
        endforeach()
    else()
        add_custom_command(TARGET NightbloomEditor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:NightbloomEditor>/Shaders"
        )
    endif()
endif()

# Group files in IDE
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${EDITOR_SOURCES})
