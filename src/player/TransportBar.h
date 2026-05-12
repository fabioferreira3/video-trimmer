#pragma once

#include <QMediaPlayer>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;

class TransportBar : public QWidget
{
    Q_OBJECT

public:
    explicit TransportBar(QWidget *parent = nullptr);

public slots:
    void setPosition(qint64 ms);
    void setDuration(qint64 ms);
    void setPlaybackState(QMediaPlayer::PlaybackState state);
    void setControlsEnabled(bool on);
    void setLoopEnabled(bool on);
    // Sync the editable In/Out time fields with authoritative session state.
    void setInPosition(qint64 ms);
    void setOutPosition(qint64 ms);

signals:
    void playPauseClicked();
    // Emitted when the user clicks the "Play from Start" button: playback
    // should (re)start from the current In marker regardless of the current
    // player position or state. Distinct from playPauseClicked(), which is a
    // resume-from-where-we-paused toggle.
    void playFromStartClicked();
    void volumeChanged(float v);  // 0.0 - 1.0
    void setInClicked();
    void setOutClicked();
    void loopToggled(bool on);
    // Emitted when the user finishes editing the In/Out time fields with a
    // valid HH:MM:SS[.mmm] / MM:SS[.mmm] / SS[.mmm] string.
    void inTimeEdited(qint64 ms);
    void outTimeEdited(qint64 ms);
    // Emitted when the user picks a preset duration from the "Duration From
    // Start" dropdown. The receiver should set Out = In + durationMs.
    void durationFromStartPresetSelected(qint64 durationMs);
    // Emitted when the user picks a preset duration from the "Duration From
    // End" dropdown. The receiver should set In = Out - durationMs.
    void durationFromEndPresetSelected(qint64 durationMs);
    // Emitted when the user clicks the Crop button: trim the working clip
    // down to the current [In, Out] and continue editing from there.
    void cropClicked();
    // Emitted when the user clicks the Cut button: remove [In, Out] from
    // the working clip and stitch the rest back together. Inverse of Crop.
    void cutClicked();

private:
    // Which In/Out edge a duration preset is anchored to. "FromStart" means
    // the duration extends forward from In (Out = In + d); "FromEnd" means
    // the duration extends backward from Out (In = Out - d).
    enum class DurationAnchor { FromStart, FromEnd };

    void updateLabels();
    // Commit the contents of an In/Out QLineEdit: parse it, emit the
    // corresponding *Edited signal on success, and re-format the field to
    // the canonical HH:MM:SS.mmm representation (or roll it back on failure).
    void commitTimeEdit(QLineEdit *edit, qint64 currentMs, bool isIn);
    // Prompt the user for a duration in seconds via a modal dialog, and
    // emit the appropriate durationFrom*PresetSelected() signal on
    // confirmation. No-op on cancel.
    void promptCustomDurationAndEmit(DurationAnchor anchor);

    QPushButton *m_playPause;
    QPushButton *m_playFromStart;
    QPushButton *m_loop;
    QPushButton *m_setIn;
    QPushButton *m_setOut;
    QLabel      *m_inLabel;
    QLabel      *m_outLabel;
    QLineEdit   *m_inEdit;
    QLineEdit   *m_outEdit;
    QComboBox   *m_durationFromStartPreset;
    QComboBox   *m_durationFromEndPreset;
    QPushButton *m_customRange;
    QPushButton *m_crop;
    QPushButton *m_cut;
    QLabel      *m_currentTime;
    QLabel      *m_totalTime;
    QSlider     *m_volume;

    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    qint64 m_inMs       = 0;
    qint64 m_outMs      = 0;
};
