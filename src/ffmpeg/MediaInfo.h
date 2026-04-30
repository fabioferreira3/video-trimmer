#pragma once

#include <QString>
#include <QtTypes>

struct MediaInfo
{
    qint64 durationMs = 0;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    QString videoCodec;
    QString audioCodec;
    QString containerFormat;

    bool isValid() const { return durationMs > 0; }
};
