#include "ffmpeg/FfmpegCutRunner.h"

#include "util/TimeFormat.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextStream>
#include <QTimer>
#include <algorithm>

namespace {

// Resolve ffmpeg once per call. Returns an empty string if not on PATH; the
// caller is expected to surface that via finished(false, ...).
QString findFfmpeg()
{
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

// Build a unique temp-file path with the given prefix and extension.
// Uses QTemporaryFile only to reserve the name; we close it immediately and
// let ffmpeg overwrite. Returns an empty string on failure.
QString reserveTempPath(const QString &prefix, const QString &suffix)
{
    const QString templ = QDir::temp().filePath(
        QStringLiteral("%1-XXXXXX.%2").arg(prefix, suffix));
    QTemporaryFile f(templ);
    f.setAutoRemove(false);
    if (!f.open()) return {};
    const QString path = f.fileName();
    f.close();
    return path;
}

}  // namespace

FfmpegCutRunner::FfmpegCutRunner(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &FfmpegCutRunner::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &FfmpegCutRunner::onReadyReadStandardError);
    connect(m_process, &QProcess::finished,
            this, &FfmpegCutRunner::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &FfmpegCutRunner::onErrorOccurred);
}

FfmpegCutRunner::~FfmpegCutRunner()
{
    // Best-effort cleanup if we're torn down mid-run.
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    cleanupIntermediates();
}

bool FfmpegCutRunner::isRunning() const
{
    return m_currentPhase != Phase::Idle;
}

void FfmpegCutRunner::start(const QString &input,
                            const QString &output,
                            qint64 inMs,
                            qint64 outMs,
                            qint64 durationMs)
{
    if (isRunning()) return;

    if (findFfmpeg().isEmpty()) {
        emit finished(false, tr("ffmpeg was not found on PATH. Install it with: pacman -S ffmpeg"));
        return;
    }

    if (durationMs <= 0 || outMs <= inMs) {
        emit finished(false, tr("Invalid cut range."));
        return;
    }

    const bool keepPre  = inMs > 0;
    const bool keepPost = outMs < durationMs;
    if (!keepPre && !keepPost) {
        emit finished(false, tr("The cut would remove the entire clip."));
        return;
    }

    m_inputPath        = input;
    m_outputPath       = output;
    m_inMs             = inMs;
    m_outMs            = outMs;
    m_durationMs       = durationMs;
    m_cancelRequested  = false;
    m_completedWeight  = 0.0;
    m_phaseIndex       = -1;
    m_phasePlan.clear();
    m_prePath.clear();
    m_postPath.clear();
    m_listPath.clear();

    // Build the phase plan and weight each phase by the number of source
    // microseconds it processes, so the overall progress bar advances at
    // a roughly uniform rate regardless of which side of the cut is bigger.
    // Concat is stream-copy of the full output and gets a token weight - it
    // tends to finish almost instantly compared to the extracts.
    qint64 totalUs = 0;
    if (keepPre) {
        const qint64 us = inMs * 1000;
        m_phasePlan.append({ Phase::ExtractPre, 0.0, us });
        totalUs += us;
    }
    if (keepPost) {
        const qint64 us = (durationMs - outMs) * 1000;
        m_phasePlan.append({ Phase::ExtractPost, 0.0, us });
        totalUs += us;
    }
    // Concat is only meaningful when both halves survive; a single-half cut
    // can be served straight out of the lone extract phase below.
    const bool needsConcat = keepPre && keepPost;
    if (needsConcat) {
        // 5% of the bar is a generous budget for the stream-copy concat
        // pass, which is essentially file I/O bound. The remaining 95%
        // is split between the extracts in proportion to their durations.
        const double concatShare = 0.05;
        const double extractsShare = 1.0 - concatShare;
        for (auto &p : m_phasePlan) {
            p.weight = (totalUs > 0)
                           ? extractsShare * (double(p.durationUs) / double(totalUs))
                           : extractsShare / m_phasePlan.size();
        }
        m_phasePlan.append({ Phase::Concat, concatShare, 0 });
    } else {
        // Single phase fills the bar.
        m_phasePlan.first().weight = 1.0;
    }

    emit started();
    emit progress(0.0);
    runNextPhase();
}

void FfmpegCutRunner::cancel()
{
    if (!isRunning()) return;
    m_cancelRequested = true;
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        QTimer::singleShot(2000, m_process, [this]() {
            if (m_process->state() != QProcess::NotRunning) {
                m_process->kill();
            }
        });
    } else {
        // Cancelled in the gap between phases: synthesise the same finish
        // path the running case would take so the caller hears about it.
        finishWithError(tr("Cancelled by user."));
    }
}

