#pragma once

#include <QString>
#include <QtTypes>

struct MediaInfo
{
    qint64 durationMs = 0;
    int width = 0;
    int height = 0;
    // fps comes from r_frame_rate: the LCM of all frame durations. For VFR
    // sources this can be wildly higher than perceived fps (e.g. 1000/1).
    double fps = 0.0;
    // avgFps comes from avg_frame_rate: total_frames / duration. For VFR
    // sources this is closer to the perceived rate.
    double avgFps = 0.0;
    QString videoCodec;
    QString audioCodec;
    QString containerFormat;

    // Populated from the first audio stream when present. Used so the status
    // bar can show meaningful "48 kHz / stereo" info for audio-only files
    // where width/height/fps are zero.
    int audioSampleRate = 0;
    int audioChannels   = 0;

    bool isValid() const { return durationMs > 0; }
    bool hasVideo() const { return width > 0 && height > 0; }
    bool hasAudio() const { return !audioCodec.isEmpty(); }
    // True for files that decode to audio only - drives audio-only UI mode
    // (no video pane, no FPS controls in the export dialog, etc.).
    bool isAudioOnly() const { return isValid() && !hasVideo() && hasAudio(); }
};
