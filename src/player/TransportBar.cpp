#include "player/TransportBar.h"

#include "util/TimeFormat.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

namespace {
// Width hint that comfortably fits "00:00:00.000" in a monospace font.
constexpr int kTimeEditMinWidthChars = 13;

// Preset durations offered by the "Duration" dropdown. Selecting one sets
// Out = In + duration.
struct DurationPreset {
    const char *label;
    qint64      ms;
};
constexpr DurationPreset kDurationPresets[] = {
    { "5 seconds",  5'000  },
    { "10 seconds", 10'000 },
    { "15 seconds", 15'000 },
    { "30 seconds", 30'000 },
    { "45 seconds", 45'000 },
    { "60 seconds", 60'000 },
};
// Sentinel stored in the combo's itemData for the "Custom..." entry.
// A zero duration is otherwise meaningless (it would collapse the range),
// so this is unambiguous.
constexpr qint64 kCustomDurationSentinel = 0;
}  // namespace

TransportBar::TransportBar(QWidget *parent)
    : QWidget(parent)
    , m_playPause(new QPushButton(tr("Play"), this))
    , m_playFromStart(new QPushButton(tr("Play from Start"), this))
    , m_loop(new QPushButton(tr("Loop"), this))
    , m_setIn(new QPushButton(tr("Set Start (I)"), this))
    , m_setOut(new QPushButton(tr("Set End (O)"), this))
    , m_inLabel(new QLabel(tr("Start:"), this))
    , m_outLabel(new QLabel(tr("End:"), this))
    , m_inEdit(new QLineEdit(this))
    , m_outEdit(new QLineEdit(this))
    , m_durationPreset(new QComboBox(this))
    , m_customRange(new QPushButton(tr("Custom Start/End"), this))
    , m_crop(new QPushButton(tr("Crop"), this))
    , m_cut(new QPushButton(tr("Cut"), this))
    , m_currentTime(new QLabel(QStringLiteral("00:00:00.000"), this))
    , m_totalTime(new QLabel(QStringLiteral("00:00:00.000"), this))
    , m_volume(new QSlider(Qt::Horizontal, this))
{
    m_volume->setRange(0, 100);
    m_volume->setValue(80);
    m_volume->setMaximumWidth(120);
    m_volume->setToolTip(tr("Volume"));

    m_playFromStart->setToolTip(
        tr("Restart playback from the In marker. "
           "Unlike Play, this always jumps back to In even if the video is "
           "already playing or paused past it."));

    m_loop->setCheckable(true);
    m_loop->setToolTip(tr("Loop the trim selection while playing"));

    m_crop->setToolTip(
        tr("Crop the working clip to the current [In, Out] selection. "
           "Continue trimming from there. Original file is untouched."));

    m_cut->setToolTip(
        tr("Cut the [In, Out] selection out of the working clip and stitch "
           "the rest back together. Repeat as needed. Original file is untouched."));

    // Use a monospace face for every time-shaped widget so layout doesn't
    // jitter while playing and so the In/Out edit fields visually line up
    // with the current/total time labels.
    QFont monoFont = m_currentTime->font();
    monoFont.setStyleHint(QFont::Monospace);
    monoFont.setFamily(QStringLiteral("Monospace"));
    m_currentTime->setFont(monoFont);
    m_totalTime->setFont(monoFont);
    m_inEdit->setFont(monoFont);
    m_outEdit->setFont(monoFont);

    const QFontMetrics fm(monoFont);
    const int timeEditWidth = fm.horizontalAdvance(QChar('0')) * kTimeEditMinWidthChars;

    auto configureTimeEdit = [&](QLineEdit *edit, const QString &tip) {
        edit->setText(QStringLiteral("00:00:00.000"));
        edit->setMinimumWidth(timeEditWidth);
        edit->setMaximumWidth(timeEditWidth + 8);
        edit->setAlignment(Qt::AlignCenter);
        edit->setToolTip(tip);
        // We deliberately don't install a strict QValidator so that the user
        // can freely type partial values like "12:25" or "1.5"; validation
        // happens on commit (Enter / focus loss).
    };
    configureTimeEdit(m_inEdit,
        tr("Start point. Type HH:MM:SS.mmm (or MM:SS, or seconds) and press Enter."));
    configureTimeEdit(m_outEdit,
        tr("End point. Type HH:MM:SS.mmm (or MM:SS, or seconds) and press Enter."));

    // The manual Start/End time edits are an "advanced" affordance: most users
    // just click Set Start/End or scrub the timeline. Keep them hidden until
    // the user opts in via the Custom Start/End toggle below.
    m_customRange->setCheckable(true);
    m_customRange->setToolTip(
        tr("Show manual Start/End time entry fields for typing exact timestamps."));
    m_inLabel->hide();
    m_outLabel->hide();
    m_inEdit->hide();
    m_outEdit->hide();

    m_durationPreset->setToolTip(
        tr("Quick duration: set Out to In + the selected length."));
    m_durationPreset->setPlaceholderText(tr("Duration..."));
    for (const auto &preset : kDurationPresets) {
        m_durationPreset->addItem(tr(preset.label), QVariant::fromValue(preset.ms));
    }
    m_durationPreset->insertSeparator(m_durationPreset->count());
    m_durationPreset->addItem(tr("Custom..."),
                              QVariant::fromValue(kCustomDurationSentinel));
    // No item should look "selected" at rest; this is a one-shot action chooser.
    m_durationPreset->setCurrentIndex(-1);

    // Two-row layout:
    //   Row 1 - playback + destructive actions, with current/total time and
    //           volume floated to the right edge (they're playback metadata).
    //   Row 2 - the selection-editing cluster: Set Start / Set End /
    //           Duration preset / Custom Start/End toggle, followed by the
    //           manual Start/End edit fields (hidden until toggled on).
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 4, 8, 4);
    outer->setSpacing(4);

    auto *row1 = new QHBoxLayout();
    row1->setContentsMargins(0, 0, 0, 0);
    row1->addWidget(m_playPause);
    row1->addWidget(m_playFromStart);
    row1->addWidget(m_loop);
    row1->addSpacing(12);
    row1->addWidget(m_crop);
    row1->addWidget(m_cut);
    row1->addStretch();
    row1->addWidget(m_currentTime);
    row1->addWidget(new QLabel(QStringLiteral(" / "), this));
    row1->addWidget(m_totalTime);
    row1->addSpacing(12);
    row1->addWidget(new QLabel(tr("Vol"), this));
    row1->addWidget(m_volume);
    outer->addLayout(row1);

    auto *row2 = new QHBoxLayout();
    row2->setContentsMargins(0, 0, 0, 0);
    row2->addWidget(m_setIn);
    row2->addWidget(m_setOut);
    row2->addSpacing(8);
    row2->addWidget(m_durationPreset);
    row2->addWidget(m_customRange);
    row2->addSpacing(8);
    row2->addWidget(m_inLabel);
    row2->addWidget(m_inEdit);
    row2->addSpacing(8);
    row2->addWidget(m_outLabel);
    row2->addWidget(m_outEdit);
    row2->addStretch();
    outer->addLayout(row2);

    connect(m_playPause,     &QPushButton::clicked, this, &TransportBar::playPauseClicked);
    connect(m_playFromStart, &QPushButton::clicked, this, &TransportBar::playFromStartClicked);
    connect(m_setIn,     &QPushButton::clicked, this, &TransportBar::setInClicked);
    connect(m_setOut,    &QPushButton::clicked, this, &TransportBar::setOutClicked);
    connect(m_loop,      &QPushButton::toggled, this, &TransportBar::loopToggled);
    connect(m_crop,      &QPushButton::clicked, this, &TransportBar::cropClicked);
    connect(m_cut,       &QPushButton::clicked, this, &TransportBar::cutClicked);
    connect(m_customRange, &QPushButton::toggled, this, [this](bool on) {
        m_inLabel->setVisible(on);
        m_inEdit->setVisible(on);
        m_outLabel->setVisible(on);
        m_outEdit->setVisible(on);
    });
    connect(m_volume,    &QSlider::valueChanged, this, [this](int v) {
        emit volumeChanged(static_cast<float>(v) / 100.0f);
    });
    // editingFinished fires both on Enter and on focus loss, which is exactly
    // what we want: the user can tab/click away without losing their input.
    connect(m_inEdit,  &QLineEdit::editingFinished, this, [this]() {
        commitTimeEdit(m_inEdit, m_inMs, /*isIn=*/true);
    });
    connect(m_outEdit, &QLineEdit::editingFinished, this, [this]() {
        commitTimeEdit(m_outEdit, m_outMs, /*isIn=*/false);
    });
    // activated() (vs currentIndexChanged()) only fires for genuine user
    // selections, so resetting the index back to -1 below will not bounce
    // back into this slot.
    connect(m_durationPreset, &QComboBox::activated, this, [this](int index) {
        if (index < 0) return;
        const qint64 durationMs = m_durationPreset->itemData(index).toLongLong();
        // Snap the combo back to its placeholder so it stays a one-shot action
        // chooser rather than appearing to "remember" the chosen duration.
        // We do this before any blocking dialog so that, if the user cancels
        // or the input is invalid, the combo is already in the correct
        // visual state.
        {
            QSignalBlocker blocker(m_durationPreset);
            m_durationPreset->setCurrentIndex(-1);
        }
        if (durationMs == kCustomDurationSentinel) {
            promptCustomDurationAndEmit();
        } else if (durationMs > 0) {
            emit outDurationPresetSelected(durationMs);
        }
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
    m_playFromStart->setEnabled(on);
    m_setIn->setEnabled(on);
    m_setOut->setEnabled(on);
    m_loop->setEnabled(on);
    m_inEdit->setEnabled(on);
    m_outEdit->setEnabled(on);
    m_durationPreset->setEnabled(on);
    m_customRange->setEnabled(on);
    m_crop->setEnabled(on);
    m_cut->setEnabled(on);
}

void TransportBar::setLoopEnabled(bool on)
{
    // Block the toggled signal to avoid bouncing back into MainWindow on programmatic restore.
    QSignalBlocker blocker(m_loop);
    m_loop->setChecked(on);
}

void TransportBar::setInPosition(qint64 ms)
{
    m_inMs = ms;
    // Don't clobber the field while the user is mid-edit; that would be
    // jarring if external state changes (e.g. ffprobe finishing) arrive
    // while they're typing. The next focus loss / Enter will re-sync.
    if (!m_inEdit->hasFocus()) {
        QSignalBlocker blocker(m_inEdit);
        m_inEdit->setText(TimeFormat::msToHms(ms));
    }
}

void TransportBar::setOutPosition(qint64 ms)
{
    m_outMs = ms;
    if (!m_outEdit->hasFocus()) {
        QSignalBlocker blocker(m_outEdit);
        m_outEdit->setText(TimeFormat::msToHms(ms));
    }
}

void TransportBar::updateLabels()
{
    m_currentTime->setText(TimeFormat::msToHms(m_positionMs));
    m_totalTime->setText(TimeFormat::msToHms(m_durationMs));
}

void TransportBar::promptCustomDurationAndEmit()
{
    // Seed with the current selection length when it's non-empty so a user
    // refining an existing range starts from "what they had". Otherwise fall
    // back to a sensible neutral default.
    const qint64 currentSelMs = (m_outMs > m_inMs) ? (m_outMs - m_inMs) : 0;
    const int    defaultSec   = currentSelMs > 0
                                  ? int(std::min<qint64>(currentSelMs / 1000, 86'400))
                                  : 10;

    bool ok = false;
    const int seconds = QInputDialog::getInt(
        this,
        tr("Custom Duration"),
        tr("Set Out to In + this many seconds:"),
        defaultSec,
        /*min=*/  1,
        /*max=*/  86'400,  // 24 hours; arbitrary but generous upper bound.
        /*step=*/ 1,
        &ok);
    if (!ok || seconds <= 0) return;

    emit outDurationPresetSelected(qint64(seconds) * 1000);
}

void TransportBar::commitTimeEdit(QLineEdit *edit, qint64 currentMs, bool isIn)
{
    qint64 parsed = 0;
    if (TimeFormat::parseHms(edit->text(), &parsed)) {
        // Reformat to canonical HH:MM:SS.mmm so the field always shows a
        // tidy value once the user is done editing.
        QSignalBlocker blocker(edit);
        edit->setText(TimeFormat::msToHms(parsed));
        if (parsed != currentMs) {
            if (isIn) emit inTimeEdited(parsed);
            else      emit outTimeEdited(parsed);
        }
    } else {
        // Bad input: roll back to the last known authoritative value.
        QSignalBlocker blocker(edit);
        edit->setText(TimeFormat::msToHms(currentMs));
    }
}
