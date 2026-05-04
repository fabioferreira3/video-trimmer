#include "MainWindow.h"

#include "TrimDialog.h"
#include "VideoSession.h"
#include "ffmpeg/FfmpegCutRunner.h"
#include "ffmpeg/FfmpegTrimRunner.h"
#include "ffmpeg/MediaInfo.h"
#include "player/PlayerWidget.h"
#include "player/TransportBar.h"
#include "timeline/TimelineWidget.h"
#include "util/TimeFormat.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QAudioDevice>
#include <QCloseEvent>
#include <QDebug>
#include <QDir>
 #include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QMediaDevices>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTemporaryFile>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kMaxRecentFiles = 10;
const QString kRecentFilesKey      = QStringLiteral("recentFiles");
const QString kWindowGeometryKey   = QStringLiteral("windowGeometry");
const QString kWindowStateKey      = QStringLiteral("windowState");
const QString kLoopEnabledKey      = QStringLiteral("loopEnabled");
const QString kAudioDeviceIdKey    = QStringLiteral("audioDeviceId");
}  // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Video Trimmer"));
    setAcceptDrops(true);
    // Catch drag/drop events anywhere in the app, including over the
    // QVideoWidget's native surface which would otherwise swallow them.
    qApp->installEventFilter(this);
    qInfo() << "[DnD] MainWindow constructed; qApp event filter installed; "
               "platform =" << QGuiApplication::platformName();

    m_session     = new VideoSession(this);
    m_trimRunner  = new FfmpegTrimRunner(this);
    m_cropRunner  = new FfmpegTrimRunner(this);
    m_cutRunner   = new FfmpegCutRunner(this);

    buildCentralLayout();
    buildMenus();
    buildStatusBar();
    wireConnections();

    loadSettings();
    updateRecentMenu();
    updateActionsEnabled();
    updateInfoLabel();

    // The keyboard shortcuts in keyPressEvent are window-wide, so claim focus.
    setFocusPolicy(Qt::StrongFocus);
}

MainWindow::~MainWindow() = default;

// -----------------------------------------------------------------------------
// Layout / construction

void MainWindow::buildCentralLayout()
{
    auto *central = new QWidget(this);
    auto *layout  = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_player    = new PlayerWidget(central);
    m_transport = new TransportBar(central);
    m_timeline  = new TimelineWidget(central);

    auto *separator1 = new QFrame(central);
    separator1->setFrameShape(QFrame::HLine);
    separator1->setFrameShadow(QFrame::Sunken);

    auto *separator2 = new QFrame(central);
    separator2->setFrameShape(QFrame::HLine);
    separator2->setFrameShadow(QFrame::Sunken);

    layout->addWidget(m_player, 1);
    layout->addWidget(separator1);
    layout->addWidget(m_timeline);
    layout->addWidget(separator2);
    layout->addWidget(m_transport);

    // Opt every child into drag-and-drop. The actual handling is centralised
    // in eventFilter(), but Qt only initiates a drag-enter sequence on
    // widgets that have explicitly accepted drops, so we cascade the flag.
    central->setAcceptDrops(true);
    m_player->setAcceptDrops(true);
    m_timeline->setAcceptDrops(true);
    m_transport->setAcceptDrops(true);

    setCentralWidget(central);
}

void MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));

    m_actionOpen = fileMenu->addAction(tr("&Open Media..."), this, &MainWindow::onOpenTriggered);
    m_actionOpen->setShortcut(QKeySequence::Open);

    m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));
    for (int i = 0; i < kMaxRecentFiles; ++i) {
        auto *act = new QAction(this);
        act->setVisible(false);
        connect(act, &QAction::triggered, this, &MainWindow::onRecentTriggered);
        m_recentMenu->addAction(act);
        m_recentActions.append(act);
    }
    m_recentMenu->addSeparator();
    m_recentMenu->addAction(tr("&Clear List"), this, &MainWindow::onClearRecentTriggered);

    fileMenu->addSeparator();
    m_actionClose = fileMenu->addAction(tr("&Close"), this, &MainWindow::onCloseTriggered);
    m_actionClose->setShortcut(QKeySequence::Close);

    fileMenu->addSeparator();
    m_actionCrop = fileMenu->addAction(tr("&Crop to Selection"), this, &MainWindow::onCropTriggered);
    m_actionCrop->setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
    m_actionCrop->setStatusTip(
        tr("Reduce the working clip to the current [In, Out] selection and continue trimming. "
           "The original file is not modified."));

    m_actionCut = fileMenu->addAction(tr("Cu&t Selection"), this, &MainWindow::onCutTriggered);
    m_actionCut->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+K")));
    m_actionCut->setStatusTip(
        tr("Remove the [In, Out] selection from the working clip and stitch the rest "
           "back together. Repeat as needed. The original file is not modified."));

    m_actionExport = fileMenu->addAction(tr("&Export Trimmed..."), this, &MainWindow::onExportTriggered);
    m_actionExport->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    m_actionExport->setStatusTip(tr("Export the current trim selection to a new media file"));

    fileMenu->addSeparator();
    m_actionQuit = fileMenu->addAction(tr("&Quit"), qApp, &QApplication::quit);
    m_actionQuit->setShortcut(QKeySequence::Quit);

    auto *audioMenu = menuBar()->addMenu(tr("&Audio"));
    m_outputMenu  = audioMenu->addMenu(tr("&Output Device"));
    m_outputGroup = new QActionGroup(this);
    m_outputGroup->setExclusive(true);
    // Refresh the device list each time the menu is opened so hot-plugged
    // devices appear without restarting the app.
    connect(m_outputMenu, &QMenu::aboutToShow,
            this, &MainWindow::onAudioMenuAboutToShow);
    rebuildAudioOutputMenu();

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    m_actionAbout = helpMenu->addAction(tr("&About"), this, &MainWindow::onAboutTriggered);
}

