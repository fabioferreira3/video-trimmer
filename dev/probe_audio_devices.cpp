// Tiny diagnostic: prints what Qt's QMediaDevices sees as audio outputs,
// using the same APIs the app uses in MainWindow::rebuildAudioOutputMenu.
//
// Build (manual one-shot, no CMake target):
//   g++ -std=c++20 -fPIC dev/probe_audio_devices.cpp \
//       $(pkg-config --cflags --libs Qt6Core Qt6Multimedia) \
//       -o build/probe_audio_devices
// Run:
//   ./build/probe_audio_devices

#include <QAudioDevice>
#include <QCoreApplication>
#include <QMediaDevices>
#include <QObject>
#include <QTextStream>
#include <QTimer>

static void dump(const char *label)
{
    QTextStream out(stdout);
    const auto def = QMediaDevices::defaultAudioOutput();
    out << "[" << label << "] default: " << def.description()
        << "  id=" << QString::fromUtf8(def.id()) << "\n";
    const auto outputs = QMediaDevices::audioOutputs();
    out << "[" << label << "] audioOutputs() count=" << outputs.size() << "\n";
    for (const QAudioDevice &d : outputs) {
        out << "    - desc='" << d.description()
            << "'  id='"     << QString::fromUtf8(d.id())
            << "'  default=" << (d.isDefault() ? "yes" : "no")
            << "\n";
    }
    out.flush();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QMediaDevices md;

    dump("immediate");

    QObject::connect(&md, &QMediaDevices::audioOutputsChanged,
                     [] { dump("changed"); });

    QTimer::singleShot(2500, [] {
        dump("after-2500ms");
        QCoreApplication::quit();
    });

    return app.exec();
}
