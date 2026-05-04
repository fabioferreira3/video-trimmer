#pragma once

#include <QDialog>
#include <QString>

#include "ffmpeg/FfmpegTrimRunner.h"
#include "ffmpeg/MediaInfo.h"

class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QRadioButton;

class TrimDialog : public QDialog
{
    Q_OBJECT

public:
    TrimDialog(const QString &sourcePath,
               const MediaInfo &info,
               qint64 inMs,
               qint64 outMs,
               QWidget *parent = nullptr);

    QString outputPath() const;
    FfmpegTrimRunner::Mode mode() const;
    // 0.0 == "Keep source" (no -r flag).
    double fpsOverride() const;
    bool audioOnly() const { return m_audioOnly; }
    // Honored only when audioOnly() is true and the precise mode is chosen.
    FfmpegTrimRunner::AudioFormat audioFormat() const;

private slots:
    void onBrowseClicked();
    void onModeChanged();
    void onAudioFormatChanged();
    void onAcceptRequested();

private:
    void updateFpsHint();
    // Recompute the suggested output filename based on the current mode +
    // audio format. If the line edit still holds the previously-suggested
    // default, replace it; otherwise leave the user's edit alone.
    void refreshOutputDefault();

    QString   m_sourcePath;
    MediaInfo m_info;
    qint64    m_inMs;
    qint64    m_outMs;
    bool      m_audioOnly = false;
    // Tracks the last value we wrote into m_outputEdit ourselves, so we can
    // tell user edits apart from "still on auto" when mode/format changes.
    QString   m_lastAutoOutput;

    QLineEdit    *m_outputEdit        = nullptr;
    QRadioButton *m_radioPrecise      = nullptr;
    QRadioButton *m_radioFast         = nullptr;
    QComboBox    *m_fpsCombo          = nullptr;
    QLabel       *m_fpsHint           = nullptr;
    QGroupBox    *m_fpsBox            = nullptr;
    QComboBox    *m_audioFormatCombo  = nullptr;
    QGroupBox    *m_audioFormatBox    = nullptr;
};