void FfmpegCutRunner::runNextPhase()
{
    ++m_phaseIndex;
    if (m_phaseIndex >= m_phasePlan.size()) {
        finishOk();
        return;
    }
    if (m_cancelRequested) {
        finishWithError(tr("Cancelled by user."));
        return;
    }
    startPhase(m_phasePlan.at(m_phaseIndex).phase);
}

void FfmpegCutRunner::startPhase(Phase phase)
{
    m_currentPhase   = phase;
    m_currentPhaseUs = m_phasePlan.at(m_phaseIndex).durationUs;
    m_stdoutBuffer.clear();
    m_stderrTail.clear();

    const QString exe = findFfmpeg();
    // Source extension drives the muxer for every intermediate so we don't
    // have to second-guess what the source container is. Fall back to mkv
    // (the ffmpeg-friendliest "anything goes" container) if the input has
    // no suffix at all, e.g. some screen-capture pipes.
    const QString sourceExt = QFileInfo(m_inputPath).suffix();
    const QString suffix    = sourceExt.isEmpty() ? QStringLiteral("mkv") : sourceExt;

    QStringList args;

    switch (phase) {
    case Phase::ExtractPre: {
        m_prePath = reserveTempPath(QStringLiteral("vtrim-cut-pre"), suffix);
        if (m_prePath.isEmpty()) {
            finishWithError(tr("Could not create a temporary file for the pre-cut segment."));
            return;
        }
        // -ss 0 is explicit so the command line is self-documenting; ffmpeg
        // would otherwise default the same way.
        args << QStringLiteral("-hide_banner")
             << QStringLiteral("-y")
             << QStringLiteral("-nostdin")
             << QStringLiteral("-ss") << TimeFormat::msToFfmpegTimestamp(0)
             << QStringLiteral("-to") << TimeFormat::msToFfmpegTimestamp(m_inMs)
             << QStringLiteral("-i")  << m_inputPath
             << QStringLiteral("-map") << QStringLiteral("0")
             << QStringLiteral("-c") << QStringLiteral("copy")
             << QStringLiteral("-avoid_negative_ts") << QStringLiteral("make_zero")
             << QStringLiteral("-progress") << QStringLiteral("pipe:1")
             << QStringLiteral("-nostats");
        // If this is the only surviving half, write straight to the final
        // output instead of a temp file - saves an entirely no-op concat pass.
        const bool soleHalf = m_phasePlan.size() == 1;
        args << (soleHalf ? m_outputPath : m_prePath);
        break;
    }
    case Phase::ExtractPost: {
        m_postPath = reserveTempPath(QStringLiteral("vtrim-cut-post"), suffix);
        if (m_postPath.isEmpty()) {
            finishWithError(tr("Could not create a temporary file for the post-cut segment."));
            return;
        }
        // No -to: we want everything from outMs to EOF. Adding -to would
        // require knowing the source duration precisely, which we have but
        // letting ffmpeg run to EOF is both simpler and more robust if the
        // probed duration was a hair short.
        args << QStringLiteral("-hide_banner")
             << QStringLiteral("-y")
             << QStringLiteral("-nostdin")
             << QStringLiteral("-ss") << TimeFormat::msToFfmpegTimestamp(m_outMs)
             << QStringLiteral("-i")  << m_inputPath
             << QStringLiteral("-map") << QStringLiteral("0")
             << QStringLiteral("-c") << QStringLiteral("copy")
             << QStringLiteral("-avoid_negative_ts") << QStringLiteral("make_zero")
             << QStringLiteral("-progress") << QStringLiteral("pipe:1")
             << QStringLiteral("-nostats");
        const bool soleHalf = m_phasePlan.size() == 1;
        args << (soleHalf ? m_outputPath : m_postPath);
        break;
    }
    case Phase::Concat: {
        QString listError;
        if (!writeConcatListFile(&m_listPath, &listError)) {
            finishWithError(listError.isEmpty()
                                ? tr("Could not write the concat list file.")
                                : listError);
            return;
        }
        // -safe 0 is required because the list file references absolute
        // paths under /tmp; ffmpeg defaults to refusing those for safety.
        args << QStringLiteral("-hide_banner")
             << QStringLiteral("-y")
             << QStringLiteral("-nostdin")
             << QStringLiteral("-f")    << QStringLiteral("concat")
             << QStringLiteral("-safe") << QStringLiteral("0")
             << QStringLiteral("-i")    << m_listPath
             << QStringLiteral("-map")  << QStringLiteral("0")
             << QStringLiteral("-c")    << QStringLiteral("copy")
             << QStringLiteral("-progress") << QStringLiteral("pipe:1")
             << QStringLiteral("-nostats")
             << m_outputPath;
        break;
    }
    case Phase::Idle:
        // Should never happen - the plan only ever contains real phases.
        finishWithError(tr("Internal error: cut runner reached an invalid state."));
        return;
    }

    m_process->start(exe, args, QIODevice::ReadOnly);
}

