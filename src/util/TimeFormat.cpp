#include "util/TimeFormat.h"

#include <QChar>
#include <QRegularExpression>
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

bool parseHms(const QString &text, qint64 *outMs)
{
    if (!outMs) return false;

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return false;

    // Anchored regex: optional H:M:S / M:S prefix groups (each up to 2 digits
    // for M/S, but the leading group is unbounded), then a mandatory seconds
    // value, then an optional fractional part. We don't bake a millisecond
    // digit-count limit in - we'll pad/truncate after the fact.
    static const QRegularExpression re(
        QStringLiteral(R"(^(?:(\d+):)?(?:(\d{1,2}):)?(\d+)(?:\.(\d+))?$)"));
    const auto m = re.match(trimmed);
    if (!m.hasMatch()) return false;

    const QString g1 = m.captured(1);
    const QString g2 = m.captured(2);
    const QString g3 = m.captured(3);
    const QString gf = m.captured(4);

    qint64 hours = 0;
    qint64 minutes = 0;
    qint64 seconds = 0;
    bool ok = true;

    // Decide which groups represent hours / minutes / seconds based on how
    // many colon-separated parts the user typed.
    if (!g1.isEmpty() && !g2.isEmpty()) {
        hours   = g1.toLongLong(&ok); if (!ok) return false;
        minutes = g2.toLongLong(&ok); if (!ok) return false;
        seconds = g3.toLongLong(&ok); if (!ok) return false;
    } else if (!g1.isEmpty()) {
        minutes = g1.toLongLong(&ok); if (!ok) return false;
        seconds = g3.toLongLong(&ok); if (!ok) return false;
    } else {
        seconds = g3.toLongLong(&ok); if (!ok) return false;
    }

    if (hours < 0 || minutes < 0 || seconds < 0) return false;
    // We deliberately allow minutes/seconds >= 60 so a value like "90" parses
    // as 90 seconds; this matches how most CLI tools accept duration input.

    qint64 millis = 0;
    if (!gf.isEmpty()) {
        QString frac = gf;
        if (frac.size() < 3) frac = frac.leftJustified(3, QChar('0'));
        else if (frac.size() > 3) frac = frac.left(3);
        millis = frac.toLongLong(&ok);
        if (!ok || millis < 0) return false;
    }

    const qint64 total = hours * 3'600'000 + minutes * 60'000 + seconds * 1'000 + millis;
    *outMs = total;
    return true;
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
