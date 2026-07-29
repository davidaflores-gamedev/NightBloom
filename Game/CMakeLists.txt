#------------------------------------------------------------------------------
# Game/CMakeLists.txt
#
# Standalone Nightbloom Game runtime (Milestone 2). Links the engine, no editor
# UI; boots a project's start scene. Built as part of the root CMakeLists.
#------------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.20)
project(NightbloomGame VERSION 0.1.0 LANGUAGES CXX)

file(GLOB_RECURSE GAME_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.hpp"
)

add_executable(NightbloomGame ${GAME_SOURCES})

set_target_properties(NightbloomGame PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:NightbloomGame>"
    OUTPUT_NAME "NightbloomGame"
)

if(WIN32)
    set_target_properties(NightbloomGame PROPERTIES
        LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE"
        LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
    )
endif()

target_link_libraries(NightbloomGame PRIVATE NightbloomEngine)

# Deploy the engine-owned shaders next to the game exe (same shared function the
# editor uses — this is the payoff of P4).
# See the note in Editor/CMakeLists.txt: the root includes this module, a
# standalone generate does not.
if(NOT COMMAND nightbloom_compile_shaders)
    include("${CMAKE_CURRENT_LIST_DIR}/../cmake/NightbloomShaders.cmake")
endif()

nightbloom_compile_shaders(NightbloomGame)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${GAME_SOURCES})
