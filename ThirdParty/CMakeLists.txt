#------------------------------------------------------------------------------
# ThirdParty/CMakeLists.txt
#
# Configuration for third-party dependencies
#------------------------------------------------------------------------------

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

# Future third-party libraries can be added here
# Examples:
# - Vulkan (find_package)
# - stb_image
# - ImGui
# - glm