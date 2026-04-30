#pragma once

#include <QString>
#include <QtTypes>

namespace TimeFormat {

// Format a millisecond duration as "HH:MM:SS.mmm" (always millisecond-padded).
QString msToHms(qint64 ms);

// Format a millisecond duration as "HH:MM:SS.mmm" suitable for ffmpeg's -ss/-to.
QString msToFfmpegTimestamp(qint64 ms);

qint64 msToFrame(qint64 ms, double fps);
qint64 frameToMs(qint64 frame, double fps);

}  // namespace TimeFormat
