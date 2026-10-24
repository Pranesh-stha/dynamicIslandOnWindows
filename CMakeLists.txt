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

# Compiler settings shared by every target.
function(island_defaults target)
    target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(${target} PRIVATE
        UNICODE _UNICODE          # use the wide-character (W) Win32 functions
        WIN32_LEAN_AND_MEAN       # skip rarely used parts of <windows.h>
        NOMINMAX                  # stop <windows.h> defining min/max macros
        # Builds made from the repo read config/settings.json from here.
        ISLAND_CONFIG_DIR=L"${CMAKE_SOURCE_DIR}/config"
    )
    target_compile_options(${target} PRIVATE
        /W4           # high warning level
        /permissive-  # strict standard C++
        /utf-8        # source files are UTF-8
        /bigobj       # C++/WinRT headers produce large object files
    )
endfunction()

# Everything except the entry point, so the unit tests can use the same code.
add_library(IslandCore STATIC
    src/App.cpp
    src/IslandWindow.cpp
    src/render/IslandVisuals.cpp
    src/platform/Log.cpp
    src/platform/Settings.cpp
    src/platform/Tray.cpp
    src/services/FullscreenWatcher.cpp
    src/services/WindowClassifier.cpp
)
island_defaults(IslandCore)
target_link_libraries(IslandCore PUBLIC
    windowsapp      # WinRT functions that C++/WinRT calls into
    CoreMessaging   # CreateDispatcherQueueController
    dwmapi          # DwmSetWindowAttribute
    shcore          # GetDpiForMonitor
)

# WIN32 = a windowed app (entry point wWinMain, no console window).
add_executable(Island WIN32
    src/main.cpp
    src/app.manifest          # embedded into the exe: DPI awareness, Windows 10/11
)
island_defaults(Island)
target_link_libraries(Island PRIVATE IslandCore)

# Unit tests: a console program that checks the pure logic (no window needed).
# Run them with:  ctest --preset debug   (or release)
enable_testing()
add_executable(IslandTests tests/UnitTests.cpp)
island_defaults(IslandTests)
target_link_libraries(IslandTests PRIVATE IslandCore)
add_test(NAME unit COMMAND IslandTests)
