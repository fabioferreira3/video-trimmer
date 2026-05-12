#include <QApplication>
#include <QCommandLineParser>

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

    // Accept zero-or-more files on the command line. This is what makes
    // `Open with → Video Trimmer` from Nautilus (and other file managers)
    // actually load the selected file: the .desktop entry's `Exec=vtrim %F`
    // expands to `vtrim /path/to/clip.mp4`, and we forward the positional
    // args to MainWindow which loads the first usable one.
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QApplication::translate("main",
            "Millisecond-precision video and audio trimmer."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("file"),
        QApplication::translate("main", "Media file to open."),
        QStringLiteral("[file]"));
    parser.process(app);

    MainWindow window;
    window.show();
    window.openFromCommandLine(parser.positionalArguments());

    return app.exec();
}
