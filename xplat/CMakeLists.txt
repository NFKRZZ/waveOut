cmake_minimum_required(VERSION 3.24)

project(waveout_xplat LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WAVEOUT_XPLAT_ENABLE_IMGUI "Enable ImGui layer hooks (implementation scaffold)." ON)
option(WAVEOUT_XPLAT_WARNINGS_AS_ERRORS "Treat warnings as errors." OFF)

find_package(SDL3 CONFIG REQUIRED)
find_package(bgfx CONFIG REQUIRED)

set(SDL3_TARGET "")
if(TARGET SDL3::SDL3)
    set(SDL3_TARGET SDL3::SDL3)
elseif(TARGET SDL3::SDL3-shared)
    set(SDL3_TARGET SDL3::SDL3-shared)
elseif(TARGET SDL3::SDL3-static)
    set(SDL3_TARGET SDL3::SDL3-static)
else()
    message(FATAL_ERROR "SDL3 package found, but no known SDL3 target is available.")
endif()

set(BGFX_TARGET "")
if(TARGET bgfx::bgfx)
    set(BGFX_TARGET bgfx::bgfx)
elseif(TARGET unofficial::bgfx::bgfx)
    set(BGFX_TARGET unofficial::bgfx::bgfx)
else()
    message(FATAL_ERROR "bgfx package found, but no known bgfx target is available.")
endif()

add_executable(waveout_xplat
    src/main.cpp
    src/App.cpp
    src/FrameStats.cpp
    src/BgfxRenderer.cpp
    src/scene/WaveScene.cpp
    src/ui/ImGuiLayer.cpp
    src/MiniaudioDecodeImpl.cpp
    ../AudioFileLoader.cpp
)

target_include_directories(waveout_xplat
    PRIVATE
        src
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(waveout_xplat
    PRIVATE
        ${SDL3_TARGET}
        ${BGFX_TARGET}
)

if(WIN32)
    target_link_libraries(waveout_xplat PRIVATE user32 gdi32 shell32)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(waveout_xplat PRIVATE dl m pthread)
endif()

if(WAVEOUT_XPLAT_ENABLE_IMGUI)
    target_compile_definitions(waveout_xplat PRIVATE WAVEOUT_XPLAT_ENABLE_IMGUI=1)
else()
    target_compile_definitions(waveout_xplat PRIVATE WAVEOUT_XPLAT_ENABLE_IMGUI=0)
endif()

target_compile_definitions(waveout_xplat PRIVATE WAVEOUT_XPLAT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
    target_compile_options(waveout_xplat PRIVATE /W4 /permissive-)
    if(WAVEOUT_XPLAT_WARNINGS_AS_ERRORS)
        target_compile_options(waveout_xplat PRIVATE /WX)
    endif()
else()
    target_compile_options(waveout_xplat PRIVATE -Wall -Wextra -Wpedantic)
    if(WAVEOUT_XPLAT_WARNINGS_AS_ERRORS)
        target_compile_options(waveout_xplat PRIVATE -Werror)
    endif()
endif()
