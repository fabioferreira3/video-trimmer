#include "TrimDialog.h"

#include "util/TimeFormat.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>
#include <cmath>

namespace {

using AudioFormat = FfmpegTrimRunner::AudioFormat;

// What container/extension the precise (re-encode) preset should default to,
// given whether the source is audio-only and (for audio) which format the
// user picked. Mirrors FfmpegTrimRunner: precise video -> .mp4
// (H.264+AAC), precise audio -> whichever extension the audio format maps to.
QString preciseDefaultExtension(bool audioOnly, AudioFormat audioFormat)
{
    return audioOnly
        ? FfmpegTrimRunner::audioFormatExtension(audioFormat)
        : QStringLiteral("mp4");
}

QString defaultOutputFor(const QString &sourcePath,
                         bool preserveContainer,
                         bool audioOnly,
                         AudioFormat audioFormat)
{
    const QFileInfo info(sourcePath);
    const QString suffix = preserveContainer && !info.suffix().isEmpty()
        ? info.suffix()
        : preciseDefaultExtension(audioOnly, audioFormat);
    return info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
         + QStringLiteral(".trim.") + suffix;
}

// Pick the most natural default output format for a source whose audio codec
// ffprobe reported as `audioCodec`. The intent is that opening foo.mp3 and
// hitting Export should suggest writing foo.trim.mp3 by default, instead of
// silently transcoding to AAC like the older single-format pipeline did.
AudioFormat defaultAudioFormatFor(const MediaInfo &info)
{
    const QString c = info.audioCodec.toLower();
    if (c == QLatin1String("mp3"))    return AudioFormat::Mp3;
    if (c == QLatin1String("vorbis")) return AudioFormat::OggVorbis;
    if (c == QLatin1String("opus"))   return AudioFormat::Opus;
    if (c == QLatin1String("flac"))   return AudioFormat::Flac;
    if (c.startsWith(QLatin1String("pcm_"))) return AudioFormat::Wav;
    return AudioFormat::Aac;
}

QString formatSourceFps(const MediaInfo &info)
{
    const double primary = info.avgFps > 0.0 ? info.avgFps : info.fps;
    if (primary <= 0.0) return TrimDialog::tr("unknown");
    QString text = QString::number(primary, 'f', 3) + TrimDialog::tr(" fps");
    if (info.avgFps > 0.0 && info.fps > 0.0
        && std::abs(info.fps - info.avgFps) > 0.5) {
        text += TrimDialog::tr(" (raw %1)").arg(QString::number(info.fps, 'f', 3));
    }
    return text;
}

// Audio summary line for the dialog header when the input is audio-only.
// Mirrors how formatSourceFps handles "unknown" gracefully.
QString formatSourceAudio(const MediaInfo &info)
{
    QStringList parts;
    if (!info.audioCodec.isEmpty()) parts << info.audioCodec;
    if (info.audioSampleRate > 0) {
        parts << TrimDialog::tr("%1 Hz").arg(info.audioSampleRate);
    }
    if (info.audioChannels > 0) {
        parts << (info.audioChannels == 1 ? TrimDialog::tr("mono")
                : info.audioChannels == 2 ? TrimDialog::tr("stereo")
                : TrimDialog::tr("%1 ch").arg(info.audioChannels));
    }
    return parts.isEmpty() ? TrimDialog::tr("unknown") : parts.join(QStringLiteral(" / "));
}

}  // namespace

