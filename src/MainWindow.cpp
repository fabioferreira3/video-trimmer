#include "MainWindow.h"

#include "TrimDialog.h"
#include "VideoSession.h"
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
#include <QDragEnterEvent>
#include <QDropEvent>
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
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

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

    m_session     = new VideoSession(this);
    m_trimRunner  = new FfmpegTrimRunner(this);

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

    setCentralWidget(central);
}

void MainWindow::buildMenus()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));

    m_actionOpen = fileMenu->addAction(tr("&Open Video..."), this, &MainWindow::onOpenTriggered);
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
    m_actionExport = fileMenu->addAction(tr("&Export Trimmed..."), this, &MainWindow::onExportTriggered);
    m_actionExport->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));

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
    connect(m_transport, &TransportBar::volumeChanged,
            m_player, &PlayerWidget::setVolume);
    connect(m_transport, &TransportBar::setInClicked,
            this, [this]() { m_session->setIn(m_player->position()); });
    connect(m_transport, &TransportBar::setOutClicked,
            this, [this]() { m_session->setOut(m_player->position()); });
    connect(m_transport, &TransportBar::loopToggled,
            this, [this](bool on) {
                m_loopEnabled = on;
                QSettings s;
                s.setValue(kLoopEnabledKey, on);
            });

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
                                             tr("Trimmed video saved successfully."));
                } else {
                    QMessageBox::critical(this, tr("Export Failed"),
                                          errorTail.isEmpty()
                                              ? tr("ffmpeg exited with an error.")
                                              : errorTail);
                }
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
        tr("Open Video"),
        lastDir,
        tr("Video Files (*.mp4 *.mkv *.mov *.avi *.webm *.m4v *.ts *.mpg *.mpeg *.flv);;All Files (*)"));

    if (path.isEmpty()) return;

    QSettings s;
    s.setValue(QStringLiteral("lastDir"), QFileInfo(path).absolutePath());

    openFile(path);
}

void MainWindow::onCloseTriggered()
{
    m_session->close();
    m_player->clearSource();
    m_timeline->setDuration(0);
    m_timeline->setPosition(0);
    m_timeline->setInOut(0, 0);
    m_transport->setPosition(0);
    m_transport->setDuration(0);
    m_transport->setControlsEnabled(false);
    setWindowTitle(tr("Video Trimmer"));
    updateActionsEnabled();
    updateInfoLabel();
}

void MainWindow::onExportTriggered()
{
    if (!m_session->isOpen() || !m_session->mediaInfo().isValid()) {
        QMessageBox::information(this, tr("No Video"),
                                 tr("Open a video before exporting."));
        return;
    }

    if (m_session->outMs() <= m_session->inMs()) {
        QMessageBox::warning(this, tr("Invalid Range"),
                             tr("Set the Out point after the In point before exporting."));
        return;
    }

    if (m_trimRunner->isRunning()) {
        QMessageBox::information(this, tr("Export In Progress"),
                                 tr("An export is already running."));
        return;
    }

    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
        QMessageBox::critical(this, tr("ffmpeg Not Found"),
                              tr("ffmpeg was not found on PATH.\n\nInstall it with:\n  sudo pacman -S ffmpeg"));
        return;
    }

    TrimDialog dlg(m_session->filePath(), m_session->inMs(), m_session->outMs(), this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString output = dlg.outputPath();
    const auto mode = dlg.mode();

    m_progressDialog = new QProgressDialog(tr("Exporting trimmed video..."),
                                           tr("Cancel"), 0, 100, this);
    m_progressDialog->setWindowModality(Qt::WindowModal);
    m_progressDialog->setMinimumDuration(0);
    m_progressDialog->setAutoClose(false);
    m_progressDialog->setAutoReset(false);
    m_progressDialog->setValue(0);
    connect(m_progressDialog, &QProgressDialog::canceled,
            m_trimRunner, &FfmpegTrimRunner::cancel);

    m_trimRunner->start(m_session->filePath(), output,
                        m_session->inMs(), m_session->outMs(), mode);
}

void MainWindow::onAboutTriggered()
{
    QMessageBox::about(this, tr("About Video Trimmer"),
        tr("<h3>Video Trimmer %1</h3>"
           "<p>A simple millisecond-precision video trimmer built with Qt 6 and FFmpeg.</p>"
           "<p>Play (Space, L, or the Play button) always plays the current trim selection: "
           "if the playhead is outside [In, Out] it jumps to In. With <b>Loop</b> off, "
           "playback pauses at Out; with Loop on, it jumps back to In and keeps playing.</p>"
           "<p><b>Keyboard shortcuts</b><br>"
           "Space: Play/Pause selection &nbsp;&middot;&nbsp; J / K / L: Rewind 1s / Pause / Play selection<br>"
           "Left/Right: Nudge playhead &plusmn;1 ms (Shift &times;10, Ctrl &times;100, Ctrl+Shift &times;1000)<br>"
           "I / O: Set In / Out at current playhead<br>"
           "[ / ]: Nudge In point &minus; / + (with Shift/Ctrl modifiers as above)<br>"
           "Alt+[ / Alt+]: Nudge Out point &minus; / + (with Shift/Ctrl modifiers)</p>")
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
    setWindowTitle(tr("%1 - Video Trimmer").arg(QFileInfo(path).fileName()));
    m_player->setSource(path);
    m_timeline->setPosition(0);
    m_transport->setControlsEnabled(true);
    addToRecentFiles(path);
    updateActionsEnabled();
}

void MainWindow::onMediaInfoUpdated(const MediaInfo &info)
{
    m_timeline->setDuration(info.durationMs);
    m_timeline->setInOut(0, info.durationMs);
    m_transport->setDuration(info.durationMs);
    updateInfoLabel();
}

void MainWindow::onProbeFailed(const QString &error)
{
    QMessageBox::warning(this, tr("Could Not Read Video"), error);
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
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            openFile(url.toLocalFile());
            event->acceptProposedAction();
            return;
        }
    }
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
        m_infoLabel->setText(tr("No video loaded"));
        return;
    }
    const MediaInfo &info = m_session->mediaInfo();
    if (!info.isValid()) {
        m_infoLabel->setText(tr("%1 - probing...").arg(QFileInfo(m_session->filePath()).fileName()));
        return;
    }
    const qint64 selectedMs = std::max<qint64>(0, m_session->outMs() - m_session->inMs());
    const QString fpsText = info.fps > 0.0 ? QString::number(info.fps, 'f', 3) : tr("?");
    m_infoLabel->setText(
        tr("%1x%2  %3 fps  %4 / %5  |  Selection: %6 (%7 ms)")
            .arg(info.width)
            .arg(info.height)
            .arg(fpsText)
            .arg(info.videoCodec.isEmpty() ? tr("?") : info.videoCodec)
            .arg(info.audioCodec.isEmpty() ? tr("none") : info.audioCodec)
            .arg(TimeFormat::msToHms(selectedMs))
            .arg(selectedMs));
}

void MainWindow::updateActionsEnabled()
{
    const bool open = m_session->isOpen();
    if (m_actionClose)  m_actionClose->setEnabled(open);
    if (m_actionExport) m_actionExport->setEnabled(open);
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
    saveSettings();
    QMainWindow::closeEvent(event);
}
