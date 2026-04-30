#include "timeline/TimelineWidget.h"

#include "util/TimeFormat.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kHandleWidth      = 10;
constexpr int kPlayheadWidth    = 2;
constexpr int kHorizontalPad    = 12;
constexpr int kPreferredHeight  = 72;
constexpr int kTrackThickness   = 14;
}  // namespace

TimelineWidget::TimelineWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(kPreferredHeight);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize TimelineWidget::sizeHint() const        { return QSize(800, kPreferredHeight); }
QSize TimelineWidget::minimumSizeHint() const { return QSize(200, kPreferredHeight); }

int TimelineWidget::trackY() const      { return height() / 2 - trackHeight() / 2; }
int TimelineWidget::trackHeight() const { return kTrackThickness; }
int TimelineWidget::trackLeft() const   { return kHorizontalPad; }
int TimelineWidget::trackRight() const  { return width() - kHorizontalPad; }
int TimelineWidget::trackWidth() const  { return std::max(0, trackRight() - trackLeft()); }

int TimelineWidget::xForMs(qint64 ms) const
{
    if (m_durationMs <= 0 || trackWidth() <= 0) return trackLeft();
    const double t = double(std::clamp<qint64>(ms, 0, m_durationMs)) / double(m_durationMs);
    return trackLeft() + int(std::round(t * trackWidth()));
}

qint64 TimelineWidget::msForX(int x) const
{
    if (m_durationMs <= 0 || trackWidth() <= 0) return 0;
    const int clamped = std::clamp(x, trackLeft(), trackRight());
    const double t = double(clamped - trackLeft()) / double(trackWidth());
    return qint64(std::round(t * double(m_durationMs)));
}

QRect TimelineWidget::inHandleRect() const
{
    const int x = xForMs(m_inMs);
    return QRect(x - kHandleWidth / 2, trackY() - 4, kHandleWidth, trackHeight() + 8);
}

QRect TimelineWidget::outHandleRect() const
{
    const int x = xForMs(m_outMs);
    return QRect(x - kHandleWidth / 2, trackY() - 4, kHandleWidth, trackHeight() + 8);
}

QRect TimelineWidget::playheadRect() const
{
    const int x = xForMs(m_positionMs);
    return QRect(x - kPlayheadWidth / 2, trackY() - 8, kPlayheadWidth, trackHeight() + 16);
}

TimelineWidget::DragTarget TimelineWidget::hitTest(const QPoint &p) const
{
    if (inHandleRect().adjusted(-4, -4, 4, 4).contains(p))   return DragTarget::InHandle;
    if (outHandleRect().adjusted(-4, -4, 4, 4).contains(p))  return DragTarget::OutHandle;
    if (playheadRect().adjusted(-6, -6, 6, 6).contains(p))   return DragTarget::Playhead;
    return DragTarget::None;
}

void TimelineWidget::setDuration(qint64 ms)
{
    if (ms < 0) ms = 0;
    if (m_durationMs == ms) return;
    m_durationMs = ms;
    if (m_outMs == 0 || m_outMs > m_durationMs) m_outMs = m_durationMs;
    if (m_inMs > m_outMs)                       m_inMs  = m_outMs;
    if (m_positionMs > m_durationMs)            m_positionMs = m_durationMs;
    update();
}

void TimelineWidget::setPosition(qint64 ms)
{
    if (m_durationMs > 0) ms = std::clamp<qint64>(ms, 0, m_durationMs);
    if (m_positionMs == ms) return;
    m_positionMs = ms;
    update();
}

void TimelineWidget::setIn(qint64 ms)
{
    ms = std::clamp<qint64>(ms, 0, m_outMs > 0 ? m_outMs : m_durationMs);
    if (m_inMs == ms) return;
    m_inMs = ms;
    emit inOutChanged(m_inMs, m_outMs);
    update();
}

void TimelineWidget::setOut(qint64 ms)
{
    ms = std::clamp<qint64>(ms, m_inMs, m_durationMs);
    if (m_outMs == ms) return;
    m_outMs = ms;
    emit inOutChanged(m_inMs, m_outMs);
    update();
}