void MainWindow::buildStatusBar()
{
    m_infoLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_infoLabel, 1);
}

void MainWindow::wireConnections()
{
    connect(m_session, &VideoSession::opened,
            this, &MainWindow::onSessionOpened);
    connect(m_session, &VideoSession::mediaInfoUpdated,
            this, &MainWindow::onMediaInfoUpdated);
    connect(m_session, &VideoSession::probeFailed,
            this, &MainWindow::onProbeFailed);
    connect(m_session, &VideoSession::inOutChanged,
            this, [this](qint64 inMs, qint64 outMs) {
                m_timeline->setInOut(inMs, outMs);
                m_transport->setInPosition(inMs);
                m_transport->setOutPosition(outMs);
                updateInfoLabel();
                // If the user shrinks the range past the current playhead while playing,
                // snap back to a sensible position immediately.
                if (m_player->playbackState() == QMediaPlayer::PlayingState
                    && outMs > inMs) {
                    const qint64 pos = m_player->position();
                    if (pos >= outMs) {
                        m_player->pause();
                        m_player->setPosition(outMs);
                    } else if (pos < inMs) {
                        m_player->setPosition(inMs);
                    }
                }
            });

    connect(m_player, &PlayerWidget::positionChanged,
            this, &MainWindow::onPlayerPosition);
    connect(m_player, &PlayerWidget::durationChanged,
            this, &MainWindow::onPlayerDuration);
    connect(m_player, &PlayerWidget::playbackStateChanged,
            m_transport, &TransportBar::setPlaybackState);
    connect(m_player, &PlayerWidget::mediaError,
            this, &MainWindow::onPlayerError);

    connect(m_transport, &TransportBar::playPauseClicked,
            this, &MainWindow::togglePlayPauseInRange);
    connect(m_transport, &TransportBar::playFromStartClicked,
            this, &MainWindow::playFromIn);
    connect(m_transport, &TransportBar::volumeChanged,
            m_player, &PlayerWidget::setVolume);
    connect(m_transport, &TransportBar::setInClicked,
            this, [this]() { m_session->setIn(m_player->position()); });
    connect(m_transport, &TransportBar::setOutClicked,
            this, [this]() { m_session->setOut(m_player->position()); });
    connect(m_transport, &TransportBar::inTimeEdited,
            this, [this](qint64 ms) { m_session->setIn(ms); });
    connect(m_transport, &TransportBar::outTimeEdited,
            this, [this](qint64 ms) { m_session->setOut(ms); });
    connect(m_transport, &TransportBar::outDurationPresetSelected,
            this, [this](qint64 durationMs) {
                // Anchor the requested duration to the current In point.
                // VideoSession::setOut clamps to media duration, so picking a
                // preset that would extend past EOF still produces a valid range.
                m_session->setOut(m_session->inMs() + durationMs);
            });
    connect(m_transport, &TransportBar::loopToggled,
            this, [this](bool on) {
                m_loopEnabled = on;
                QSettings s;
                s.setValue(kLoopEnabledKey, on);
            });
    connect(m_transport, &TransportBar::cropClicked,
            this, &MainWindow::onCropTriggered);
    connect(m_transport, &TransportBar::cutClicked,
            this, &MainWindow::onCutTriggered);

    connect(m_timeline, &TimelineWidget::seekRequested,
            this, &MainWindow::onTimelineSeek);
    connect(m_timeline, &TimelineWidget::inOutChanged,
            this, &MainWindow::onTimelineInOut);

    connect(m_trimRunner, &FfmpegTrimRunner::progress,
            this, [this](double pct) {
                if (m_progressDialog) {
                    m_progressDialog->setValue(int(std::round(pct)));
                }
            });
    connect(m_trimRunner, &FfmpegTrimRunner::finished,
            this, [this](bool ok, const QString &errorTail) {
                if (m_progressDialog) {
                    m_progressDialog->reset();
                    m_progressDialog->deleteLater();
                    m_progressDialog = nullptr;
                }
                if (ok) {
                    QMessageBox::information(this, tr("Export Complete"),
                                             m_session->mediaInfo().isAudioOnly()
                                                 ? tr("Trimmed audio saved successfully.")
                                                 : tr("Trimmed video saved successfully."));
                } else {
                    QMessageBox::critical(this, tr("Export Failed"),
                                          errorTail.isEmpty()
                                              ? tr("ffmpeg exited with an error.")
                                              : errorTail);
                }
            });

    connect(m_cropRunner, &FfmpegTrimRunner::progress,
            this, [this](double pct) {
                if (m_cropProgressDialog) {
                    m_cropProgressDialog->setValue(int(std::round(pct)));
                }
            });
    connect(m_cropRunner, &FfmpegTrimRunner::finished,
            this, [this](bool ok, const QString &errorTail) {
                if (m_cropProgressDialog) {
                    m_cropProgressDialog->reset();
                    m_cropProgressDialog->deleteLater();
                    m_cropProgressDialog = nullptr;
                }

                const QString output = m_cropPendingOutput;
                m_cropPendingOutput.clear();

                if (!ok) {
                    if (!output.isEmpty()) QFile::remove(output);
                    QMessageBox::critical(this, tr("Crop Failed"),
                                          errorTail.isEmpty()
                                              ? tr("ffmpeg exited with an error.")
                                              : errorTail);
                    return;
                }

                // Capture the friendly name for the title bar before we swap
                // the session source. On the first edit this is the user's
                // original file; on subsequent edits it's already set so we
                // preserve "myvideo.mp4" instead of latching onto a temp path.
                if (m_workingClipDisplayName.isEmpty()) {
                    m_workingClipDisplayName = QFileInfo(m_session->filePath()).fileName();
                }

                // Stop playback before swapping the source so QMediaPlayer
                // releases the previous file's handles cleanly.
                m_player->pause();
                m_player->clearSource();

                // Track the new temp file BEFORE opening it so onSessionOpened
                // recognises this open as a transient working-clip result.
                m_workingClipTempFiles.append(output);
                m_session->openFile(output);
            });

    connect(m_cutRunner, &FfmpegCutRunner::progress,
            this, [this](double pct) {
                if (m_cutProgressDialog) {
                    m_cutProgressDialog->setValue(int(std::round(pct)));
                }
            });
    connect(m_cutRunner, &FfmpegCutRunner::finished,
            this, [this](bool ok, const QString &errorTail) {
                if (m_cutProgressDialog) {
                    m_cutProgressDialog->reset();
                    m_cutProgressDialog->deleteLater();
                    m_cutProgressDialog = nullptr;
                }

                const QString output = m_cutPendingOutput;
                m_cutPendingOutput.clear();

                if (!ok) {
                    // FfmpegCutRunner already removes the partial output and
                    // its intermediates on failure, but be defensive.
                    if (!output.isEmpty()) QFile::remove(output);
                    QMessageBox::critical(this, tr("Cut Failed"),
                                          errorTail.isEmpty()
                                              ? tr("ffmpeg exited with an error.")
                                              : errorTail);
                    return;
                }

                if (m_workingClipDisplayName.isEmpty()) {
                    m_workingClipDisplayName = QFileInfo(m_session->filePath()).fileName();
                }

                m_player->pause();
                m_player->clearSource();

                m_workingClipTempFiles.append(output);
                m_session->openFile(output);
            });

    // Initial volume sync (TransportBar default is 80).
    m_player->setVolume(0.8f);
}

