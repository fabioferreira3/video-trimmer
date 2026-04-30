#include "ffmpeg/FfprobeRunner.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStandardPaths>
#include <QStringList>

namespace {

double parseRational(const QString &rational)
{
    const auto parts = rational.split('/');
    if (parts.size() == 2) {
        bool numOk = false;
        bool denOk = false;
        const double num = parts[0].toDouble(&numOk);
        const double den = parts[1].toDouble(&denOk);
        if (numOk && denOk && den != 0.0) {
            return num / den;
        }
    }
    bool ok = false;
    const double v = rational.toDouble(&ok);
    return ok ? v : 0.0;
}

}  // namespace

FfprobeRunner::FfprobeRunner(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    connect(m_process, &QProcess::finished,
            this, &FfprobeRunner::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &FfprobeRunner::onErrorOccurred);
}

void FfprobeRunner::probe(const QString &filePath)
{
    if (isRunning()) {
        cancel();
    }

    const QString exe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (exe.isEmpty()) {
        emit failed(tr("ffprobe was not found on PATH. Install it (pacman -S ffmpeg) and try again."));
        return;
    }

    m_currentPath = filePath;
    const QStringList args = {
        QStringLiteral("-v"),            QStringLiteral("error"),
        QStringLiteral("-print_format"), QStringLiteral("json"),
        QStringLiteral("-show_format"),
        QStringLiteral("-show_streams"),
        filePath,
    };
    m_process->start(exe, args, QIODevice::ReadOnly);
}

void FfprobeRunner::cancel()
{
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

bool FfprobeRunner::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

void FfprobeRunner::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString stderrTail =
            QString::fromUtf8(m_process->readAllStandardError()).right(2048);
        emit failed(tr("ffprobe failed: %1")
                        .arg(stderrTail.isEmpty() ? tr("unknown error") : stderrTail));
        return;
    }

    bool ok = false;
    const MediaInfo info = parseJson(m_process->readAllStandardOutput(), &ok);
    if (!ok || !info.isValid()) {
        emit failed(tr("Could not parse ffprobe output for %1").arg(m_currentPath));
        return;
    }
    emit metadataReady(info);
}

void FfprobeRunner::onErrorOccurred(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        emit failed(tr("Failed to start ffprobe."));
    }
}

MediaInfo FfprobeRunner::parseJson(const QByteArray &json, bool *ok)
{
    if (ok) *ok = false;
    MediaInfo info;

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return info;
    }

    const QJsonObject root = doc.object();
    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    info.containerFormat = format.value(QStringLiteral("format_name")).toString();
    info.durationMs = static_cast<qint64>(
        format.value(QStringLiteral("duration")).toString().toDouble() * 1000.0);

    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    for (const QJsonValue &v : streams) {
        const QJsonObject s = v.toObject();
        const QString type = s.value(QStringLiteral("codec_type")).toString();
        if (type == QStringLiteral("video") && info.videoCodec.isEmpty()) {
            info.videoCodec = s.value(QStringLiteral("codec_name")).toString();
            info.width  = s.value(QStringLiteral("width")).toInt();
            info.height = s.value(QStringLiteral("height")).toInt();
            info.fps    = parseRational(s.value(QStringLiteral("r_frame_rate")).toString());
            // Some containers expose duration only at the stream level.
            if (info.durationMs <= 0) {
                const QString streamDur = s.value(QStringLiteral("duration")).toString();
                if (!streamDur.isEmpty()) {
                    info.durationMs = static_cast<qint64>(streamDur.toDouble() * 1000.0);
                }
            }
        } else if (type == QStringLiteral("audio") && info.audioCodec.isEmpty()) {
            info.audioCodec = s.value(QStringLiteral("codec_name")).toString();
        }
    }

    if (ok) *ok = true;
    return info;
}
