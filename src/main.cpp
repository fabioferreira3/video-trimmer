#include <QApplication>

#include "MainWindow.h"

int main(int argc, char *argv[])
{
    // Qt 6.10's native PipeWire audio backend silently drops any sink whose
    // node doesn't respond to SPA_PARAM_EnumFormat - which is exactly how
    // Bluetooth A2DP sinks behave (they only negotiate format once a stream
    // connects). The result is that BT headphones never appear in
    // QMediaDevices::audioOutputs(), so the user can't pin them in the
    // Audio menu. Force the older PulseAudio backend (which talks to
    // pipewire-pulse and lists every sink the daemon exposes) unless the
    // user has explicitly chosen a backend themselves.
    if (!qEnvironmentVariableIsSet("QT_AUDIO_BACKEND"))
        qputenv("QT_AUDIO_BACKEND", "pulseaudio");

    QApplication app(argc, argv);

    // Application/organization names drive QSettings storage paths; keep them
    // stable across renames so existing users don't lose their pinned audio
    // device, recent files, etc. The desktop file name is what compositors
    // (Wayland especially) use to associate the running window with the
    // installed .desktop entry — it must match the basename of vtrim.desktop.
    QApplication::setApplicationName("Video Trimmer");
    QApplication::setApplicationVersion("0.1.1");
    QApplication::setOrganizationName("video-trimmer");
    QApplication::setDesktopFileName("vtrim");

    MainWindow window;
    window.show();

    return app.exec();
}
