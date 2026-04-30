#pragma once

#include <QWidget>

class TimelineWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget *parent = nullptr);

    qint64 positionMs() const { return m_positionMs; }
    qint64 inMs() const { return m_inMs; }
    qint64 outMs() const { return m_outMs; }
    qint64 durationMs() const { return m_durationMs; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

public slots:
    void setDuration(qint64 ms);
    void setPosition(qint64 ms);
    void setIn(qint64 ms);
    void setOut(qint64 ms);
    void setInOut(qint64 inMs, qint64 outMs);

signals:
    void seekRequested(qint64 ms);
    void inOutChanged(qint64 inMs, qint64 outMs);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    enum class DragTarget { None, Playhead, InHandle, OutHandle };

    qint64 m_durationMs = 0;
    qint64 m_positionMs = 0;
    qint64 m_inMs = 0;
    qint64 m_outMs = 0;
    DragTarget m_drag = DragTarget::None;

    int trackY() const;
    int trackHeight() const;
    int trackLeft() const;
    int trackRight() const;
    int trackWidth() const;
    int xForMs(qint64 ms) const;
    qint64 msForX(int x) const;
    QRect inHandleRect() const;
    QRect outHandleRect() const;
    QRect playheadRect() const;
    DragTarget hitTest(const QPoint &p) const;
};