// -----------------------------------------------------------------------------
// Slots

void MainWindow::onOpenTriggered()
{
    const QString lastDir = []() {
        QSettings s;
        return s.value(QStringLiteral("lastDir"),
                       QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
                .toString();
    }();

    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Open Media"),
        lastDir,
        tr("Media Files (*.mp4 *.mkv *.mov *.avi *.webm *.m4v *.ts *.mpg *.mpeg *.flv "
           "*.mp3 *.m4a *.aac *.ogg *.opus *.flac *.wav *.wma);;"
           "Video Files (*.mp4 *.mkv *.mov *.avi *.webm *.m4v *.ts *.mpg *.mpeg *.flv);;"
           "Audio Files (*.mp3 *.m4a *.aac *.ogg *.opus *.flac *.wav *.wma);;"
           "All Files (*)"));

    if (path.isEmpty()) return;

    QSettings s;
    s.setValue(QStringLiteral("lastDir"), QFileInfo(path).absolutePath());

    openFile(path);
}

void MainWindow::onCloseTriggered()
{
    if (m_cropRunner && m_cropRunner->isRunning()) {
        m_cropRunner->cancel();
    }
    if (m_cutRunner && m_cutRunner->isRunning()) {
        m_cutRunner->cancel();
    }
    m_session->close();
    m_player->clearSource();
    m_player->setAudioOnlyMode(false);
    m_timeline->setDuration(0);
    m_timeline->setPosition(0);
    m_timeline->setInOut(0, 0);
    m_transport->setPosition(0);
    m_transport->setDuration(0);
    m_transport->setInPosition(0);
    m_transport->setOutPosition(0);
    m_transport->setControlsEnabled(false);
    setWindowTitle(tr("Video Trimmer"));
    // Drop any working-clip temp files now that the player has released the source.
    cleanupWorkingClipTempFiles();
    m_workingClipDisplayName.clear();
    updateActionsEnabled();
    updateInfoLabel();
}

void MainWindow::onExportTriggered()
{
    if (!m_session->isOpen() || !m_session->mediaInfo().isValid()) {
        QMessageBox::information(this, tr("No Media"),
                                 tr("Open a video or audio file before exporting."));
        return;
    }

    if (m_session->outMs() <= m_session->inMs()) {
        QMessageBox::warning(this, tr("Invalid Range"),
                             tr("Set the Out point after the In point before exporting."));
        return;
    }

    if (m_trimRunner->isRunning() || m_cropRunner->isRunning() || m_cutRunner->isRunning()) {
        QMessageBox::information(this, tr("Operation In Progress"),
                                 tr("Wait for the current operation to finish."));
        return;
    }

    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
        QMessageBox::critical(this, tr("ffmpeg Not Found"),
                              tr("ffmpeg was not found on PATH.\n\nInstall it with:\n  sudo pacman -S ffmpeg"));
        return;
    }

    TrimDialog dlg(m_session->filePath(),
                   m_session->mediaInfo(),
                   m_session->inMs(),
                   m_session->outMs(),
                   this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString output      = dlg.outputPath();
    const auto    mode        = dlg.mode();
    const double  fps         = dlg.fpsOverride();
    const bool    audioOnly   = dlg.audioOnly();
    const auto    audioFormat = dlg.audioFormat();

    m_progressDialog = new QProgressDialog(audioOnly
                                               ? tr("Exporting trimmed audio...")
                                               : tr("Exporting trimmed video..."),
                                           tr("Cancel"), 0, 100, this);
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->setAutoClose(false);
    m_progressDialog->setAutoReset(false);
    m_progressDialog->setValue(0);
    connect(m_progressDialog, &QProgressDialog::canceled,
            m_trimRunner, &FfmpegTrimRunner::cancel);

    m_trimRunner->start(m_session->filePath(), output,
                        m_session->inMs(), m_session->outMs(),
                        mode, fps, audioOnly, audioFormat);
}

