#include "ffmpeg/FfmpegTrimRunner.h"

#include "util/TimeFormat.h"

#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <algorithm>

namespace {

// Encoder + bitrate/quality args for each audio output format. The picks
// here mirror what `ffmpeg` itself would pick by container, and target
// "good enough for everyone" defaults rather than maximum quality:
//   - lossy formats (aac/mp3/vorbis/opus) hover around 192 kb/s; opus is
//     intentionally lower since it's noticeably more efficient at the same
//     perceptual quality.
//   - wav and flac are lossless and take no bitrate flag; pcm_s16le is the
//     universally-supported 16-bit/sample WAV encoding.
QStringList audioCodecArgsFor(FfmpegTrimRunner::AudioFormat fmt)
{
    using AF = FfmpegTrimRunner::AudioFormat;
    switch (fmt) {
    case AF::Aac:
        return { QStringLiteral("-c:a"), QStringLiteral("aac"),
                 QStringLiteral("-b:a"), QStringLiteral("192k") };
    case AF::Mp3:
        return { QStringLiteral("-c:a"), QStringLiteral("libmp3lame"),
                 QStringLiteral("-b:a"), QStringLiteral("192k") };
    case AF::Wav:
        return { QStringLiteral("-c:a"), QStringLiteral("pcm_s16le") };
    case AF::Flac:
        return { QStringLiteral("-c:a"), QStringLiteral("flac") };
    case AF::OggVorbis:
        return { QStringLiteral("-c:a"), QStringLiteral("libvorbis"),
                 QStringLiteral("-q:a"), QStringLiteral("5") };
    case AF::Opus:
        return { QStringLiteral("-c:a"), QStringLiteral("libopus"),
                 QStringLiteral("-b:a"), QStringLiteral("128k") };
    }
    return { QStringLiteral("-c:a"), QStringLiteral("aac"),
             QStringLiteral("-b:a"), QStringLiteral("192k") };
}

}  // namespace

QString FfmpegTrimRunner::audioFormatExtension(AudioFormat format)
{
    switch (format) {
    case AudioFormat::Aac:       return QStringLiteral("m4a");
    case AudioFormat::Mp3:       return QStringLiteral("mp3");
    case AudioFormat::Wav:       return QStringLiteral("wav");
    case AudioFormat::Flac:      return QStringLiteral("flac");
    case AudioFormat::OggVorbis: return QStringLiteral("ogg");
    case AudioFormat::Opus:      return QStringLiteral("opus");
    }
    return QStringLiteral("m4a");
}

FfmpegTrimRunner::FfmpegTrimRunner(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &FfmpegTrimRunner::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &FfmpegTrimRunner::onReadyReadStandardError);
    connect(m_process, &QProcess::finished,
            this, &FfmpegTrimRunner::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &FfmpegTrimRunner::onErrorOccurred);
}

