#include "timeline/TimelineWidget.h"

#include "util/TimeFormat.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPolygon>
#include <algorithm>
#include <cmath>

namespace {
// Visual width of a trim handle "tab". Made larger than the original 10px so the
// user has a comfortably sized target to grab.
constexpr int kHandleWidth      = 16;
// How far the handle tab extends above and below the track (so the tab sticks
// out of the selection bar like a bracket).
constexpr int kHandleVOverhang  = 12;
// Extra padding around the handle that is still considered a hit. Keeps the
// click area generous without making the painted glyph look bloated.
constexpr int kHandleHitPad     = 4;
// Width of the actual mark line drawn at the handle's exact ms position.
constexpr int kHandleStem       = 2;
// Playhead drawing
constexpr int kPlayheadWidth    = 2;
constexpr int kPlayheadVOverhang = 14;
constexpr int kPlayheadHitPad   = 6;
// The triangular needle indicators that flank the playhead line.
constexpr int kPlayheadArrowH   = 7;
constexpr int kPlayheadArrowW   = 10;

constexpr int kHorizontalPad    = 18;
constexpr int kPreferredHeight  = 80;
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

// In handle: tab-shaped, extends mostly to the LEFT of the mark position so it
// never fights the Out handle for pixels even when In == Out.
QRect TimelineWidget::inHandleRect() const
{
    const int x = xForMs(m_inMs);
    const int top = trackY() - kHandleVOverhang;
    const int h   = trackHeight() + 2 * kHandleVOverhang;
    return QRect(x - kHandleWidth + kHandleStem, top, kHandleWidth, h);
}

// Out handle: tab-shaped, extends mostly to the RIGHT of the mark position.
QRect TimelineWidget::outHandleRect() const
{
    const int x = xForMs(m_outMs);
    const int top = trackY() - kHandleVOverhang;
    const int h   = trackHeight() + 2 * kHandleVOverhang;
    return QRect(x - kHandleStem + 1, top, kHandleWidth, h);
}

QRect TimelineWidget::playheadRect() const
{
    const int x = xForMs(m_positionMs);
    const int top = trackY() - kPlayheadVOverhang;
    const int h   = trackHeight() + 2 * kPlayheadVOverhang;
    return QRect(x - kPlayheadWidth / 2, top, kPlayheadWidth, h);
}

TimelineWidget::DragTarget TimelineWidget::hitTest(const QPoint &p) const
{
    const QRect inHit  = inHandleRect().adjusted(-kHandleHitPad, -kHandleHitPad,
                                                  kHandleHitPad,  kHandleHitPad);
    const QRect outHit = outHandleRect().adjusted(-kHandleHitPad, -kHandleHitPad,
                                                   kHandleHitPad,  kHandleHitPad);
    // Generous hit area for the playhead too: the line itself is 2px wide but we
    // accept clicks anywhere inside an arrow-sized box around it.
    const int phPadX = std::max(kPlayheadHitPad, kPlayheadArrowW / 2 + 2);
    const QRect phHit  = playheadRect().adjusted(-phPadX, -kPlayheadHitPad,
                                                  phPadX,  kPlayheadHitPad);

    const bool inOk  = inHit.contains(p);
    const bool outOk = outHit.contains(p);

    // Both handle hit boxes match (overlap region between very-close In and Out):
    // pick the one whose mark line is closer to the click. Ties go to whichever
    // side of the midpoint the click landed on, so a click on the left half of
    // the overlap selects In and on the right half selects Out.
    if (inOk && outOk) {
        const int xIn  = xForMs(m_inMs);
        const int xOut = xForMs(m_outMs);
        const int mid  = (xIn + xOut) / 2;
        return p.x() <= mid ? DragTarget::InHandle : DragTarget::OutHandle;
    }
    if (inOk)  return DragTarget::InHandle;
    if (outOk) return DragTarget::OutHandle;

    // Handles take priority over the playhead because they're the harder
    // targets to land on; the playhead is only matched when no handle is.
    if (phHit.contains(p)) return DragTarget::Playhead;
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

namespace {

// Build a left-facing trim bracket (the In handle) anchored at xMark. The
// vertical "stem" sits at the mark; the body extends to the left and is gently
// rounded on its outer edge.
QPainterPath makeInHandlePath(const QRect &r, int stem)
{
    const qreal radius = 3.0;
    const int innerRight = r.right() + 1;
    const int stemLeft   = innerRight - stem;
    QPainterPath path;
    path.moveTo(stemLeft, r.top());
    path.lineTo(r.left() + radius, r.top());
    path.quadTo(r.left(), r.top(), r.left(), r.top() + radius);
    path.lineTo(r.left(), r.bottom() - radius);
    path.quadTo(r.left(), r.bottom() + 1, r.left() + radius, r.bottom() + 1);
    path.lineTo(stemLeft, r.bottom() + 1);
    path.lineTo(stemLeft, r.top());
    path.closeSubpath();

    // The stem itself (a thin vertical block right at the mark line).
    path.addRect(QRectF(stemLeft, r.top(), stem, r.height()));
    return path;
}

QPainterPath makeOutHandlePath(const QRect &r, int stem)
{
    const qreal radius = 3.0;
    const int innerLeft = r.left();
    const int stemRight = innerLeft + stem;
    QPainterPath path;
    path.moveTo(stemRight, r.top());
    path.lineTo(r.right() + 1 - radius, r.top());
    path.quadTo(r.right() + 1, r.top(), r.right() + 1, r.top() + radius);
    path.lineTo(r.right() + 1, r.bottom() - radius);
    path.quadTo(r.right() + 1, r.bottom() + 1, r.right() + 1 - radius, r.bottom() + 1);
    path.lineTo(stemRight, r.bottom() + 1);
    path.lineTo(stemRight, r.top());
    path.closeSubpath();

    path.addRect(QRectF(innerLeft, r.top(), stem, r.height()));
    return path;
}

}  // namespace

void TimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QPalette &pal = palette();

    const QRect track(trackLeft(), trackY(), trackWidth(), trackHeight());
    p.fillRect(track, pal.color(QPalette::AlternateBase));
    p.setPen(pal.color(QPalette::Mid));
    p.drawRect(track);

    if (m_durationMs <= 0) {
        p.setPen(pal.color(QPalette::Mid));
        p.drawText(rect(), Qt::AlignCenter, tr("No media loaded"));
        return;
    }

    const int xIn  = xForMs(m_inMs);
    const int xOut = xForMs(m_outMs);

    // Selection fill.
    const QRect sel(xIn, trackY(), std::max(0, xOut - xIn), trackHeight());
    QColor selColor = pal.color(QPalette::Highlight);
    selColor.setAlpha(140);
    p.fillRect(sel, selColor);

    // Tick marks under the track.
    p.setPen(pal.color(QPalette::Mid));
    for (int i = 0; i <= 10; ++i) {
        const qint64 ms = m_durationMs * i / 10;
        const int x = xForMs(ms);
        p.drawLine(x, trackY() + trackHeight(), x, trackY() + trackHeight() + 4);
    }

    // ------------------------------------------------------------------
    // Trim handles
    // ------------------------------------------------------------------
    const QColor handleBase   = pal.color(QPalette::Highlight).darker(140);
    const QColor handleHover  = pal.color(QPalette::Highlight).lighter(115);
    const QColor handleActive = pal.color(QPalette::Highlight).lighter(135);
    const QColor handleEdge   = pal.color(QPalette::WindowText);

    auto colorFor = [&](DragTarget t) {
        if (m_drag  == t) return handleActive;
        if (m_drag  == DragTarget::None && m_hover == t) return handleHover;
        return handleBase;
    };

    // Decide draw order so the currently interacted handle ends up on top.
    DragTarget first  = DragTarget::InHandle;
    DragTarget second = DragTarget::OutHandle;
    if (m_drag == DragTarget::InHandle ||
        (m_drag == DragTarget::None && m_hover == DragTarget::InHandle)) {
        first  = DragTarget::OutHandle;
        second = DragTarget::InHandle;
    }

    auto drawHandle = [&](DragTarget which) {
        const QRect r   = (which == DragTarget::InHandle) ? inHandleRect() : outHandleRect();
        const QPainterPath path = (which == DragTarget::InHandle)
            ? makeInHandlePath(r, kHandleStem)
            : makeOutHandlePath(r, kHandleStem);
        p.setPen(QPen(handleEdge, 1));
        p.setBrush(colorFor(which));
        p.drawPath(path);

        // Three thin grip ridges centered on the tab body, hinting "drag me".
        QColor ridge = handleEdge;
        ridge.setAlpha(140);
        p.setPen(QPen(ridge, 1));
        const int cy = r.center().y();
        int gripX;
        if (which == DragTarget::InHandle) {
            gripX = r.left() + (r.width() - kHandleStem) / 2 - 1;
        } else {
            gripX = r.left() + kHandleStem + (r.width() - kHandleStem) / 2 - 1;
        }
        for (int dy = -4; dy <= 4; dy += 4) {
            p.drawLine(gripX, cy + dy, gripX + 2, cy + dy);
        }
    };

    drawHandle(first);
    drawHandle(second);

    // ------------------------------------------------------------------
    // Playhead
    // ------------------------------------------------------------------
    const int xPos = xForMs(m_positionMs);
    const QRect ph = playheadRect();

    QColor playheadColor = pal.color(QPalette::WindowText);
    if (m_drag == DragTarget::Playhead)            playheadColor = pal.color(QPalette::Highlight).lighter(110);
    else if (m_drag == DragTarget::None && m_hover == DragTarget::Playhead)
                                                   playheadColor = pal.color(QPalette::Highlight);

    p.setPen(QPen(playheadColor, kPlayheadWidth));
    p.drawLine(xPos, ph.top(), xPos, ph.bottom());

    // Triangular needles top & bottom give the playhead a fat, easy-to-click
    // grab target without thickening the line that runs through the selection.
    p.setPen(Qt::NoPen);
    p.setBrush(playheadColor);
    QPolygon topArrow;
    topArrow << QPoint(xPos - kPlayheadArrowW / 2, ph.top())
             << QPoint(xPos + kPlayheadArrowW / 2, ph.top())
             << QPoint(xPos,                       ph.top() + kPlayheadArrowH);
    p.drawPolygon(topArrow);

    QPolygon bottomArrow;
    bottomArrow << QPoint(xPos - kPlayheadArrowW / 2, ph.bottom())
                << QPoint(xPos + kPlayheadArrowW / 2, ph.bottom())
                << QPoint(xPos,                       ph.bottom() - kPlayheadArrowH);
    p.drawPolygon(bottomArrow);

    // ------------------------------------------------------------------
    // Time labels
    // ------------------------------------------------------------------
    p.setPen(pal.color(QPalette::WindowText));
    QFont f = p.font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    p.setFont(f);

    const QString currentText = TimeFormat::msToHms(m_positionMs);
    const QString inText      = tr("In  %1").arg(TimeFormat::msToHms(m_inMs));
    const QString outText     = tr("Out %1").arg(TimeFormat::msToHms(m_outMs));

    const QRect topArea(trackLeft(), 0, trackWidth(),
                        trackY() - kHandleVOverhang);
    if (topArea.height() > 0) {
        p.drawText(topArea, Qt::AlignLeft  | Qt::AlignVCenter, inText);
        p.drawText(topArea, Qt::AlignCenter,                   currentText);
        p.drawText(topArea, Qt::AlignRight | Qt::AlignVCenter, outText);
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
    } else {
        update();
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_drag == DragTarget::None) {
        const DragTarget t = hitTest(event->pos());
        switch (t) {
        case DragTarget::InHandle:
        case DragTarget::OutHandle: setCursor(Qt::SizeHorCursor); break;
        case DragTarget::Playhead:  setCursor(Qt::SplitHCursor);  break;
        default:                    setCursor(Qt::ArrowCursor);
        }
        if (t != m_hover) {
            m_hover = t;
            update();
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
    // Refresh hover state based on where the cursor is now resting.
    m_hover = hitTest(event->pos());
    update();
}

void TimelineWidget::leaveEvent(QEvent *)
{
    if (m_hover != DragTarget::None) {
        m_hover = DragTarget::None;
        update();
    }
}