void MainWindow::onCropTriggered()
{
    if (!m_session->isOpen() || !m_session->mediaInfo().isValid()) {
        QMessageBox::information(this, tr("No Media"),
                                 tr("Open a video or audio file before cropping."));
        return;
    }

    if (m_session->outMs() <= m_session->inMs()) {
        QMessageBox::warning(this, tr("Invalid Range"),
                             tr("Set the Out point after the In point before cropping."));
        return;
    }

    // If the selection already covers the entire clip there is nothing to do;
    // creating a no-op temp file would just churn disk for no benefit.
    if (m_session->inMs() == 0
        && m_session->outMs() >= m_session->mediaInfo().durationMs) {
        QMessageBox::information(this, tr("Nothing to Crop"),
                                 tr("The selection already covers the entire clip."));
        return;
    }

    if (m_cropRunner->isRunning() || m_trimRunner->isRunning() || m_cutRunner->isRunning()) {
        QMessageBox::information(this, tr("Operation In Progress"),
                                 tr("Wait for the current operation to finish."));
        return;
    }

    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
        QMessageBox::critical(this, tr("ffmpeg Not Found"),
                              tr("ffmpeg was not found on PATH.\n\nInstall it with:\n  sudo pacman -S ffmpeg"));
        return;
    }

    {
        const auto answer = QMessageBox::question(
            this, tr("Crop to Selection"),
            tr("Reduce the working clip to the current [In, Out] selection?\n\n"
               "This action cannot be undone. Your original file on disk will "
               "not be modified \u2014 only the in-app working clip changes."),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Yes);
        if (answer != QMessageBox::Yes) return;
    }

    // Build a unique temp output path that preserves the source extension so
    // ffmpeg can pick the right muxer from the suffix and the player can
    // re-decode without surprises. QTemporaryFile's only job here is to
    // reserve a unique name; we close it immediately and let ffmpeg overwrite.
    const QString sourcePath = m_session->filePath();
    const QString sourceExt  = QFileInfo(sourcePath).suffix();
    const QString suffix     = sourceExt.isEmpty() ? QStringLiteral("mkv") : sourceExt;
    const QString templatePath = QDir::temp().filePath(
        QStringLiteral("vtrim-crop-XXXXXX.") + suffix);

    QTemporaryFile tempFile(templatePath);
    tempFile.setAutoRemove(false);
    if (!tempFile.open()) {
        QMessageBox::critical(this, tr("Crop Failed"),
                              tr("Could not create a temporary file for the crop output."));
        return;
    }
    const QString outputPath = tempFile.fileName();
    tempFile.close();

    m_cropPendingOutput = outputPath;

    m_cropProgressDialog = new QProgressDialog(tr("Cropping to selection..."),
                                               tr("Cancel"), 0, 100, this);
    m_cropProgressDialog->setWindowModality(Qt::WindowModal);
    m_cropProgressDialog->setMinimumDuration(0);
    m_cropProgressDialog->setAutoClose(false);
    m_cropProgressDialog->setAutoReset(false);
    m_cropProgressDialog->setValue(0);
    connect(m_cropProgressDialog, &QProgressDialog::canceled,
            m_cropRunner, &FfmpegTrimRunner::cancel);

    // Stream-copy (Fast) mode is the right default for an iterative trim
    // workflow: it's near-instant and lossless, so the user can crop, then
    // re-trim, then crop again without watching their video degrade. The
    // tradeoff is the standard one - the cut snaps to the source's nearest
    // preceding keyframe, so the cropped clip may include a few extra frames
    // before the requested In point. Users who need ms-accurate cuts can
    // still go through Export (which offers Precise mode).
    m_cropRunner->start(sourcePath, outputPath,
                        m_session->inMs(), m_session->outMs(),
                        FfmpegTrimRunner::Mode::Fast);
}

void MainWindow::onCutTriggered()
{
    if (!m_session->isOpen() || !m_session->mediaInfo().isValid()) {
        QMessageBox::information(this, tr("No Media"),
                                 tr("Open a video or audio file before cutting."));
        return;
    }

    if (m_session->outMs() <= m_session->inMs()) {
        QMessageBox::warning(this, tr("Invalid Range"),
                             tr("Set the Out point after the In point before cutting."));
        return;
    }

    // Cutting the entire clip would leave nothing behind. Refuse with a
    // clear message rather than producing an empty output the user has to
    // diagnose later.
    if (m_session->inMs() == 0
        && m_session->outMs() >= m_session->mediaInfo().durationMs) {
        QMessageBox::information(this, tr("Nothing Would Remain"),
                                 tr("The selection covers the entire clip; "
                                    "cutting it would leave nothing behind."));
        return;
    }

    if (m_cropRunner->isRunning() || m_trimRunner->isRunning() || m_cutRunner->isRunning()) {
        QMessageBox::information(this, tr("Operation In Progress"),
                                 tr("Wait for the current operation to finish."));
        return;
    }

    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
        QMessageBox::critical(this, tr("ffmpeg Not Found"),
                              tr("ffmpeg was not found on PATH.\n\nInstall it with:\n  sudo pacman -S ffmpeg"));
        return;
    }

    {
        const auto answer = QMessageBox::question(
            this, tr("Cut Selection"),
            tr("Remove the current [In, Out] selection from the working clip?\n\n"
               "This action cannot be undone. Your original file on disk will "
               "not be modified \u2014 only the in-app working clip changes."),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Yes);
        if (answer != QMessageBox::Yes) return;
    }

    // Reserve a unique output path that preserves the source extension, the
    // same way Crop does. The runner overwrites it; we just need a name no
    // other process is going to grab from under us.
    const QString sourcePath = m_session->filePath();
    const QString sourceExt  = QFileInfo(sourcePath).suffix();
    const QString suffix     = sourceExt.isEmpty() ? QStringLiteral("mkv") : sourceExt;
    const QString templatePath = QDir::temp().filePath(
        QStringLiteral("vtrim-cut-XXXXXX.") + suffix);

    QTemporaryFile tempFile(templatePath);
    tempFile.setAutoRemove(false);
    if (!tempFile.open()) {
        QMessageBox::critical(this, tr("Cut Failed"),
                              tr("Could not create a temporary file for the cut output."));
        return;
    }
    const QString outputPath = tempFile.fileName();
    tempFile.close();

    m_cutPendingOutput = outputPath;

    m_cutProgressDialog = new QProgressDialog(tr("Cutting selection..."),
                                              tr("Cancel"), 0, 100, this);
    m_cutProgressDialog->setWindowModality(Qt::WindowModal);
    m_cutProgressDialog->setMinimumDuration(0);
    m_cutProgressDialog->setAutoClose(false);
    m_cutProgressDialog->setAutoReset(false);
    m_cutProgressDialog->setValue(0);
    connect(m_cutProgressDialog, &QProgressDialog::canceled,
            m_cutRunner, &FfmpegCutRunner::cancel);

    m_cutRunner->start(sourcePath, outputPath,
                       m_session->inMs(), m_session->outMs(),
                       m_session->mediaInfo().durationMs);
}