void FfmpegTrimRunner::start(const QString &input,
                             const QString &output,
                             qint64 inMs,
                             qint64 outMs,
                             Mode mode,
                             double fpsOverride,
                             bool audioOnly,
                             AudioFormat audioFormat,
                             bool dropAudio)
{
    if (isRunning()) return;

    // audioOnly + dropAudio would produce an output with neither stream; the
    // dialogs that drive this runner never combine them, but be defensive.
    if (audioOnly && dropAudio) {
        emit finished(false, tr("Cannot drop audio from an audio-only export."));
        return;
    }

    const QString exe = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (exe.isEmpty()) {
        emit finished(false, tr("ffmpeg was not found on PATH. Install it with: pacman -S ffmpeg"));
        return;
    }

    m_stdoutBuffer.clear();
    m_stderrTail.clear();
    m_cancelRequested = false;
    m_durationUs = std::max<qint64>(0, outMs - inMs) * 1000;

    QStringList args;
    args << QStringLiteral("-hide_banner")
         << QStringLiteral("-y")
         << QStringLiteral("-nostdin")
         // -ss before -i: fast seek; since FFmpeg 4.0 also frame-accurate when re-encoding.
         << QStringLiteral("-ss") << TimeFormat::msToFfmpegTimestamp(inMs)
         << QStringLiteral("-to") << TimeFormat::msToFfmpegTimestamp(outMs)
         << QStringLiteral("-i")  << input
         << QStringLiteral("-map") << QStringLiteral("0");

    if (mode == Mode::Precise) {
        if (audioOnly) {
            // No -c:v / -r / +faststart: the source has no video stream and
            // +faststart is only meaningful for MP4-family containers (which
            // is just one of the formats the user can now pick here).
            // The encoder + bitrate/quality args come from `audioFormat`.
            args << QStringLiteral("-vn");
            args << audioCodecArgsFor(audioFormat);
        } else {
            args << QStringLiteral("-c:v")     << QStringLiteral("libx264")
                 << QStringLiteral("-preset")  << QStringLiteral("medium")
                 << QStringLiteral("-crf")     << QStringLiteral("18");
            if (fpsOverride > 0.0) {
                // -r after -i forces a constant output frame rate, which both
                // normalizes pathological VFR timebases and satisfies upload
                // platforms that reject anything outside their supported range.
                args << QStringLiteral("-r")
                     << QString::number(fpsOverride, 'f', 6);
            }
            if (dropAudio) {
                // -an wins over any -c:a setting, but specifying both would be
                // ambiguous; skip the audio encoder args entirely instead.
                args << QStringLiteral("-an");
            } else {
                args << QStringLiteral("-c:a") << QStringLiteral("aac")
                     << QStringLiteral("-b:a") << QStringLiteral("192k");
            }
            args << QStringLiteral("-movflags") << QStringLiteral("+faststart");
        }
    } else {
        args << QStringLiteral("-c") << QStringLiteral("copy")
             << QStringLiteral("-avoid_negative_ts") << QStringLiteral("make_zero");
        if (dropAudio) {
            args << QStringLiteral("-an");
        }
    }

    args << QStringLiteral("-progress") << QStringLiteral("pipe:1")
         << QStringLiteral("-nostats")
         << output;

    m_process->start(exe, args, QIODevice::ReadOnly);
    emit started();
}

void FfmpegTrimRunner::cancel()
{
    if (!isRunning()) return;
    m_cancelRequested = true;
    m_process->terminate();
    QTimer::singleShot(2000, m_process, [this]() {
        if (m_process->state() != QProcess::NotRunning) {
            m_process->kill();
        }
    });
}

bool FfmpegTrimRunner::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

void FfmpegTrimRunner::onReadyReadStandardOutput()
{
    m_stdoutBuffer.append(m_process->readAllStandardOutput());

    int newline;
    while ((newline = m_stdoutBuffer.indexOf('\n')) != -1) {
        const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
        m_stdoutBuffer.remove(0, newline + 1);

        const int eq = line.indexOf('=');
        if (eq <= 0) continue;
        const QByteArray key = line.left(eq);
        const QByteArray val = line.mid(eq + 1);

        // Both "out_time_us" and "out_time_ms" carry microseconds in ffmpeg's progress output.
        if (key == "out_time_us" || key == "out_time_ms") {
            bool ok = false;
            const qint64 us = val.toLongLong(&ok);
            if (ok && m_durationUs > 0) {
                const double pct = std::clamp(double(us) / double(m_durationUs) * 100.0,
                                              0.0, 100.0);
                emit progress(pct);
            }
        }
    }
}

void FfmpegTrimRunner::onReadyReadStandardError()
{
    m_stderrTail.append(m_process->readAllStandardError());
    if (m_stderrTail.size() > 16 * 1024) {
        m_stderrTail = m_stderrTail.right(16 * 1024);
    }
}

void FfmpegTrimRunner::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    const bool ok = exitStatus == QProcess::NormalExit
                 && exitCode == 0
                 && !m_cancelRequested;

    QString tail;
    if (m_cancelRequested) {
        tail = tr("Cancelled by user.");
    } else {
        tail = QString::fromUtf8(m_stderrTail).right(2048);
    }
    emit finished(ok, tail);
}

void FfmpegTrimRunner::onErrorOccurred(QProcess::ProcessError /*error*/)
{
    // The process's finished() will surface the failure path consistently.
}