void TimelineWidget::setInOut(qint64 inMs, qint64 outMs)
{
    inMs  = std::clamp<qint64>(inMs,  0,    m_durationMs);
    outMs = std::clamp<qint64>(outMs, inMs, m_durationMs);
    if (m_inMs == inMs && m_outMs == outMs) return;
    m_inMs  = inMs;
    m_outMs = outMs;
    update();
}

void TimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QPalette &pal = palette();

    const QRect track(trackLeft(), trackY(), trackWidth(), trackHeight());
    p.fillRect(track, pal.color(QPalette::AlternateBase));
    p.setPen(pal.color(QPalette::Mid));
    p.drawRect(track);

    if (m_durationMs > 0) {
        const int xIn  = xForMs(m_inMs);
        const int xOut = xForMs(m_outMs);
        const QRect sel(xIn, trackY(), std::max(0, xOut - xIn), trackHeight());
        QColor selColor = pal.color(QPalette::Highlight);
        selColor.setAlpha(140);
        p.fillRect(sel, selColor);

        p.setPen(pal.color(QPalette::Mid));
        for (int i = 0; i <= 10; ++i) {
            const qint64 ms = m_durationMs * i / 10;
            const int x = xForMs(ms);
            p.drawLine(x, trackY() + trackHeight(), x, trackY() + trackHeight() + 4);
        }

        const QColor handleColor = pal.color(QPalette::Highlight).darker(140);
        p.setPen(Qt::NoPen);
        p.setBrush(handleColor);
        p.drawRect(inHandleRect());
        p.drawRect(outHandleRect());

        p.setPen(QPen(pal.color(QPalette::WindowText), kPlayheadWidth));
        const int xPos = xForMs(m_positionMs);
        p.drawLine(xPos, trackY() - 8, xPos, trackY() + trackHeight() + 8);

        p.setPen(pal.color(QPalette::WindowText));
        QFont f = p.font();
        f.setPointSizeF(f.pointSizeF() * 0.85);
        p.setFont(f);

        const QString currentText = TimeFormat::msToHms(m_positionMs);
        const QString inText      = tr("In  %1").arg(TimeFormat::msToHms(m_inMs));
        const QString outText     = tr("Out %1").arg(TimeFormat::msToHms(m_outMs));

        const QRect topArea(trackLeft(), 0, trackWidth(), trackY());
        p.drawText(topArea, Qt::AlignLeft  | Qt::AlignVCenter, inText);
        p.drawText(topArea, Qt::AlignCenter,                   currentText);
        p.drawText(topArea, Qt::AlignRight | Qt::AlignVCenter, outText);
    } else {
        p.setPen(pal.color(QPalette::Mid));
        p.drawText(rect(), Qt::AlignCenter, tr("No video loaded"));
    }
}

void TimelineWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    if (m_durationMs <= 0) return;

    m_drag = hitTest(event->pos());
    if (m_drag == DragTarget::None) {
        m_drag = DragTarget::Playhead;
        const qint64 ms = msForX(event->pos().x());
        m_positionMs = ms;
        emit seekRequested(ms);
        update();
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_drag == DragTarget::None) {
        switch (hitTest(event->pos())) {
        case DragTarget::InHandle:
        case DragTarget::OutHandle: setCursor(Qt::SizeHorCursor); break;
        case DragTarget::Playhead:  setCursor(Qt::SplitHCursor);  break;
        default:                    setCursor(Qt::ArrowCursor);
        }
        return;
    }

    if (m_durationMs <= 0) return;
    const qint64 ms = msForX(event->pos().x());

    switch (m_drag) {
    case DragTarget::Playhead:
        m_positionMs = ms;
        emit seekRequested(ms);
        break;
    case DragTarget::InHandle:
        m_inMs = std::clamp<qint64>(ms, 0, m_outMs);
        emit inOutChanged(m_inMs, m_outMs);
        break;
    case DragTarget::OutHandle:
        m_outMs = std::clamp<qint64>(ms, m_inMs, m_durationMs);
        emit inOutChanged(m_inMs, m_outMs);
        break;
    case DragTarget::None:
        break;
    }
    update();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;
    m_drag = DragTarget::None;
}
