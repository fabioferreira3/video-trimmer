#pragma once

#include <QDialog>
#include <QString>

#include "ffmpeg/FfmpegTrimRunner.h"

class QLineEdit;
class QRadioButton;

class TrimDialog : public QDialog
{
    Q_OBJECT

public:
    TrimDialog(const QString &sourcePath,
               qint64 inMs,
               qint64 outMs,
               QWidget *parent = nullptr);

    QString outputPath() const;
    FfmpegTrimRunner::Mode mode() const;

private slots:
    void onBrowseClicked();
    void onModeChanged();
    void onAcceptRequested();

private:
    QString m_sourcePath;
    qint64  m_inMs;
    qint64  m_outMs;

    QLineEdit    *m_outputEdit  = nullptr;
    QRadioButton *m_radioPrecise = nullptr;
    QRadioButton *m_radioFast    = nullptr;
};
