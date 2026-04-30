#include "VideoSession.h"

#include "ffmpeg/FfprobeRunner.h"

#include <algorithm>

VideoSession::VideoSession(QObject *parent)
    : QObject(parent)
    , m_probe(new FfprobeRunner(this))
{
    connect(m_probe, &FfprobeRunner::metadataReady,
            this, [this](const MediaInfo &info) {
                m_mediaInfo = info;
                if (m_outMs == 0 || m_outMs > info.durationMs) {
                    m_outMs = info.durationMs;
                }
                emit mediaInfoUpdated(info);
                emit inOutChanged(m_inMs, m_outMs);
            });
    connect(m_probe, &FfprobeRunner::failed,
            this, &VideoSession::probeFailed);
}

void VideoSession::openFile(const QString &path)
{
    m_filePath = path;
    m_mediaInfo = MediaInfo{};
    m_inMs = 0;
    m_outMs = 0;
    emit opened(path);
    emit inOutChanged(m_inMs, m_outMs);
    m_probe->probe(path);
}

void VideoSession::close()
{
    m_filePath.clear();
    m_mediaInfo = MediaInfo{};
    m_inMs = 0;
    m_outMs = 0;
    emit closed();
}

void VideoSession::setIn(qint64 ms)
{
    if (m_mediaInfo.durationMs > 0) {
        ms = std::clamp<qint64>(ms, 0, m_mediaInfo.durationMs);
    } else {
        ms = std::max<qint64>(0, ms);
    }
    if (ms > m_outMs && m_outMs > 0) ms = m_outMs;
    if (ms == m_inMs) return;
    m_inMs = ms;
    emit inOutChanged(m_inMs, m_outMs);
}

void VideoSession::setOut(qint64 ms)
{
    if (m_mediaInfo.durationMs > 0) {
        ms = std::clamp<qint64>(ms, 0, m_mediaInfo.durationMs);
    } else {
        ms = std::max<qint64>(0, ms);
    }
    if (ms < m_inMs) ms = m_inMs;
    if (ms == m_outMs) return;
    m_outMs = ms;
    emit inOutChanged(m_inMs, m_outMs);
}

void VideoSession::setInOut(qint64 inMs, qint64 outMs)
{
    if (m_mediaInfo.durationMs > 0) {
        inMs  = std::clamp<qint64>(inMs,  0, m_mediaInfo.durationMs);
        outMs = std::clamp<qint64>(outMs, inMs, m_mediaInfo.durationMs);
    } else {
        inMs  = std::max<qint64>(0, inMs);
        outMs = std::max<qint64>(inMs, outMs);
    }
    if (inMs == m_inMs && outMs == m_outMs) return;
    m_inMs = inMs;
    m_outMs = outMs;
    emit inOutChanged(m_inMs, m_outMs);
}