void MainWindow::onAboutTriggered()
{
    QMessageBox::about(this, tr("About Video Trimmer"),
        tr("<h3>Video Trimmer %1</h3>"
           "<p>A simple millisecond-precision video and audio trimmer built with Qt 6 and FFmpeg.</p>"
           "<p>Play (Space, L, or the Play button) always plays the current trim selection: "
           "if the playhead is outside [In, Out] it jumps to In. With <b>Loop</b> off, "
           "playback pauses at Out; with Loop on, it jumps back to In and keeps playing.</p>"
           "<p>The <b>In</b> and <b>Out</b> fields in the transport bar accept direct "
           "timestamp input (e.g. <code>00:12:25</code>, <code>12:25.500</code>, "
           "<code>90</code>). Press Enter to commit.</p>"
           "<p><b>Crop to Selection</b> (Ctrl+K) reduces the working clip to the "
           "current [In, Out] and reloads the player so you can keep trimming "
           "from there. <b>Cut Selection</b> (Ctrl+Shift+K) does the inverse: "
           "it removes the [In, Out] segment and stitches the rest back together, "
           "so you can chain multiple cuts on the same clip. Both use stream copy, "
           "so they're fast and lossless; the original file on disk is never modified.</p>"
           "<p><b>Keyboard shortcuts</b><br>"
           "Space: Play/Pause selection &nbsp;&middot;&nbsp; J / K / L: Rewind 1s / Pause / Play selection<br>"
           "Left/Right: Nudge playhead &plusmn;1 ms (Shift &times;10, Ctrl &times;100, Ctrl+Shift &times;1000)<br>"
           "I / O: Set In / Out at current playhead<br>"
           "[ / ]: Nudge In point &minus; / + (with Shift/Ctrl modifiers as above)<br>"
           "Alt+[ / Alt+]: Nudge Out point &minus; / + (with Shift/Ctrl modifiers)<br>"
           "Ctrl+K: Crop to selection &nbsp;&middot;&nbsp; Ctrl+Shift+K: Cut selection &nbsp;&middot;&nbsp; Ctrl+E: Export trimmed&hellip;</p>")
        .arg(QApplication::applicationVersion()));
}

void MainWindow::onClearRecentTriggered()
{
    m_recentFiles.clear();
    QSettings s;
    s.setValue(kRecentFilesKey, m_recentFiles);
    updateRecentMenu();
}

void MainWindow::onAudioMenuAboutToShow()
{
    rebuildAudioOutputMenu();
}

void MainWindow::onAudioDeviceTriggered()
{
    auto *act = qobject_cast<QAction *>(sender());
    if (!act) return;

    m_audioDeviceId = act->data().toByteArray();
    QSettings s;
    s.setValue(kAudioDeviceIdKey, m_audioDeviceId);

    if (m_player) {
        m_player->setAudioDeviceById(m_audioDeviceId);
    }
}

void MainWindow::rebuildAudioOutputMenu()
{
    if (!m_outputMenu || !m_outputGroup) return;

    // Clear the menu and the action group together so we don't leak orphan
    // checked actions across rebuilds.
    qDeleteAll(m_outputGroup->actions());
    m_outputMenu->clear();

    auto *defaultAct = m_outputMenu->addAction(tr("Follow System Default"));
    defaultAct->setCheckable(true);
    defaultAct->setActionGroup(m_outputGroup);
    defaultAct->setData(QByteArray());
    defaultAct->setChecked(m_audioDeviceId.isEmpty());
    connect(defaultAct, &QAction::triggered,
            this, &MainWindow::onAudioDeviceTriggered);

    m_outputMenu->addSeparator();

    bool sawCurrent = false;
    for (const QAudioDevice &dev : QMediaDevices::audioOutputs()) {
        auto *act = m_outputMenu->addAction(dev.description());
        act->setCheckable(true);
        act->setActionGroup(m_outputGroup);
        act->setData(dev.id());
        const bool isCurrent = !m_audioDeviceId.isEmpty() && dev.id() == m_audioDeviceId;
        act->setChecked(isCurrent);
        if (isCurrent) sawCurrent = true;
        connect(act, &QAction::triggered,
                this, &MainWindow::onAudioDeviceTriggered);
    }

    // The previously-pinned device is no longer present (e.g. unplugged).
    // Don't lose the preference, but show "Follow System Default" as active.
    if (!m_audioDeviceId.isEmpty() && !sawCurrent) {
        defaultAct->setChecked(true);
    }
}

