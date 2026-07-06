#------------------------------------------------------------------------------
# Engine/CMakeLists.txt
#
# Build configuration for the Nightbloom Engine library
#------------------------------------------------------------------------------

# Manual Vulkan setup (ADD THIS SECTION AT THE TOP)
if(APPLE)
    # Homebrew Vulkan: vulkan-headers + vulkan-loader, with MoltenVK as the ICD.
    find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h
        HINTS /opt/homebrew/include /usr/local/include)
    find_library(VULKAN_LIBRARY NAMES vulkan
        HINTS /opt/homebrew/lib /usr/local/lib)
    if(NOT VULKAN_INCLUDE_DIR OR NOT VULKAN_LIBRARY)
        message(FATAL_ERROR
            "Vulkan not found — brew install vulkan-headers vulkan-loader molten-vk")
    endif()
    # GLFW provides the macOS window + Vulkan surface (see Window/Platform/GLFW).
    find_package(glfw3 REQUIRED
        PATHS /opt/homebrew/lib/cmake/glfw3 /usr/local/lib/cmake/glfw3)
else()
    set(VULKAN_SDK_PATH "$ENV{VULKAN_SDK}")
    set(VULKAN_INCLUDE_DIR "${VULKAN_SDK_PATH}/Include")
    set(VULKAN_LIBRARY "${VULKAN_SDK_PATH}/Lib/vulkan-1.lib")

    # Verify Vulkan files exist
    if(NOT EXISTS ${VULKAN_INCLUDE_DIR}/vulkan/vulkan.h)
        message(FATAL_ERROR "Vulkan headers not found at: ${VULKAN_INCLUDE_DIR}")
    endif()

    if(NOT EXISTS ${VULKAN_LIBRARY})
        message(FATAL_ERROR "Vulkan library not found at: ${VULKAN_LIBRARY}")
    endif()
endif()

message(STATUS "Using manual Vulkan setup:")
message(STATUS "  Include: ${VULKAN_INCLUDE_DIR}")
message(STATUS "  Library: ${VULKAN_LIBRARY}")

# Gather source files
file(GLOB_RECURSE ENGINE_SOURCES 
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)
# Exclude test files from main library
file(GLOB_RECURSE TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp"
)
list(REMOVE_ITEM ENGINE_SOURCES ${TEST_SOURCES})
# The Win32 window backend can't compile off-Windows (the GLFW backend is
# internally #ifdef-guarded instead, so it needs no exclusion on Windows).
# Engine/Math is likewise SSE/__declspec-bound (x86+MSVC); nothing outside its
# own tests consumes it — the engine proper uses glm.
if(NOT WIN32)
    file(GLOB_RECURSE WIN32_PLATFORM_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Window/Platform/Win32/*")
    file(GLOB_RECURSE SSE_MATH_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Math/*")
    if(WIN32_PLATFORM_SOURCES)
        list(REMOVE_ITEM ENGINE_SOURCES ${WIN32_PLATFORM_SOURCES})
    endif()
    if(SSE_MATH_SOURCES)
        list(REMOVE_ITEM ENGINE_SOURCES ${SSE_MATH_SOURCES})
    endif()
endif()
# Create engine library
add_library(NightbloomEngine STATIC ${ENGINE_SOURCES})
# Create alias for cleaner usage
add_library(Nightbloom::Engine ALIAS NightbloomEngine)

# Set include directories (UPDATED - ADD VULKAN)
target_include_directories(NightbloomEngine 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<INSTALL_INTERFACE:include>
        ${VULKAN_INCLUDE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/glm
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        # nlohmann/json single-header (header-only) — engine-internal (SceneSerializer).
        # Added as a private include dir rather than a linked target so it stays out of
        # NightbloomEngine's export set (export() rejects non-exported link deps).
        ${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/nlohmann/single_include
)

# Platform-specific sources and definitions
if(WIN32)
    target_compile_definitions(NightbloomEngine PUBLIC NIGHTBLOOM_PLATFORM_WINDOWS)
    target_compile_definitions(NightbloomEngine PRIVATE *CRT*SECURE_NO_WARNINGS)
elseif(UNIX AND NOT APPLE)
    target_compile_definitions(NightbloomEngine PUBLIC NIGHTBLOOM_PLATFORM_LINUX)
elseif(APPLE)
    target_compile_definitions(NightbloomEngine PUBLIC NIGHTBLOOM_PLATFORM_MACOS)
endif()

# Link libraries (UPDATED - ADD VULKAN)
target_link_libraries(NightbloomEngine
    PUBLIC
        ${VULKAN_LIBRARY}
	VMA
	ImGui
    PRIVATE
        # Platform libraries
        $<$<PLATFORM_ID:Windows>:user32.lib>
        $<$<PLATFORM_ID:Windows>:gdi32.lib>
)
if(APPLE)
    # Window backend + Vulkan surface creation (GLFWWindow, VulkanSwapchain)
    target_link_libraries(NightbloomEngine PRIVATE glfw)
endif()

# Precompiled headers
# target_precompile_headers(NightbloomEngine PRIVATE
#     <vector>
#     <string>
#     <memory>
#     <functional>
#     <algorithm>
#     <cstdint>
# )
# Group files in IDE

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/.. PREFIX "Engine" FILES ${ENGINE_SOURCES})

# Build tests if enabled
if(NIGHTBLOOM_BUILD_TESTS AND TEST_SOURCES)
    add_executable(NightbloomTests ${TEST_SOURCES})
    
    target_link_libraries(NightbloomTests
        PRIVATE
            NightbloomEngine
            GTest::gtest_main
    )
    
    # Add include directories for tests
    target_include_directories(NightbloomTests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
    
    # Discover tests
    include(GoogleTest)
    gtest_discover_tests(NightbloomTests)
    
    # Set folder in IDE
    set_target_properties(NightbloomTests PROPERTIES FOLDER "Tests")
endif()