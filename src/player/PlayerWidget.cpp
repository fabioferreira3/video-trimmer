#include "player/PlayerWidget.h"

#include <QAudio>
#include <QAudioDevice>
#include <QAudioOutput>
#include <QDebug>
#include <QMediaDevices>
#include <QMediaMetaData>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <algorithm>

PlayerWidget::PlayerWidget(QWidget *parent)
    : QWidget(parent)
    , m_player(new QMediaPlayer(this))
    , m_audio(new QAudioOutput(this))
    , m_videoWidget(new QVideoWidget(this))
{
    // Don't touch the audio device here. QAudioOutput's constructor binds to the
    // system default at creation time, and any per-stream routing the user does
    // in pavucontrol/Helvum will stick to the resulting stream as long as we
    // don't call setDevice() again. The MainWindow Audio menu is the explicit
    // way to pin a specific device persistently.
    m_audio->setMuted(false);
    m_audio->setVolume(1.0f);

    m_player->setAudioOutput(m_audio);
    m_player->setVideoOutput(m_videoWidget);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_videoWidget, 1);

    m_videoWidget->setMinimumSize(320, 180);
    m_videoWidget->setStyleSheet(QStringLiteral("background-color: black;"));

    connect(m_player, &QMediaPlayer::positionChanged,
            this, &PlayerWidget::positionChanged);
    connect(m_player, &QMediaPlayer::durationChanged,
            this, &PlayerWidget::durationChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged,
            this, &PlayerWidget::playbackStateChanged);
    connect(m_player, &QMediaPlayer::errorOccurred,
            this, [this](QMediaPlayer::Error, const QString &msg) {
                emit mediaError(msg);
            });

    // One-shot diagnostic when a file is fully loaded - run the app from a terminal
    // (./build/video-trimmer) to see this output and verify the pipeline.
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::LoadedMedia) {
                    qInfo().nospace()
                        << "[PlayerWidget] LoadedMedia"
                        << "  hasAudio="    << m_player->hasAudio()
                        << "  hasVideo="    << m_player->hasVideo()
                        << "  audioTracks=" << m_player->audioTracks().size()
                        << "  device='"     << m_audio->device().description() << "'"
                        << "  volume="      << m_audio->volume()
                        << "  muted="       << m_audio->isMuted();
                }
            });
}

QMediaPlayer::PlaybackState PlayerWidget::playbackState() const
{
    return m_player->playbackState();
}

qint64 PlayerWidget::position() const { return m_player->position(); }
qint64 PlayerWidget::duration() const { return m_player->duration(); }

void PlayerWidget::setSource(const QString &path)
{
    m_player->setSource(QUrl::fromLocalFile(path));
}

void PlayerWidget::clearSource()
{
    m_player->setSource({});
}

void PlayerWidget::play()  { m_player->play(); }
void PlayerWidget::pause() { m_player->pause(); }

void PlayerWidget::togglePlayPause()
{
    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->pause();
    } else {
        m_player->play();
    }
}

void PlayerWidget::setPosition(qint64 ms)
{
    m_player->setPosition(ms);
}

void PlayerWidget::setVolume(float linear01)
{
    // The slider is linear 0..1, but human hearing is logarithmic. Without this
    // conversion, "halfway" on the slider is barely quieter than full.
    const float clamped = std::clamp(linear01, 0.0f, 1.0f);
    const float real = QAudio::convertVolume(clamped,
                                             QAudio::LogarithmicVolumeScale,
                                             QAudio::LinearVolumeScale);
    m_audio->setVolume(real);
}

QByteArray PlayerWidget::currentAudioDeviceId() const
{
    return m_audio->device().id();
}

void PlayerWidget::setAudioDeviceById(const QByteArray &id)
{
    QAudioDevice target;
    if (id.isEmpty()) {
        target = QMediaDevices::defaultAudioOutput();
    } else {
        for (const QAudioDevice &dev : QMediaDevices::audioOutputs()) {
            if (dev.id() == id) {
                target = dev;
                break;
            }
        }
        if (target.isNull()) {
            qWarning() << "[PlayerWidget] audio device id not found:" << id
                       << "- falling back to system default";
            target = QMediaDevices::defaultAudioOutput();
        }
    }

    if (m_audio->device() != target) {
        m_audio->setDevice(target);
        qInfo() << "[PlayerWidget] audio device pinned to" << target.description();
    }
}
