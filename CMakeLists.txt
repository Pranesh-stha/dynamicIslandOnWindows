cmake_minimum_required(VERSION 3.28)
project(Island VERSION 0.0.1 LANGUAGES CXX)

# C++/WinRT only works with the Microsoft compiler. MinGW g++ is also on this
# machine's PATH, so fail loudly instead of producing confusing errors.
if(NOT MSVC)
    message(FATAL_ERROR
        "Island must be built with MSVC (Visual Studio Build Tools).\n"
        "Use the preset:  cmake --preset default")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Link the C++ runtime into the exe, so Island.exe runs on its own without
# needing the separate "Visual C++ Redistributable" DLLs.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# WIN32 = a windowed app (entry point wWinMain, no console window).
add_executable(Island WIN32
    src/main.cpp
)

target_compile_definitions(Island PRIVATE
    UNICODE _UNICODE          # use the wide-character (W) Win32 functions
    WIN32_LEAN_AND_MEAN       # skip rarely used parts of <windows.h>
    NOMINMAX                  # stop <windows.h> defining min/max macros
)

target_compile_options(Island PRIVATE
    /W4           # high warning level
    /permissive-  # strict standard C++
    /utf-8        # source files are UTF-8
    /bigobj       # C++/WinRT headers produce large object files
)

# windowsapp.lib provides the WinRT functions that C++/WinRT calls into.
target_link_libraries(Island PRIVATE windowsapp)
