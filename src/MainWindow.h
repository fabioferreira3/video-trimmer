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

private slots:
    void onOpenTriggered();
    void onCloseTriggered();
    void onExportTriggered();
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

    // Play / toggle that constrain playback to the current [in, out] selection.
    void playInRange();
    void togglePlayPauseInRange();
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
    QAction *m_actionAbout  = nullptr;
    QAction *m_actionQuit   = nullptr;

    QMenu        *m_outputMenu  = nullptr;
    QActionGroup *m_outputGroup = nullptr;
    // Persisted audio output preference: empty == "follow system default".
    QByteArray    m_audioDeviceId;

    FfmpegTrimRunner       *m_trimRunner = nullptr;
    QPointer<QProgressDialog> m_progressDialog;

    bool m_loopEnabled = false;
};
