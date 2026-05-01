cmake_minimum_required(VERSION 3.21)

# The project / binary / package is named `vtrim` because the obvious name
# `video-trimmer` is already taken by the GTK4 GNOME Circle app shipped in
# Arch's [extra] repo (apps.gnome.org/VideoTrimmer). Sharing a binary name
# would collide on /usr/bin/video-trimmer, so we deliberately use a short,
# distinct identifier here. The user-facing display name in the .desktop
# entry can still be friendlier ("Qt Video Trimmer").
project(vtrim
    VERSION 0.1.1
    DESCRIPTION "Millisecond-precision video trimmer built with Qt and FFmpeg"
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

qt_add_executable(vtrim
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
target_include_directories(vtrim PRIVATE src)

target_link_libraries(vtrim PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
)

target_compile_options(vtrim PRIVATE
    -Wall -Wextra -Wpedantic
)

# ---------------------------------------------------------------------------
# Install rules
#
# Defaults to a per-user install under ~/.local so no sudo is ever needed:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
#         -DCMAKE_INSTALL_PREFIX="$HOME/.local"
#   cmake --build build -j
#   cmake --install build
#
# For a system-wide install pass -DCMAKE_INSTALL_PREFIX=/usr/local and run the
# install step with sudo. The .desktop entry uses a stock freedesktop icon
# name (video-x-generic) so we don't have to ship our own artwork.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS vtrim
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Bake the absolute path to the binary into the .desktop's Exec= line.
# Desktop launchers (Walker, wofi, GNOME Activities, etc.) are exec'd from
# the systemd --user session, which usually does NOT have ~/.local/bin on
# PATH even when your interactive shell does - so a bare `Exec=vtrim`
# fails with "Command not found" for per-user installs. Using
# CMAKE_INSTALL_FULL_BINDIR makes the entry correct for every prefix.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/packaging/vtrim.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/vtrim.desktop
    @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/vtrim.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

# Ship the license file. Distro packagers (Arch's namcap in particular)
# expect MIT-style licenses to live under /usr/share/licenses/<pkgname>/
# because they are not in /usr/share/licenses/common/.
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/${PROJECT_NAME}
)
