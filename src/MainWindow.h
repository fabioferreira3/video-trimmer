#pragma once

#include <QList>
#include <QMainWindow>
#include <QPointer>
#include <QStringList>

class VideoSession;
class PlayerWidget;
class TransportBar;
class TimelineWidget;
class FfmpegTrimRunner;
class FfmpegCutRunner;
struct MediaInfo;

class QAction;
class QActionGroup;
class QLabel;
class QMenu;
class QProgressDialog;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    // Application-wide DnD interceptor. QVideoWidget renders into a native
    // surface (especially on Wayland) which never delivers drag/drop to the
    // QMainWindow's own dragEnterEvent/dropEvent. A qApp-level event filter
    // catches the DnD events regardless of which child widget the platform
    // delivered them to, and routes the URL through openFile().
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onOpenTriggered();
    void onCloseTriggered();
    void onExportTriggered();
    void onCropTriggered();
    void onCutTriggered();
    void onAboutTriggered();
    void onClearRecentTriggered();
    void onRecentTriggered();
    void onAudioDeviceTriggered();
    void onAudioMenuAboutToShow();

    void onSessionOpened(const QString &path);
    void onMediaInfoUpdated(const MediaInfo &info);
    void onProbeFailed(const QString &error);

    void onPlayerPosition(qint64 ms);
    void onPlayerDuration(qint64 ms);
    void onPlayerError(const QString &message);

    void onTimelineSeek(qint64 ms);
    void onTimelineInOut(qint64 inMs, qint64 outMs);

private:
    void buildMenus();
    void buildStatusBar();
    void buildCentralLayout();
    void wireConnections();
    void rebuildAudioOutputMenu();

    void openFile(const QString &path);
    void addToRecentFiles(const QString &path);
    void updateRecentMenu();
    void saveSettings();
    void loadSettings();
    void updateInfoLabel();
    void updateActionsEnabled();
    // Delete every temp file produced by previous crop / cut operations and
    // forget about them. Safe to call repeatedly. Used both when starting a
    // fresh user-initiated open (so transient working-clip files don't leak
    // past the session that produced them) and on application exit.
    void cleanupWorkingClipTempFiles();

    // Play / toggle that constrain playback to the current [in, out] selection.
    void playInRange();
    void togglePlayPauseInRange();
    // Unconditionally seek to the current In marker and start playback.
    // Used by the "Play from Start" transport button and is deliberately
    // distinct from playInRange() (which only re-seeks when the current
    // position is outside the [In, Out] range, so it doubles as a "resume").
    void playFromIn();
    bool hasValidRange() const;

    qint64 nudgeStepMs(Qt::KeyboardModifiers mods) const;

    VideoSession   *m_session   = nullptr;
    PlayerWidget   *m_player    = nullptr;
    TransportBar   *m_transport = nullptr;
    TimelineWidget *m_timeline  = nullptr;
    QLabel         *m_infoLabel = nullptr;

    QMenu              *m_recentMenu = nullptr;
    QList<QAction *>    m_recentActions;
    QStringList         m_recentFiles;

    QAction *m_actionOpen   = nullptr;
    QAction *m_actionClose  = nullptr;
    QAction *m_actionExport = nullptr;
    QAction *m_actionCrop   = nullptr;
    QAction *m_actionCut    = nullptr;
    QAction *m_actionAbout  = nullptr;
    QAction *m_actionQuit   = nullptr;

    QMenu        *m_outputMenu  = nullptr;
    QActionGroup *m_outputGroup = nullptr;
    // Persisted audio output preference: empty == "follow system default".
    QByteArray    m_audioDeviceId;

    FfmpegTrimRunner       *m_trimRunner = nullptr;
    QPointer<QProgressDialog> m_progressDialog;

    // Crop ("trim in place") uses its own runner + progress dialog so its
    // signal wiring stays cleanly separated from the user-driven Export
    // pipeline. The Crop / Cut / Export entry points are mutually exclusive
    // at the user level - each one refuses to start while another is busy.
    FfmpegTrimRunner          *m_cropRunner = nullptr;
    QPointer<QProgressDialog>  m_cropProgressDialog;
    // Pending output path of the in-flight crop, empty when no crop is running.
    QString                    m_cropPendingOutput;

    // Cut ("remove [In, Out] from the working clip") uses its own runner
    // because, unlike crop, it's a multi-step pipeline (extract pre +
    // extract post + concat). The result is otherwise treated identically
    // to a crop result: it becomes the new working clip via a transient
    // temp file tracked in m_workingClipTempFiles.
    FfmpegCutRunner           *m_cutRunner = nullptr;
    QPointer<QProgressDialog>  m_cutProgressDialog;
    QString                    m_cutPendingOutput;

    // Temp file paths produced by past crop / cut operations on the current
    // working clip. Cleaned up when the user opens a different source,
    // closes the current one, or quits the app. We keep older entries around
    // until they're definitely no longer referenced by the player to avoid
    // yanking a file out from under QMediaPlayer mid-load.
    QStringList                m_workingClipTempFiles;
    // Friendly name to display in the window title once we've replaced the
    // session source with a transient working-clip temp file. Captures the
    // original user file name so iterative crops/cuts keep showing
    // "myvideo.mp4 (edited)" rather than the opaque /tmp/vtrim-...mp4 path.
    QString                    m_workingClipDisplayName;

    bool m_loopEnabled = false;
};
