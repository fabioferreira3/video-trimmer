#include "TrimDialog.h"

#include "util/TimeFormat.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace {

QString defaultOutputFor(const QString &sourcePath, bool preserveContainer)
{
    const QFileInfo info(sourcePath);
    const QString suffix = preserveContainer && !info.suffix().isEmpty()
        ? info.suffix()
        : QStringLiteral("mp4");
    return info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
         + QStringLiteral(".trim.") + suffix;
}

}  // namespace

TrimDialog::TrimDialog(const QString &sourcePath, qint64 inMs, qint64 outMs, QWidget *parent)
    : QDialog(parent)
    , m_sourcePath(sourcePath)
    , m_inMs(inMs)
    , m_outMs(outMs)
{
    setWindowTitle(tr("Export Trimmed Video"));
    setMinimumWidth(560);

    auto *layout = new QVBoxLayout(this);

    auto *infoForm = new QFormLayout;
    infoForm->addRow(tr("Source:"),   new QLabel(QFileInfo(sourcePath).fileName(), this));
    infoForm->addRow(tr("In:"),       new QLabel(TimeFormat::msToHms(inMs), this));
    infoForm->addRow(tr("Out:"),      new QLabel(TimeFormat::msToHms(outMs), this));
    infoForm->addRow(tr("Duration:"), new QLabel(TimeFormat::msToHms(outMs - inMs), this));
    layout->addLayout(infoForm);

    auto *outputBox = new QGroupBox(tr("Output"), this);
    auto *outputLayout = new QHBoxLayout(outputBox);
    m_outputEdit = new QLineEdit(this);
    m_outputEdit->setText(defaultOutputFor(sourcePath, /*preserveContainer=*/false));
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

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    auto *startBtn = buttons->addButton(tr("Start Export"), QDialogButtonBox::AcceptRole);
    Q_UNUSED(startBtn);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &TrimDialog::onAcceptRequested);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browseBtn, &QPushButton::clicked, this, &TrimDialog::onBrowseClicked);
    connect(m_radioPrecise, &QRadioButton::toggled, this, &TrimDialog::onModeChanged);
    connect(m_radioFast,    &QRadioButton::toggled, this, &TrimDialog::onModeChanged);
}

QString TrimDialog::outputPath() const { return m_outputEdit->text().trimmed(); }

FfmpegTrimRunner::Mode TrimDialog::mode() const
{
    return m_radioFast->isChecked()
        ? FfmpegTrimRunner::Mode::Fast
        : FfmpegTrimRunner::Mode::Precise;
}

void TrimDialog::onBrowseClicked()
{
    const QString chosen = QFileDialog::getSaveFileName(
        this,
        tr("Save Trimmed Video"),
        m_outputEdit->text(),
        tr("Video Files (*.mp4 *.mkv *.mov *.webm);;All Files (*)"));
    if (!chosen.isEmpty()) m_outputEdit->setText(chosen);
}

void TrimDialog::onModeChanged()
{
    // For fast mode, default to preserving the source container, since stream-copy
    // can fail when remuxing across container types.
    const bool fast = m_radioFast->isChecked();
    const QString suggested = defaultOutputFor(m_sourcePath, /*preserveContainer=*/fast);
    const QString currentDefault = defaultOutputFor(m_sourcePath, /*preserveContainer=*/!fast);
    if (m_outputEdit->text() == currentDefault) {
        m_outputEdit->setText(suggested);
    }
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
