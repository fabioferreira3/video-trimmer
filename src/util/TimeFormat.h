#pragma once

#include <QString>
#include <QtTypes>

namespace TimeFormat {

// Format a millisecond duration as "HH:MM:SS.mmm" (always millisecond-padded).
QString msToHms(qint64 ms);

// Format a millisecond duration as "HH:MM:SS.mmm" suitable for ffmpeg's -ss/-to.
QString msToFfmpegTimestamp(qint64 ms);

// Parse a flexible HH:MM:SS[.mmm] / MM:SS[.mmm] / SS[.mmm] string into
// milliseconds. Returns true on success and writes the result to *outMs;
// returns false (leaving *outMs untouched) if the input cannot be interpreted
// as a non-negative duration. Whitespace is trimmed; the fractional part is
// padded/truncated to milliseconds (e.g. ".5" -> 500 ms, ".1234" -> 123 ms).
bool parseHms(const QString &text, qint64 *outMs);

qint64 msToFrame(qint64 ms, double fps);
qint64 frameToMs(qint64 frame, double fps);

}  // namespace TimeFormat