TrimDialog::TrimDialog(const QString &sourcePath,
                       const MediaInfo &info,
                       qint64 inMs,
                       qint64 outMs,
                       QWidget *parent)
    : QDialog(parent)
    , m_sourcePath(sourcePath)
    , m_info(info)
    , m_inMs(inMs)
    , m_outMs(outMs)
    , m_audioOnly(info.isAudioOnly())
{
    setWindowTitle(m_audioOnly ? tr("Export Trimmed Audio")
                               : tr("Export Trimmed Video"));
    setMinimumWidth(560);

    auto *layout = new QVBoxLayout(this);

    auto *infoForm = new QFormLayout;
    infoForm->addRow(tr("Source:"),   new QLabel(QFileInfo(sourcePath).fileName(), this));
    if (m_audioOnly) {
        infoForm->addRow(tr("Source Audio:"), new QLabel(formatSourceAudio(info), this));
    } else {
        infoForm->addRow(tr("Source FPS:"), new QLabel(formatSourceFps(info), this));
    }
    infoForm->addRow(tr("In:"),       new QLabel(TimeFormat::msToHms(inMs), this));
    infoForm->addRow(tr("Out:"),      new QLabel(TimeFormat::msToHms(outMs), this));
    infoForm->addRow(tr("Duration:"), new QLabel(TimeFormat::msToHms(outMs - inMs), this));
    layout->addLayout(infoForm);

    auto *outputBox = new QGroupBox(tr("Output"), this);
    auto *outputLayout = new QHBoxLayout(outputBox);
    m_outputEdit = new QLineEdit(this);
    auto *browseBtn = new QPushButton(tr("Browse..."), this);
    outputLayout->addWidget(m_outputEdit);
    outputLayout->addWidget(browseBtn);
    layout->addWidget(outputBox);

    auto *modeBox = new QGroupBox(tr("Trim Mode"), this);
    auto *modeLayout = new QVBoxLayout(modeBox);
    m_radioPrecise = new QRadioButton(tr("Precise (re-encode, ms-accurate, slower)"), this);
    m_radioFast    = new QRadioButton(tr("Fast (stream copy, snaps to nearest keyframe)"), this);
    m_radioPrecise->setChecked(true);
    modeLayout->addWidget(m_radioPrecise);
    modeLayout->addWidget(m_radioFast);
    layout->addWidget(modeBox);

    // Audio output format (visible only for audio-only sources). Building
    // this unconditionally keeps the member non-null and the slot logic
    // simple; we just hide the whole group for video sources, where the
    // chosen format is irrelevant (video sources go to MP4/H.264+AAC).
    m_audioFormatBox = new QGroupBox(tr("Audio Format"), this);
    auto *audioFormatLayout = new QHBoxLayout(m_audioFormatBox);
    audioFormatLayout->addWidget(new QLabel(tr("Output format:"), this));
    m_audioFormatCombo = new QComboBox(this);
    // Order doesn't affect correctness, but list lossy/compressed first since
    // those are by far the most common picks for trimmed clips.
    m_audioFormatCombo->addItem(tr("M4A (AAC, 192 kb/s)"),
                                int(AudioFormat::Aac));
    m_audioFormatCombo->addItem(tr("MP3 (libmp3lame, 192 kb/s)"),
                                int(AudioFormat::Mp3));
    m_audioFormatCombo->addItem(tr("Opus (libopus, 128 kb/s)"),
                                int(AudioFormat::Opus));
    m_audioFormatCombo->addItem(tr("OGG (Vorbis, q=5)"),
                                int(AudioFormat::OggVorbis));
    m_audioFormatCombo->addItem(tr("FLAC (lossless)"),
                                int(AudioFormat::Flac));
    m_audioFormatCombo->addItem(tr("WAV (PCM 16-bit)"),
                                int(AudioFormat::Wav));
    if (m_audioOnly) {
        const AudioFormat preselect = defaultAudioFormatFor(info);
        const int idx = m_audioFormatCombo->findData(int(preselect));
        if (idx >= 0) m_audioFormatCombo->setCurrentIndex(idx);
    }
    audioFormatLayout->addWidget(m_audioFormatCombo, 1);
    layout->addWidget(m_audioFormatBox);
    if (!m_audioOnly) m_audioFormatBox->setVisible(false);

    // Now that the audio format combo carries our default selection, we can
    // compute the initial output filename. Doing this AFTER the combo is
    // set avoids an extra round-trip through onAudioFormatChanged.
    refreshOutputDefault();

    m_fpsBox = new QGroupBox(tr("Frame Rate"), this);
    auto *fpsLayout = new QVBoxLayout(m_fpsBox);
    auto *fpsRow = new QHBoxLayout;
    fpsRow->addWidget(new QLabel(tr("Output FPS:"), this));
    m_fpsCombo = new QComboBox(this);
    m_fpsCombo->setEditable(true);
    // Order matters: index 0 is the "no override" sentinel.
    m_fpsCombo->addItem(tr("Keep source"));
    for (const QString &preset : { QStringLiteral("23.976"),
                                   QStringLiteral("24"),
                                   QStringLiteral("25"),
                                   QStringLiteral("29.97"),
                                   QStringLiteral("30"),
                                   QStringLiteral("50"),
                                   QStringLiteral("59.94"),
                                   QStringLiteral("60") }) {
        m_fpsCombo->addItem(preset);
    }
    if (auto *edit = m_fpsCombo->lineEdit()) {
        // Permit decimals; bound generously to cover any reasonable target rate.
        auto *validator = new QDoubleValidator(0.0, 1000.0, 6, this);
        validator->setNotation(QDoubleValidator::StandardNotation);
        validator->setLocale(QLocale::c());
        edit->setValidator(validator);
        edit->setPlaceholderText(tr("Keep source"));
    }
    fpsRow->addWidget(m_fpsCombo, 1);
    fpsLayout->addLayout(fpsRow);

    m_fpsHint = new QLabel(this);
    m_fpsHint->setWordWrap(true);
    fpsLayout->addWidget(m_fpsHint);

    layout->addWidget(m_fpsBox);
    // Frame rate controls have no meaning for audio-only sources; hide the
    // entire group so the dialog stays uncluttered.
    if (m_audioOnly) m_fpsBox->setVisible(false);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *startBtn = buttons->addButton(tr("Start Export"), QDialogButtonBox::AcceptRole);
    Q_UNUSED(startBtn);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &TrimDialog::onAcceptRequested);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browseBtn, &QPushButton::clicked, this, &TrimDialog::onBrowseClicked);
    connect(m_radioPrecise, &QRadioButton::toggled, this, &TrimDialog::onModeChanged);
    connect(m_radioFast,    &QRadioButton::toggled, this, &TrimDialog::onModeChanged);
    connect(m_fpsCombo, &QComboBox::currentTextChanged,
            this, [this](const QString &) { updateFpsHint(); });
    connect(m_audioFormatCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { onAudioFormatChanged(); });

    updateFpsHint();
}

