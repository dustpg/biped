cmake_minimum_required(VERSION 3.10)
project(biped_test)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Add executable
add_executable(biped_test
    main.cpp
    font_ft2.cpp
    graphics_d3d11.cpp
)

# Include directories
target_include_directories(biped_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}  # To find biped.h
)

# Windows-specific settings
if(WIN32)
    # Windows system libraries
    target_link_libraries(biped_test PRIVATE
        user32
        gdi32
        winmm
        dxguid
        d3d11
        d3dcompiler
    )
    
    # Windows-specific compile definitions
    target_compile_definitions(biped_test PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
endif()

# FreeType2 library
# Try multiple ways to find FreeType2
find_package(Freetype QUIET)
if(Freetype_FOUND)
    target_link_libraries(biped_test PRIVATE ${FREETYPE_LIBRARIES})
    target_include_directories(biped_test PRIVATE ${FREETYPE_INCLUDE_DIRS})
else()
    # Try using pkg-config (Linux/Mac)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FREETYPE2 QUIET freetype2)
        if(FREETYPE2_FOUND)
            target_link_libraries(biped_test PRIVATE ${FREETYPE2_LIBRARIES})
            target_include_directories(biped_test PRIVATE ${FREETYPE2_INCLUDE_DIRS})
        endif()
    endif()
    
    # If still not found, try manual search
    if(NOT FREETYPE2_FOUND AND NOT Freetype_FOUND)
        find_path(FREETYPE_INCLUDE_DIR ft2build.h
            PATHS
                ${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows/include
                ${CMAKE_SOURCE_DIR}/vcpkg_installed/x86-windows/include
                "D:/vcpkg/vcpkg/installed/x64-windows/include"
                "D:/vcpkg/vcpkg/installed/x86-windows/include"
                /usr/include
                /usr/local/include
        )
        find_library(FREETYPE_LIBRARY
            NAMES freetype freetype2
            PATHS
                ${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows/lib
                ${CMAKE_SOURCE_DIR}/vcpkg_installed/x86-windows/lib
                "D:/vcpkg/vcpkg/installed/x64-windows/lib"
                "D:/vcpkg/vcpkg/installed/x86-windows/lib"
                /usr/lib
                /usr/local/lib
        )
        if(FREETYPE_INCLUDE_DIR AND FREETYPE_LIBRARY)
            target_include_directories(biped_test PRIVATE ${FREETYPE_INCLUDE_DIR})
            target_link_libraries(biped_test PRIVATE ${FREETYPE_LIBRARY})
            message(STATUS "Found FreeType2: ${FREETYPE_INCLUDE_DIR}")
        else()
            message(WARNING "FreeType2 not found. Please install it or set FREETYPE_INCLUDE_DIR and FREETYPE_LIBRARY manually.")
        endif()
    endif()
endif()

# Compile options
if(MSVC)
    target_compile_options(biped_test PRIVATE
        /utf-8
        /W4
        /permissive-
    )
else()
    target_compile_options(biped_test PRIVATE
        -Wall
        -Wextra
    )
endif()

# Debug configuration
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(biped_test PRIVATE
        _DEBUG
        BIPED_DEBUG_OUTPUT
    )
endif()

# Release configuration
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    target_compile_definitions(biped_test PRIVATE
        NDEBUG
    )
endif()

