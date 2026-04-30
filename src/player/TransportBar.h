#pragma once

#include <QMediaPlayer>
#include <QWidget>

class QLabel;
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

signals:
    void playPauseClicked();
    void volumeChanged(float v);  // 0.0 - 1.0
    void setInClicked();
    void setOutClicked();
    void loopToggled(bool on);

private:
    void updateLabels();

    QPushButton *m_playPause;
    QPushButton *m_loop;
    QPushButton *m_setIn;
    QPushButton *m_setOut;
    QLabel      *m_currentTime;
    QLabel      *m_totalTime;
    QSlider     *m_volume;

    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
};
