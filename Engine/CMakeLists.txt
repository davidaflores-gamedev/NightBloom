#------------------------------------------------------------------------------
# Engine/CMakeLists.txt
#
# Build configuration for the Nightbloom Engine library
#------------------------------------------------------------------------------

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

# Create engine library
add_library(NightbloomEngine STATIC ${ENGINE_SOURCES})

# Create alias for cleaner usage
add_library(Nightbloom::Engine ALIAS NightbloomEngine)

# Set include directories
target_include_directories(NightbloomEngine 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Platform-specific sources and definitions
if(WIN32)
    target_compile_definitions(NightbloomEngine PUBLIC NIGHTBLOOM_PLATFORM_WINDOWS)
    target_compile_definitions(NightbloomEngine PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(UNIX AND NOT APPLE)
    target_compile_definitions(NightbloomEngine PUBLIC NIGHTBLOOM_PLATFORM_LINUX)
elseif(APPLE)
    target_compile_definitions(NightbloomEngine PUBLIC NIGHTBLOOM_PLATFORM_MACOS)
endif()

# Link libraries
target_link_libraries(NightbloomEngine
    PUBLIC
        # Future: Vulkan, etc.
    PRIVATE
        # Platform libraries
        $<$<PLATFORM_ID:Windows>:user32.lib>
        $<$<PLATFORM_ID:Windows>:gdi32.lib>
)

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