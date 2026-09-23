// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorProtocol/ScrollAxisEnum.h>

#include <QPoint>
#include <QRect>
#include <QSize>

namespace PhosphorScrollEngine {

/// Coordinate mapping for one strip orientation: a pure value type, trivially
/// copyable, no allocation, cheap enough to read once per column and once per
/// tile inside the relayout loop.
///
/// Every accessor is named for the ROLE (main / cross), never the physical
/// axis. MAIN is the strip — columns lie along it and the view scrolls along
/// it. CROSS is the within-column stack. For Horizontal, main = x and
/// cross = y, and every accessor below is the identity mapping, which is what
/// makes the transposition rewrite land behaviour-preserving.
///
/// A call site that still needs a physical x or y is by definition NOT
/// transposing and must say so in a comment. There are two legitimate cases:
/// the park position (which means "off every output", a claim independent of
/// strip direction) and the tab indicator's screen-edge vocabulary (a user who
/// asked for an indicator on the left means the left of their screen).
///
/// THIS TYPE MAPS COORDINATES; IT NEVER INTERPRETS USER INTENT. The
/// direction-token to role remap belongs with the navigation verbs, not here —
/// geometry transposition and input-intent transposition are different
/// problems and must not share an object.
class StripAxis
{
public:
    constexpr StripAxis() = default;
    constexpr explicit StripAxis(PhosphorProtocol::ScrollAxis axis)
        : m_axis(axis)
    {
    }

    static constexpr StripAxis horizontal()
    {
        return StripAxis(PhosphorProtocol::ScrollAxis::Horizontal);
    }
    static constexpr StripAxis vertical()
    {
        return StripAxis(PhosphorProtocol::ScrollAxis::Vertical);
    }

    constexpr PhosphorProtocol::ScrollAxis axis() const
    {
        return m_axis;
    }
    constexpr bool isHorizontal() const
    {
        return m_axis == PhosphorProtocol::ScrollAxis::Horizontal;
    }
    /// The negative of isHorizontal, spelled out. Callers that want the
    /// vertical case were reaching through axis() to compare the wire enum,
    /// which puts the protocol type in the middle of a question this object
    /// already answers.
    constexpr bool isVertical() const
    {
        return m_axis == PhosphorProtocol::ScrollAxis::Vertical;
    }
    /// Main and cross exchanged. NO engine call site uses this today — it
    /// completes the role algebra for embedders of this library (a consumer
    /// reasoning about the cross axis in main-axis vocabulary should not
    /// hand-roll the flip), and the unit tests pin the involution.
    constexpr StripAxis transposed() const
    {
        return StripAxis(isHorizontal() ? PhosphorProtocol::ScrollAxis::Vertical
                                        : PhosphorProtocol::ScrollAxis::Horizontal);
    }

    // -- Read -----------------------------------------------------------

    constexpr int mainPos(const QRect& r) const
    {
        return isHorizontal() ? r.x() : r.y();
    }
    constexpr int crossPos(const QRect& r) const
    {
        return isHorizontal() ? r.y() : r.x();
    }
    constexpr int mainSize(const QRect& r) const
    {
        return isHorizontal() ? r.width() : r.height();
    }
    constexpr int crossSize(const QRect& r) const
    {
        return isHorizontal() ? r.height() : r.width();
    }
    constexpr int mainSize(const QSize& s) const
    {
        return isHorizontal() ? s.width() : s.height();
    }
    constexpr int crossSize(const QSize& s) const
    {
        return isHorizontal() ? s.height() : s.width();
    }
    constexpr int mainPos(const QPoint& p) const
    {
        return isHorizontal() ? p.x() : p.y();
    }
    constexpr int crossPos(const QPoint& p) const
    {
        return isHorizontal() ? p.y() : p.x();
    }

    /// Inclusive low / high edges, matching QRect's own semantics where
    /// right() and bottom() are the LAST CONTAINED pixel rather than one past.
    constexpr int mainLow(const QRect& r) const
    {
        return isHorizontal() ? r.left() : r.top();
    }
    constexpr int mainHigh(const QRect& r) const
    {
        return isHorizontal() ? r.right() : r.bottom();
    }
    constexpr int crossLow(const QRect& r) const
    {
        return isHorizontal() ? r.top() : r.left();
    }
    constexpr int crossHigh(const QRect& r) const
    {
        return isHorizontal() ? r.bottom() : r.right();
    }

    // -- Write ----------------------------------------------------------

    /// Build a rect from role coordinates. The ONE constructor every resolved
    /// rect should go through.
    constexpr QRect makeRect(int mainPosition, int crossPosition, int mainExtent, int crossExtent) const
    {
        return isHorizontal() ? QRect(mainPosition, crossPosition, mainExtent, crossExtent)
                              : QRect(crossPosition, mainPosition, crossExtent, mainExtent);
    }

    /// setLeft/setTop equivalent: MOVES the low edge and HOLDS the high edge,
    /// so it changes both position and extent. setMainHigh is the mirror. That
    /// asymmetry is QRect's own, and it is load-bearing for the straddle
    /// clamp's pinned-position test — do not "fix" either into a move.
    constexpr void setMainLow(QRect& r, int v) const
    {
        if (isHorizontal()) {
            r.setLeft(v);
        } else {
            r.setTop(v);
        }
    }
    constexpr void setMainHigh(QRect& r, int v) const
    {
        if (isHorizontal()) {
            r.setRight(v);
        } else {
            r.setBottom(v);
        }
    }
    constexpr void setCrossHigh(QRect& r, int v) const
    {
        if (isHorizontal()) {
            r.setBottom(v);
        } else {
            r.setRight(v);
        }
    }

    /// Reposition without resizing, unlike the setters above.
    constexpr void moveMain(QRect& r, int v) const
    {
        if (isHorizontal()) {
            r.moveLeft(v);
        } else {
            r.moveTop(v);
        }
    }

    /// Translate along the MAIN axis only — the view slide. The cross
    /// coordinate is left exactly as it was.
    constexpr QRect translatedMain(const QRect& r, int delta) const
    {
        return isHorizontal() ? r.translated(delta, 0) : r.translated(0, delta);
    }

    constexpr bool operator==(const StripAxis& other) const
    {
        return m_axis == other.m_axis;
    }
    constexpr bool operator!=(const StripAxis& other) const
    {
        return m_axis != other.m_axis;
    }

private:
    PhosphorProtocol::ScrollAxis m_axis = PhosphorProtocol::ScrollAxis::Horizontal;
};

} // namespace PhosphorScrollEngine