QString TrimDialog::outputPath() const { return m_outputEdit->text().trimmed(); }

FfmpegTrimRunner::Mode TrimDialog::mode() const
{
    return m_radioFast->isChecked()
        ? FfmpegTrimRunner::Mode::Fast
        : FfmpegTrimRunner::Mode::Precise;
}

double TrimDialog::fpsOverride() const
{
    // Audio-only exports never get a -r flag, even if the (hidden) combo
    // somehow ended up on a numeric preset.
    if (m_audioOnly) return 0.0;
    if (!m_fpsCombo) return 0.0;
    if (m_fpsCombo->currentIndex() == 0) return 0.0;
    const QString text = m_fpsCombo->currentText().trimmed();
    if (text.isEmpty() || text == tr("Keep source")) return 0.0;
    bool ok = false;
    // Parse with C locale so "29.97" works regardless of the user's locale.
    const double v = QLocale::c().toDouble(text, &ok);
    if (!ok || v <= 0.0) return 0.0;
    return v;
}

FfmpegTrimRunner::AudioFormat TrimDialog::audioFormat() const
{
    if (!m_audioFormatCombo) return FfmpegTrimRunner::AudioFormat::Aac;
    bool ok = false;
    const int raw = m_audioFormatCombo->currentData().toInt(&ok);
    if (!ok) return FfmpegTrimRunner::AudioFormat::Aac;
    return static_cast<FfmpegTrimRunner::AudioFormat>(raw);
}

void TrimDialog::updateFpsHint()
{
    if (!m_fpsHint || !m_fpsCombo) return;

    const bool fast = m_radioFast && m_radioFast->isChecked();
    m_fpsCombo->setEnabled(!fast);

    if (fast) {
        m_fpsHint->setText(tr("Frame rate is preserved as-is in Fast (stream copy) mode."));
        return;
    }

    const double v = fpsOverride();
    if (v <= 0.0) {
        m_fpsHint->setText(tr("Output will keep the source frame rate."));
    } else {
        m_fpsHint->setText(tr("Output will be re-encoded at a constant %1 fps.")
                               .arg(QString::number(v, 'f', 3)));
    }
}

void TrimDialog::onBrowseClicked()
{
    const QString filter = m_audioOnly
        ? tr("Audio Files (*.m4a *.aac *.mp3 *.ogg *.opus *.flac *.wav *.wma);;All Files (*)")
        : tr("Video Files (*.mp4 *.mkv *.mov *.webm);;All Files (*)");
    const QString chosen = QFileDialog::getSaveFileName(
        this,
        m_audioOnly ? tr("Save Trimmed Audio") : tr("Save Trimmed Video"),
        m_outputEdit->text(),
        filter);
    if (!chosen.isEmpty()) m_outputEdit->setText(chosen);
}

void TrimDialog::onModeChanged()
{
    // Fast mode = stream-copy preserves source codec, so the audio format
    // picker is meaningless there. Disable it (still shown, so the user can
    // see the chosen format) instead of hiding to keep the dialog stable.
    if (m_audioFormatBox && m_audioOnly) {
        const bool fast = m_radioFast && m_radioFast->isChecked();
        m_audioFormatCombo->setEnabled(!fast);
    }
    refreshOutputDefault();
    updateFpsHint();
}

void TrimDialog::onAudioFormatChanged()
{
    refreshOutputDefault();
}

void TrimDialog::refreshOutputDefault()
{
    if (!m_outputEdit) return;

    // Fast mode preserves the source container (stream-copy across
    // containers is unreliable); precise mode uses the chosen audio format
    // for audio-only sources, or the canonical .mp4 for video sources.
    const bool fast = m_radioFast && m_radioFast->isChecked();
    const AudioFormat fmt = m_audioOnly
        ? audioFormat()
        : AudioFormat::Aac;
    const QString suggested =
        defaultOutputFor(m_sourcePath, /*preserveContainer=*/fast, m_audioOnly, fmt);

    // Replace the field only when it still holds whatever default WE last
    // set. Once the user types something, leave their text alone.
    const QString current = m_outputEdit->text();
    if (current.isEmpty() || current == m_lastAutoOutput) {
        m_outputEdit->setText(suggested);
    }
    m_lastAutoOutput = suggested;
}

void TrimDialog::onAcceptRequested()
{
    if (outputPath().isEmpty()) {
        QMessageBox::warning(this, tr("Output Required"),
                             tr("Please choose an output file path."));
        return;
    }
    if (m_outMs <= m_inMs) {
        QMessageBox::warning(this, tr("Invalid Range"),
                             tr("The Out point must be after the In point."));
        return;
    }
    accept();
}