void MainWindow::onRecentTriggered()
{
    auto *act = qobject_cast<QAction *>(sender());
    if (!act) return;
    const QString path = act->data().toString();
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("File Missing"),
                             tr("The file no longer exists:\n%1").arg(path));
        m_recentFiles.removeAll(path);
        QSettings s;
        s.setValue(kRecentFilesKey, m_recentFiles);
        updateRecentMenu();
        return;
    }
    openFile(path);
}

void MainWindow::onSessionOpened(const QString &path)
{
    // A crop / cut result feeds back into the same VideoSession as a normal
    // open, but it should not be treated as a fresh user-opened file: skip
    // the recents list, surface the original filename in the title, and
    // retire the previous working-clip temp now that the player is about to
    // bind to the new one.
    const bool isWorkingClipResult =
        !m_workingClipTempFiles.isEmpty() && m_workingClipTempFiles.last() == path;

    QString displayName;
    if (isWorkingClipResult) {
        displayName = m_workingClipDisplayName.isEmpty()
                          ? QFileInfo(path).fileName()
                          : m_workingClipDisplayName;
        displayName += QStringLiteral(" (edited)");

        // Drop every prior working-clip temp - only the newest one is in
        // use now. Done after the player is told to clear its source in the
        // crop / cut finished handler, so no stale handle holds the file open.
        if (m_workingClipTempFiles.size() > 1) {
            for (int i = 0; i < m_workingClipTempFiles.size() - 1; ++i) {
                QFile::remove(m_workingClipTempFiles.at(i));
            }
            const QString kept = m_workingClipTempFiles.last();
            m_workingClipTempFiles.clear();
            m_workingClipTempFiles.append(kept);
        }
    } else {
        // A genuine user-initiated open: tear down any leftover working-clip
        // state so this file's title and recents behave normally.
        cleanupWorkingClipTempFiles();
        m_workingClipDisplayName.clear();
        displayName = QFileInfo(path).fileName();
    }

    setWindowTitle(tr("%1 - Video Trimmer").arg(displayName));
    m_player->setSource(path);
    m_timeline->setPosition(0);
    m_transport->setControlsEnabled(true);
    if (!isWorkingClipResult) {
        addToRecentFiles(path);
    }
    updateActionsEnabled();
}

void MainWindow::onMediaInfoUpdated(const MediaInfo &info)
{
    m_timeline->setDuration(info.durationMs);
    m_timeline->setInOut(0, info.durationMs);
    m_transport->setDuration(info.durationMs);
    // Now that ffprobe has confirmed whether the file actually has video,
    // flip the player into audio-mode (or back out of it) so audio-only
    // files don't show a perpetually black video pane.
    if (m_player) {
        const QString caption = QFileInfo(m_session->filePath()).fileName();
        m_player->setAudioOnlyMode(info.isAudioOnly(), caption);
    }
    updateInfoLabel();
}

void MainWindow::onProbeFailed(const QString &error)
{
    QMessageBox::warning(this, tr("Could Not Read Media"), error);
}

void MainWindow::onPlayerPosition(qint64 ms)
{
    m_timeline->setPosition(ms);
    m_transport->setPosition(ms);

    // While playing, enforce the trim selection boundary at Out:
    // - loop enabled  -> jump back to In and keep playing
    // - loop disabled -> pause at Out
    if (m_player->playbackState() == QMediaPlayer::PlayingState && hasValidRange()) {
        const qint64 out = m_session->outMs();
        if (ms >= out) {
            if (m_loopEnabled) {
                m_player->setPosition(m_session->inMs());
            } else {
                m_player->pause();
                m_player->setPosition(out);
            }
        }
    }
}

void MainWindow::onPlayerDuration(qint64 ms)
{
    // QMediaPlayer reports duration once decoded; let it override ffprobe if newer.
    if (ms > 0 && ms != m_timeline->durationMs()) {
        m_timeline->setDuration(ms);
        m_transport->setDuration(ms);
    }
}

void MainWindow::onPlayerError(const QString &message)
{
    QMessageBox::warning(this, tr("Playback Error"), message);
}

void MainWindow::onTimelineSeek(qint64 ms)
{
    m_player->setPosition(ms);
}

void MainWindow::onTimelineInOut(qint64 inMs, qint64 outMs)
{
    m_session->setInOut(inMs, outMs);
    updateInfoLabel();
}

// -----------------------------------------------------------------------------
// Drag & drop

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    qInfo() << "[DnD] MainWindow::dragEnterEvent fired; formats ="
            << (event->mimeData() ? event->mimeData()->formats() : QStringList{})
            << " urls =" << (event->mimeData() ? event->mimeData()->urls() : QList<QUrl>{});
    if (event->mimeData()->hasUrls()) {
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                event->acceptProposedAction();
                return;
            }
        }
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    qInfo() << "[DnD] MainWindow::dropEvent fired; urls ="
            << (event->mimeData() ? event->mimeData()->urls() : QList<QUrl>{});
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            openFile(url.toLocalFile());
            event->acceptProposedAction();
            return;
        }
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (!watched || !watched->isWidgetType()) {
        return QMainWindow::eventFilter(watched, event);
    }

    // Only consider events for widgets that belong to this main window.
    auto *w = qobject_cast<QWidget *>(watched);
    if (!w || w->window() != this) {
        return QMainWindow::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto *e = static_cast<QDragMoveEvent *>(event);
        qInfo() << "[DnD] eventFilter Drag" << (event->type() == QEvent::DragEnter ? "Enter" : "Move")
                << " on" << watched->metaObject()->className()
                << "name=" << watched->objectName()
                << " formats=" << (e->mimeData() ? e->mimeData()->formats() : QStringList{});
        if (e->mimeData() && e->mimeData()->hasUrls()) {
            for (const QUrl &url : e->mimeData()->urls()) {
                if (url.isLocalFile()) {
                    e->acceptProposedAction();
                    return true;
                }
            }
        }
        break;
    }
    case QEvent::Drop: {
        auto *e = static_cast<QDropEvent *>(event);
        qInfo() << "[DnD] eventFilter Drop on" << watched->metaObject()->className()
                << "name=" << watched->objectName()
                << " urls=" << (e->mimeData() ? e->mimeData()->urls() : QList<QUrl>{});
        if (e->mimeData() && e->mimeData()->hasUrls()) {
            for (const QUrl &url : e->mimeData()->urls()) {
                if (url.isLocalFile()) {
                    openFile(url.toLocalFile());
                    e->acceptProposedAction();
                    return true;
                }
            }
        }
        break;
    }
    default:
        break;
    }

    return QMainWindow::eventFilter(watched, event);
}

