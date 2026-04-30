#pragma once

#include <QObject>
#include <QString>

#include "ffmpeg/MediaInfo.h"

class FfprobeRunner;

class VideoSession : public QObject
{
    Q_OBJECT

public:
    explicit VideoSession(QObject *parent = nullptr);

    bool isOpen() const { return !m_filePath.isEmpty(); }
    QString filePath() const { return m_filePath; }
    const MediaInfo &mediaInfo() const { return m_mediaInfo; }
    qint64 inMs() const { return m_inMs; }
    qint64 outMs() const { return m_outMs; }

public slots:
    void openFile(const QString &path);
    void close();
    void setIn(qint64 ms);
    void setOut(qint64 ms);
    void setInOut(qint64 inMs, qint64 outMs);

signals:
    void opened(const QString &path);
    void mediaInfoUpdated(const MediaInfo &info);
    void inOutChanged(qint64 inMs, qint64 outMs);
    void closed();
    void probeFailed(const QString &error);

private:
    QString m_filePath;
    MediaInfo m_mediaInfo;
    qint64 m_inMs = 0;
    qint64 m_outMs = 0;
    FfprobeRunner *m_probe;
};
