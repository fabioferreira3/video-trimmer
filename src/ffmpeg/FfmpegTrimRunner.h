#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class FfmpegTrimRunner : public QObject
{
    Q_OBJECT

public:
    enum class Mode { Precise, Fast };

    // Output audio codec / container choices for the precise audio-only
    // pipeline. The runner picks both the encoder (`-c:a ...`) and the
    // expected file extension from this. Ignored when `audioOnly` is false
    // or the trim mode is Fast (stream copy preserves the source codec).
    enum class AudioFormat { Aac, Mp3, Wav, Flac, OggVorbis, Opus };

    explicit FfmpegTrimRunner(QObject *parent = nullptr);

    // fpsOverride > 0 forces a constant output frame rate via ffmpeg's -r.
    // It is honored only in Precise mode; Fast mode (stream copy) ignores it.
    // audioOnly switches the precise pipeline to audio-only encoding,
    // skipping libx264 / -r / +faststart that don't apply to audio files.
    // audioFormat picks the audio encoder/container for the precise audio-only
    // pipeline and is otherwise ignored.
    void start(const QString &input,
               const QString &output,
               qint64 inMs,
               qint64 outMs,
               Mode mode,
               double fpsOverride = 0.0,
               bool audioOnly = false,
               AudioFormat audioFormat = AudioFormat::Aac);
    void cancel();
    bool isRunning() const;

    // File extension (no leading dot) the runner expects for the given
    // audio format. Used by the export dialog to suggest output filenames.
    static QString audioFormatExtension(AudioFormat format);

signals:
    void started();
    void progress(double percent);
    void finished(bool ok, const QString &errorTail);

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onErrorOccurred(QProcess::ProcessError error);

private:
    QProcess  *m_process;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrTail;
    qint64     m_durationUs = 0;
    bool       m_cancelRequested = false;
};
