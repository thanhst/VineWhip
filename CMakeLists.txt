cmake_minimum_required(VERSION 3.21)
project(WhipCPU VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "WhipCPU requires Windows and the Visual Studio C++ toolchain.")
endif()

enable_language(RC)
add_executable(WhipCPU WIN32
    src/main.cpp
    src/MouseInput.cpp
    src/Overlay.cpp
    src/Priority.cpp
    assets/WhipCPU.rc
)
target_compile_features(WhipCPU PRIVATE cxx_std_20)
target_compile_definitions(WhipCPU PRIVATE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    WINVER=0x0A00 _WIN32_WINNT=0x0A00
)
target_compile_options(WhipCPU PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:/W4>
    $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
)
target_include_directories(WhipCPU PRIVATE assets)
# The resource file supplies the application manifest.
target_link_options(WhipCPU PRIVATE /MANIFEST:NO)
target_link_libraries(WhipCPU PRIVATE user32 gdi32 gdiplus shell32 winmm)
