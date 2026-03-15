#------------------------------------------------------------------------------
# ThirdParty/CMakeLists.txt
#
# Configuration for third-party dependencies
#------------------------------------------------------------------------------

set(VULKAN_SDK_PATH "C:/VulkanSDK/1.4.321.1")
set(VULKAN_INCLUDE_DIR "${VULKAN_SDK_PATH}/Include")
set(VULKAN_LIBRARY "${VULKAN_SDK_PATH}/Lib/vulkan-1.lib")

# Google Test
if(NIGHTBLOOM_BUILD_TESTS)
    # Check if googletest exists
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/googletest/CMakeLists.txt")
        # Prevent overriding the parent project's compiler/linker settings
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        
        # Don't build GMock
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        
        # Don't install gtest
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        
        add_subdirectory(googletest)
        
        # Hide gtest targets in IDE
        set_target_properties(gtest gtest_main PROPERTIES FOLDER "ThirdParty/GoogleTest")
    else()
        message(WARNING "GoogleTest not found. Please run: git submodule add https://github.com/google/googletest.git ThirdParty/googletest")
    endif()
endif()

# VMA (Vulkan Memory Allocator) - Header-only library
add_library(VMA INTERFACE)
target_include_directories(VMA INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/VMA>
    $<INSTALL_INTERFACE:include/VMA>
)

# ImGui - Check if the directory exists first
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
if(EXISTS ${IMGUI_DIR})
    # Check for missing ImGui internal headers (they're in the main imgui folder)
    if(NOT EXISTS "${IMGUI_DIR}/imstb_rectpack.h")
        message(WARNING "ImGui internal headers missing. Make sure you copied ALL files from imgui/ folder")
    endif()
    
    add_library(ImGui STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
        ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    )

    # Include directories for ImGui
    target_include_directories(ImGui PUBLIC 
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${VULKAN_INCLUDE_DIR}  # This is the key fix - ImGui needs to find vulkan/vulkan.h
    )
    
    # Link Vulkan library
    target_link_libraries(ImGui PUBLIC ${VULKAN_LIBRARY})
    
    # For Windows
    target_compile_definitions(ImGui PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    
    message(STATUS "ImGui configured with Vulkan at: ${VULKAN_INCLUDE_DIR}")
else()
    message(WARNING "ImGui directory not found at ${IMGUI_DIR}")
endif()

# Future third-party libraries can be added here
# Examples:
# - Vulkan (find_package)
# - stb_image
# - ImGui
# - glm