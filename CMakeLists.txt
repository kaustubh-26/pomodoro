cmake_minimum_required(VERSION 3.15)
project(Pomodoro VERSION 1.0.0 LANGUAGES CXX)

# Enforce C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Tell CMake where to find platform.h
include_directories(src)

# Define the three executables
add_executable(pomodoro src/pomodoro.cpp)
add_executable(pomo src/pomo.cpp)
add_executable(pomodoro_tray_indicator src/pomodoro_tray_indicator.cpp)

# Platform-specific configurations
if(WIN32)
    # Windows requires winmm for PlaySound, and shell32 for ShellExecute
    target_link_libraries(pomodoro PRIVATE winmm)
    target_link_libraries(pomo PRIVATE winmm)
    target_link_libraries(pomodoro_tray_indicator PRIVATE shell32 winmm)
else()
    # Linux requires GTK3 and AppIndicator for the tray
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
    pkg_check_modules(APPINDICATOR REQUIRED appindicator3-0.1)

    target_include_directories(pomodoro_tray_indicator PRIVATE 
        ${GTK3_INCLUDE_DIRS} 
        ${APPINDICATOR_INCLUDE_DIRS}
    )
    target_link_libraries(pomodoro_tray_indicator PRIVATE 
        ${GTK3_LIBRARIES} 
        ${APPINDICATOR_LIBRARIES}
    )
endif()