// -----------------------------------------------------------------------------
// Keyboard

qint64 MainWindow::nudgeStepMs(Qt::KeyboardModifiers mods) const
{
    const bool shift = mods & Qt::ShiftModifier;
    const bool ctrl  = mods & Qt::ControlModifier;
    if (ctrl && shift) return 1000;
    if (ctrl)          return 100;
    if (shift)         return 10;
    return 1;
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (!m_session->isOpen()) {
        QMainWindow::keyPressEvent(event);
        return;
    }

    const auto mods   = event->modifiers();
    const qint64 dur  = m_session->mediaInfo().durationMs;
    const qint64 pos  = m_player->position();
    const qint64 step = nudgeStepMs(mods);

    auto seek = [this, dur](qint64 ms) {
        ms = std::clamp<qint64>(ms, 0, dur);
        m_player->pause();
        m_player->setPosition(ms);
    };

    switch (event->key()) {
    case Qt::Key_Space:
        togglePlayPauseInRange();
        return;

    case Qt::Key_I:
        m_session->setIn(pos);
        return;
    case Qt::Key_O:
        m_session->setOut(pos);
        return;

    case Qt::Key_J:
        seek(pos - 1000);
        return;
    case Qt::Key_K:
        m_player->pause();
        return;
    case Qt::Key_L:
        playInRange();
        return;

    case Qt::Key_Left:
        seek(pos - step);
        return;
    case Qt::Key_Right:
        seek(pos + step);
        return;

    case Qt::Key_BracketLeft: {
        const qint64 delta = -step;
        if (mods & Qt::AltModifier) m_session->setOut(m_session->outMs() + delta);
        else                        m_session->setIn(m_session->inMs() + delta);
        return;
    }
    case Qt::Key_BracketRight: {
        const qint64 delta = step;
        if (mods & Qt::AltModifier) m_session->setOut(m_session->outMs() + delta);
        else                        m_session->setIn(m_session->inMs() + delta);
        return;
    }

    case Qt::Key_Home:
        seek(0);
        return;
    case Qt::Key_End:
        seek(dur);
        return;
    }

    QMainWindow::keyPressEvent(event);
}

// -----------------------------------------------------------------------------
// File / state helpers

void MainWindow::openFile(const QString &path)
{
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, tr("File Not Found"),
                             tr("Cannot find:\n%1").arg(path));
        return;
    }
    m_session->openFile(path);
}

void MainWindow::addToRecentFiles(const QString &path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    m_recentFiles.removeAll(abs);
    m_recentFiles.prepend(abs);
    while (m_recentFiles.size() > kMaxRecentFiles) {
        m_recentFiles.removeLast();
    }
    QSettings s;
    s.setValue(kRecentFilesKey, m_recentFiles);
    updateRecentMenu();
}

void MainWindow::updateRecentMenu()
{
    const int count = std::min<int>(m_recentFiles.size(), m_recentActions.size());
    for (int i = 0; i < count; ++i) {
        const QString &path = m_recentFiles.at(i);
        m_recentActions[i]->setText(QStringLiteral("&%1  %2")
                                        .arg(i + 1)
                                        .arg(QFileInfo(path).fileName()));
        m_recentActions[i]->setData(path);
        m_recentActions[i]->setToolTip(path);
        m_recentActions[i]->setVisible(true);
    }
    for (int i = count; i < m_recentActions.size(); ++i) {
        m_recentActions[i]->setVisible(false);
    }
    if (m_recentMenu) {
        m_recentMenu->setEnabled(count > 0);
    }
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue(kWindowGeometryKey, saveGeometry());
    s.setValue(kWindowStateKey,    saveState());
    s.setValue(kRecentFilesKey,    m_recentFiles);
}

void MainWindow::loadSettings()
{
    QSettings s;
    const QByteArray geom = s.value(kWindowGeometryKey).toByteArray();
    if (!geom.isEmpty()) {
        restoreGeometry(geom);
    } else {
        resize(1100, 720);
    }
    const QByteArray state = s.value(kWindowStateKey).toByteArray();
    if (!state.isEmpty()) restoreState(state);

    m_recentFiles = s.value(kRecentFilesKey).toStringList();
    while (m_recentFiles.size() > kMaxRecentFiles) m_recentFiles.removeLast();

    m_loopEnabled = s.value(kLoopEnabledKey, false).toBool();
    if (m_transport) m_transport->setLoopEnabled(m_loopEnabled);

    m_audioDeviceId = s.value(kAudioDeviceIdKey).toByteArray();
    if (m_player && !m_audioDeviceId.isEmpty()) {
        m_player->setAudioDeviceById(m_audioDeviceId);
    }
    rebuildAudioOutputMenu();
}

