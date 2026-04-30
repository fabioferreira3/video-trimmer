#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class FfmpegTrimRunner : public QObject
{
    Q_OBJECT

public:
    enum class Mode { Precise, Fast };

    explicit FfmpegTrimRunner(QObject *parent = nullptr);

    void start(const QString &input,
               const QString &output,
               qint64 inMs,
               qint64 outMs,
               Mode mode);
    void cancel();
    bool isRunning() const;

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
