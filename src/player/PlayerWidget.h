#pragma once

#include <QMediaPlayer>
#include <QWidget>

class QAudioOutput;
class QVideoWidget;

class PlayerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PlayerWidget(QWidget *parent = nullptr);

    QMediaPlayer *player() const { return m_player; }
    QMediaPlayer::PlaybackState playbackState() const;
    qint64 position() const;
    qint64 duration() const;

    // Empty id == "system default at construction time" (no explicit setDevice call).
    QByteArray currentAudioDeviceId() const;

public slots:
    void setSource(const QString &path);
    void clearSource();
    void play();
    void pause();
    void togglePlayPause();
    void setPosition(qint64 ms);
    void setVolume(float v);  // 0.0 - 1.0
    // Pin the QAudioOutput to a specific device id (from QAudioDevice::id()).
    // Pass an empty id to follow the system's current default audio output.
    void setAudioDeviceById(const QByteArray &id);

signals:
    void positionChanged(qint64 ms);
    void durationChanged(qint64 ms);
    void playbackStateChanged(QMediaPlayer::PlaybackState state);
    void mediaError(const QString &message);

private:
    QMediaPlayer *m_player;
    QAudioOutput *m_audio;
    QVideoWidget *m_videoWidget;
};
