#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

#include "ffmpeg/MediaInfo.h"

class FfprobeRunner : public QObject
{
    Q_OBJECT

public:
    explicit FfprobeRunner(QObject *parent = nullptr);

    void probe(const QString &filePath);
    void cancel();
    bool isRunning() const;

signals:
    void metadataReady(const MediaInfo &info);
    void failed(const QString &error);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onErrorOccurred(QProcess::ProcessError error);

private:
    QProcess *m_process;
    QString m_currentPath;

    static MediaInfo parseJson(const QByteArray &json, bool *ok);
};
