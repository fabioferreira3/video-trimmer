cmake_minimum_required(VERSION 3.21)

project(video-trimmer
    VERSION 0.1.0
    DESCRIPTION "Simple video trimmer built with Qt and FFmpeg"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate compile_commands.json so clangd / your editor can index the project.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Widgets
    Multimedia
    MultimediaWidgets
)

# AUTOMOC scans headers for Q_OBJECT and runs Qt's moc on them automatically.
# AUTOUIC compiles .ui files; AUTORCC compiles .qrc resource files.
qt_standard_project_setup()

qt_add_executable(video-trimmer
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/TrimDialog.h
    src/TrimDialog.cpp
    src/VideoSession.h
    src/VideoSession.cpp
    src/player/PlayerWidget.h
    src/player/PlayerWidget.cpp
    src/player/TransportBar.h
    src/player/TransportBar.cpp
    src/timeline/TimelineWidget.h
    src/timeline/TimelineWidget.cpp
    src/ffmpeg/MediaInfo.h
    src/ffmpeg/FfprobeRunner.h
    src/ffmpeg/FfprobeRunner.cpp
    src/ffmpeg/FfmpegTrimRunner.h
    src/ffmpeg/FfmpegTrimRunner.cpp
    src/util/TimeFormat.h
    src/util/TimeFormat.cpp
)

# Lets us write `#include "player/PlayerWidget.h"` from anywhere in the source tree.
target_include_directories(video-trimmer PRIVATE src)

target_link_libraries(video-trimmer PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)

target_compile_options(video-trimmer PRIVATE
    -Wall -Wextra -Wpedantic
)
