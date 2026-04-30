#include "util/TimeFormat.h"

#include <QChar>
#include <cmath>

namespace TimeFormat {

QString msToHms(qint64 ms)
{
    if (ms < 0) ms = 0;
    const qint64 hours   = ms / 3'600'000;
    const qint64 minutes = (ms / 60'000) % 60;
    const qint64 seconds = (ms / 1'000) % 60;
    const qint64 millis  = ms % 1'000;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(hours,   2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(millis,  3, 10, QChar('0'));
}

QString msToFfmpegTimestamp(qint64 ms)
{
    return msToHms(ms);
}

qint64 msToFrame(qint64 ms, double fps)
{
    if (fps <= 0.0) return 0;
    return static_cast<qint64>(std::llround(ms * fps / 1000.0));
}

qint64 frameToMs(qint64 frame, double fps)
{
    if (fps <= 0.0) return 0;
    return static_cast<qint64>(std::llround(frame * 1000.0 / fps));
}

}  // namespace TimeFormat