void MainWindow::updateInfoLabel()
{
    if (!m_session->isOpen()) {
        m_infoLabel->setText(tr("No media loaded"));
        return;
    }
    const MediaInfo &info = m_session->mediaInfo();
    if (!info.isValid()) {
        m_infoLabel->setText(tr("%1 - probing...").arg(QFileInfo(m_session->filePath()).fileName()));
        return;
    }
    const qint64 selectedMs = std::max<qint64>(0, m_session->outMs() - m_session->inMs());
    const QString selectionPart =
        tr("Selection: %1 (%2 ms)")
            .arg(TimeFormat::msToHms(selectedMs))
            .arg(selectedMs);

    if (info.isAudioOnly()) {
        // Audio-only: render the codec / sample-rate / channel count pieces
        // we actually have, then append the trim selection summary.
        QStringList parts;
        parts << (info.audioCodec.isEmpty() ? tr("audio") : info.audioCodec);
        if (info.audioSampleRate > 0) {
            parts << tr("%1 Hz").arg(info.audioSampleRate);
        }
        if (info.audioChannels > 0) {
            parts << (info.audioChannels == 1 ? tr("mono")
                    : info.audioChannels == 2 ? tr("stereo")
                    : tr("%1 ch").arg(info.audioChannels));
        }
        m_infoLabel->setText(
            tr("%1  |  %2").arg(parts.join(QStringLiteral(" / ")), selectionPart));
        return;
    }

    // Prefer avg_frame_rate for the headline number when both are available,
    // since r_frame_rate can be wildly inflated for variable-frame-rate sources.
    // Append the raw rate parenthetically when the two disagree meaningfully so
    // the user can tell at a glance that re-encoding may renormalize timing.
    const double primary = info.avgFps > 0.0 ? info.avgFps : info.fps;
    QString fpsText = tr("?");
    if (primary > 0.0) {
        fpsText = QString::number(primary, 'f', 3);
        if (info.avgFps > 0.0 && info.fps > 0.0
            && std::abs(info.fps - info.avgFps) > 0.5) {
            fpsText += tr(" (raw %1)").arg(QString::number(info.fps, 'f', 3));
        }
    }

    m_infoLabel->setText(
        tr("%1x%2  %3 fps  %4 / %5  |  %6")
            .arg(info.width)
            .arg(info.height)
            .arg(fpsText)
            .arg(info.videoCodec.isEmpty() ? tr("?") : info.videoCodec)
            .arg(info.audioCodec.isEmpty() ? tr("none") : info.audioCodec)
            .arg(selectionPart));
}

void MainWindow::updateActionsEnabled()
{
    const bool open = m_session->isOpen();
    if (m_actionClose)  m_actionClose->setEnabled(open);
    if (m_actionExport) m_actionExport->setEnabled(open);
    if (m_actionCrop)   m_actionCrop->setEnabled(open);
    if (m_actionCut)    m_actionCut->setEnabled(open);
}

void MainWindow::cleanupWorkingClipTempFiles()
{
    for (const QString &path : std::as_const(m_workingClipTempFiles)) {
        QFile::remove(path);
    }
    m_workingClipTempFiles.clear();
}

bool MainWindow::hasValidRange() const
{
    return m_session->isOpen() && m_session->outMs() > m_session->inMs();
}

void MainWindow::playInRange()
{
    if (!m_session->isOpen()) return;

    if (hasValidRange()) {
        const qint64 in  = m_session->inMs();
        const qint64 out = m_session->outMs();
        const qint64 pos = m_player->position();
        if (pos < in || pos >= out) {
            m_player->setPosition(in);
        }
    }
    m_player->play();
}

void MainWindow::togglePlayPauseInRange()
{
    if (m_player->playbackState() == QMediaPlayer::PlayingState) {
        m_player->pause();
    } else {
        playInRange();
    }
}

void MainWindow::playFromIn()
{
    if (!m_session->isOpen()) return;
    // Always jump back to the In marker, regardless of whether we're
    // currently inside [In, Out]. That's the whole point of this action:
    // it's the "restart the selection" counterpart to the resume-style
    // Play button. When no In has been set, inMs() defaults to 0, so this
    // degrades gracefully to "play from the very beginning".
    m_player->setPosition(m_session->inMs());
    m_player->play();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_trimRunner && m_trimRunner->isRunning()) {
        const auto answer = QMessageBox::question(
            this, tr("Export In Progress"),
            tr("An export is still running. Cancel it and quit?"));
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_trimRunner->cancel();
    }
    if (m_cropRunner && m_cropRunner->isRunning()) {
        const auto answer = QMessageBox::question(
            this, tr("Crop In Progress"),
            tr("A crop is still running. Cancel it and quit?"));
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_cropRunner->cancel();
        // Remove the in-flight output now; cancellation may have left a
        // partial file behind that nothing else will reap.
        if (!m_cropPendingOutput.isEmpty()) {
            QFile::remove(m_cropPendingOutput);
            m_cropPendingOutput.clear();
        }
    }
    if (m_cutRunner && m_cutRunner->isRunning()) {
        const auto answer = QMessageBox::question(
            this, tr("Cut In Progress"),
            tr("A cut is still running. Cancel it and quit?"));
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_cutRunner->cancel();
        // The cut runner cleans its intermediates on cancel, but the final
        // output it was writing into may also be a partial. Reap it here.
        if (!m_cutPendingOutput.isEmpty()) {
            QFile::remove(m_cutPendingOutput);
            m_cutPendingOutput.clear();
        }
    }
    // Stop the player so its file handles drop before we delete the temp
    // files underneath it. The window is going away anyway.
    if (m_player) {
        m_player->pause();
        m_player->clearSource();
    }
    cleanupWorkingClipTempFiles();
    saveSettings();
    QMainWindow::closeEvent(event);
}