void FfmpegCutRunner::emitOverallProgress(double phaseFraction)
{
    phaseFraction = std::clamp(phaseFraction, 0.0, 1.0);
    const double weight = m_phasePlan.at(m_phaseIndex).weight;
    const double overall = std::clamp(
        (m_completedWeight + weight * phaseFraction) * 100.0, 0.0, 100.0);
    emit progress(overall);
}

void FfmpegCutRunner::onReadyReadStandardOutput()
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

        // Both key names carry microseconds; ffmpeg renamed _ms -> _us a
        // few releases ago but kept the alias around for compatibility.
        if (key == "out_time_us" || key == "out_time_ms") {
            bool ok = false;
            const qint64 us = val.toLongLong(&ok);
            if (!ok) continue;
            if (m_currentPhaseUs > 0) {
                emitOverallProgress(double(us) / double(m_currentPhaseUs));
            }
        }
    }
}

void FfmpegCutRunner::onReadyReadStandardError()
{
    m_stderrTail.append(m_process->readAllStandardError());
    if (m_stderrTail.size() > 16 * 1024) {
        m_stderrTail = m_stderrTail.right(16 * 1024);
    }
}

void FfmpegCutRunner::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    const bool ok = exitStatus == QProcess::NormalExit
                 && exitCode == 0
                 && !m_cancelRequested;

    if (!ok) {
        QString tail = m_cancelRequested
                           ? tr("Cancelled by user.")
                           : QString::fromUtf8(m_stderrTail).right(2048);
        finishWithError(tail);
        return;
    }

    // Phase succeeded: credit its weight to the overall bar before moving on.
    m_completedWeight = std::min(1.0, m_completedWeight + m_phasePlan.at(m_phaseIndex).weight);
    emit progress(std::clamp(m_completedWeight * 100.0, 0.0, 100.0));
    runNextPhase();
}

void FfmpegCutRunner::onErrorOccurred(QProcess::ProcessError /*error*/)
{
    // Routed through onProcessFinished() so error reporting stays consistent.
}

bool FfmpegCutRunner::writeConcatListFile(QString *outPath, QString *errorMessage)
{
    const QString listTemplate = QDir::temp().filePath(
        QStringLiteral("vtrim-cut-list-XXXXXX.txt"));
    QTemporaryFile f(listTemplate);
    f.setAutoRemove(false);
    if (!f.open()) {
        if (errorMessage) *errorMessage = tr("Could not create the concat list file.");
        return false;
    }

    // The concat demuxer wants one `file '<path>'` line per input. Inner
    // single quotes are escaped per ffmpeg's documented rule: '\''.
    auto escaped = [](const QString &p) {
        QString out = p;
        out.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
        return out;
    };

    QTextStream ts(&f);
    if (!m_prePath.isEmpty()) {
        ts << "file '" << escaped(m_prePath) << "'\n";
    }
    if (!m_postPath.isEmpty()) {
        ts << "file '" << escaped(m_postPath) << "'\n";
    }
    ts.flush();
    f.close();

    if (outPath) *outPath = f.fileName();
    return true;
}

void FfmpegCutRunner::cleanupIntermediates()
{
    if (!m_prePath.isEmpty())  { QFile::remove(m_prePath);  m_prePath.clear();  }
    if (!m_postPath.isEmpty()) { QFile::remove(m_postPath); m_postPath.clear(); }
    if (!m_listPath.isEmpty()) { QFile::remove(m_listPath); m_listPath.clear(); }
}

void FfmpegCutRunner::finishWithError(const QString &errorTail)
{
    cleanupIntermediates();
    // The output file may be a partial - drop it so the caller never sees
    // a half-written cut on failure.
    if (!m_outputPath.isEmpty()) {
        QFile::remove(m_outputPath);
    }
    m_currentPhase    = Phase::Idle;
    m_phasePlan.clear();
    m_phaseIndex      = -1;
    m_completedWeight = 0.0;
    m_currentPhaseUs  = 0;
    emit finished(false, errorTail);
}

void FfmpegCutRunner::finishOk()
{
    cleanupIntermediates();
    m_currentPhase    = Phase::Idle;
    m_phasePlan.clear();
    m_phaseIndex      = -1;
    m_completedWeight = 0.0;
    m_currentPhaseUs  = 0;
    emit progress(100.0);
    emit finished(true, QString());
}
