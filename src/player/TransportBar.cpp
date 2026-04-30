#include "player/TransportBar.h"

#include "util/TimeFormat.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>

TransportBar::TransportBar(QWidget *parent)
    : QWidget(parent)
    , m_playPause(new QPushButton(tr("Play"), this))
    , m_loop(new QPushButton(tr("Loop"), this))
    , m_setIn(new QPushButton(tr("Set In (I)"), this))
    , m_setOut(new QPushButton(tr("Set Out (O)"), this))
    , m_currentTime(new QLabel(QStringLiteral("00:00:00.000"), this))
    , m_totalTime(new QLabel(QStringLiteral("00:00:00.000"), this))
    , m_volume(new QSlider(Qt::Horizontal, this))
{
    m_volume->setRange(0, 100);
    m_volume->setValue(80);
    m_volume->setMaximumWidth(120);
    m_volume->setToolTip(tr("Volume"));

    m_loop->setCheckable(true);
    m_loop->setToolTip(tr("Loop the trim selection while playing"));

    // Use a monospace face for the time labels so they don't jitter while playing.
    QFont monoFont = m_currentTime->font();
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setFamily(QStringLiteral("Monospace"));
    m_currentTime->setFont(monoFont);
    m_totalTime->setFont(monoFont);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->addWidget(m_playPause);
    layout->addWidget(m_loop);
    layout->addSpacing(8);
    layout->addWidget(m_setIn);
    layout->addWidget(m_setOut);
    layout->addStretch();
    layout->addWidget(m_currentTime);
    layout->addWidget(new QLabel(QStringLiteral(" / "), this));
    layout->addWidget(m_totalTime);
    layout->addSpacing(12);
    layout->addWidget(new QLabel(tr("Vol"), this));
    layout->addWidget(m_volume);

    connect(m_playPause, &QPushButton::clicked, this, &TransportBar::playPauseClicked);
    connect(m_setIn,     &QPushButton::clicked, this, &TransportBar::setInClicked);
    connect(m_setOut,    &QPushButton::clicked, this, &TransportBar::setOutClicked);
    connect(m_loop,      &QPushButton::toggled, this, &TransportBar::loopToggled);
    connect(m_volume,    &QSlider::valueChanged, this, [this](int v) {
        emit volumeChanged(static_cast<float>(v) / 100.0f);
    });

    setControlsEnabled(false);
}

void TransportBar::setPosition(qint64 ms)
{
    m_positionMs = ms;
    updateLabels();
}

void TransportBar::setDuration(qint64 ms)
{
    m_durationMs = ms;
    updateLabels();
}

void TransportBar::setPlaybackState(QMediaPlayer::PlaybackState state)
{
    m_playPause->setText(state == QMediaPlayer::PlayingState ? tr("Pause") : tr("Play"));
}

void TransportBar::setControlsEnabled(bool on)
{
    m_playPause->setEnabled(on);
    m_setIn->setEnabled(on);
    m_setOut->setEnabled(on);
    m_loop->setEnabled(on);
}

void TransportBar::setLoopEnabled(bool on)
{
    // Block the toggled signal to avoid bouncing back into MainWindow on programmatic restore.
    QSignalBlocker blocker(m_loop);
    m_loop->setChecked(on);
}

void TransportBar::updateLabels()
{
    m_currentTime->setText(TimeFormat::msToHms(m_positionMs));
    m_totalTime->setText(TimeFormat::msToHms(m_durationMs));
}
