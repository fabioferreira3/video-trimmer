#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

// Removes the [inMs, outMs] segment from a media file by stitching the
// surviving "before" and "after" parts back together with stream copy
// (ffmpeg's concat demuxer + `-c copy`). This keeps the operation
// near-instant and lossless across iterations - matching what Crop already
// does for keep-segment trims - so the user can chain many cuts on the same
// working clip without quality degradation.
//
// Pipeline (all phases use `-c copy`, so they each take milliseconds even
// for large source files):
//
//   1. Extract pre-segment   [0, inMs)            -> pre-XXXXXX.<ext>
//   2. Extract post-segment  [outMs, durationMs)  -> post-XXXXXX.<ext>
//   3. Concat the two parts via the concat demuxer -> the caller's output
//
// Special cases:
//   - inMs == 0           : pre-segment is empty; just copy [outMs, end] out.
//   - outMs >= durationMs : post-segment is empty; just copy [0, in) out.
//   - both                : nothing would survive; the caller is expected to
//                           guard against this and we refuse the start anyway.
//
// Caveat (same one Crop has): `-c copy` snaps boundaries to the nearest
// preceding keyframe of the source, so the join may include a few extra
// frames before / after the requested marks. Users who need ms-accurate
// boundaries can re-export through the standard Export pipeline at the end.
class FfmpegCutRunner : public QObject
{
    Q_OBJECT

public:
    explicit FfmpegCutRunner(QObject *parent = nullptr);
    ~FfmpegCutRunner() override;

    // Kicks off the cut. `output` must already be a unique path the caller
    // owns; we'll overwrite it. `durationMs` is the source clip's total
    // duration and is used to decide whether the post-segment is needed.
    // No-op if the runner is already busy.
    void start(const QString &input,
               const QString &output,
               qint64 inMs,
               qint64 outMs,
               qint64 durationMs);

    void cancel();
    bool isRunning() const;

signals:
    void started();
    // 0..100. Across the multi-phase pipeline we map each phase's intra-ffmpeg
    // progress proportionally to the duration that phase covers, so the bar
    // moves smoothly even though three separate ffmpeg processes run.
    void progress(double percent);
    void finished(bool ok, const QString &errorTail);

private slots:
    void onReadyReadStandardOutput();
    void onReadyReadStandardError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onErrorOccurred(QProcess::ProcessError error);

private:
    enum class Phase {
        Idle,
        ExtractPre,    // [0, inMs)            -> m_prePath
        ExtractPost,   // [outMs, durationMs)  -> m_postPath
        Concat,        // pre + post           -> m_outputPath
    };

    // Phase weights: the share of the final progress bar each phase fills.
    // Computed once at start() based on which phases are actually scheduled
    // for this cut (e.g. an in==0 cut skips ExtractPre entirely, so Concat
    // owns the bar past whatever ExtractPost contributed).
    struct PhaseWeight {
        Phase   phase;
        double  weight;       // 0..1, all weights sum to 1
        qint64  durationUs;   // total work in this phase, for intra-progress
    };

    void runNextPhase();
    void startPhase(Phase phase);
    void emitOverallProgress(double phaseFraction);
    void cleanupIntermediates();
    void finishWithError(const QString &errorTail);
    void finishOk();

    // Build the concat-demuxer list file referencing the surviving parts.
    // Returns true on success and writes the full list file path to *outPath.
    bool writeConcatListFile(QString *outPath, QString *errorMessage);

    QProcess  *m_process;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrTail;

    // Inputs captured at start().
    QString m_inputPath;
    QString m_outputPath;
    qint64  m_inMs       = 0;
    qint64  m_outMs      = 0;
    qint64  m_durationMs = 0;

    // Intermediate files we own and must clean up on finish/cancel.
    QString m_prePath;
    QString m_postPath;
    QString m_listPath;

    // Phase plan for this run. Built in start(), drained in runNextPhase().
    QList<PhaseWeight> m_phasePlan;
    int                m_phaseIndex = -1;

    // Cumulative weight already credited to overall progress (sum of weights
    // of phases that have completed). Used to translate the current phase's
    // intra-ffmpeg progress into the global 0..100 number we emit.
    double m_completedWeight = 0.0;

    Phase m_currentPhase     = Phase::Idle;
    bool  m_cancelRequested  = false;

    // Microsecond duration the current phase's ffmpeg invocation is
    // expected to cover. Reset at each phase start.
    qint64 m_currentPhaseUs = 0;
};
