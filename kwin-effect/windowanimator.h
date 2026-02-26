// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QHash>
#include <QRect>
#include <QRectF>
#include <QPointF>
#include <QString>
#include <QElapsedTimer>
#include <QtMath>

namespace KWin {
class EffectWindow;
class WindowPaintData;
}

namespace PlasmaZones {

/**
 * @brief Cubic bezier easing curve defined by two control points
 *
 * P0=(0,0) and P3=(1,1) are implicit. The curve is defined by
 * control points P1=(x1,y1) and P2=(x2,y2).
 * Stored in config as "x1,y1,x2,y2" string.
 */
struct CubicBezierCurve
{
    qreal x1 = 0.33;
    qreal y1 = 1.0;
    qreal x2 = 0.68;
    qreal y2 = 1.0;

    /// Evaluate the easing curve at time x (0.0-1.0), returns eased value
    qreal evaluate(qreal x) const;

    /// Parse from "x1,y1,x2,y2" string format; returns default OutCubic on failure
    static CubicBezierCurve fromString(const QString& str);

    /// Serialize to "x1,y1,x2,y2" string format
    QString toString() const;

    bool operator==(const CubicBezierCurve& other) const
    {
        // Use 1.0+ offset to avoid qFuzzyCompare(0,0) returning false
        return qFuzzyCompare(1.0 + x1, 1.0 + other.x1) && qFuzzyCompare(1.0 + y1, 1.0 + other.y1)
            && qFuzzyCompare(1.0 + x2, 1.0 + other.x2) && qFuzzyCompare(1.0 + y2, 1.0 + other.y2);
    }
    bool operator!=(const CubicBezierCurve& other) const
    {
        return !(*this == other);
    }
};

/**
 * @brief Animation data for translate-only window transitions
 *
 * Stores the start position and target geometry for smooth slide animations.
 * The window's actual frame is moved to targetGeometry immediately via
 * moveResize(); this animation only provides a visual translation offset
 * that slides from the old position to the new. No scale transforms.
 */
struct WindowAnimation
{
    QPointF startPosition;   ///< Visual top-left position before snap
    QRect targetGeometry;    ///< Target geometry (for duplicate detection)
    QElapsedTimer timer;     ///< Timer for animation progress
    qreal duration = 150.0;  ///< Animation duration in milliseconds
    CubicBezierCurve easing; ///< Easing curve for this animation

    /// Check if the animation timer has been started
    bool isValid() const
    {
        return timer.isValid();
    }

    /// Calculate current progress (0.0 to 1.0) with the configured easing
    qreal progress() const
    {
        if (!timer.isValid() || duration <= 0.0) {
            return 0.0;
        }
        qreal t = qMin(1.0, timer.elapsed() / duration);
        return easing.evaluate(t);
    }

    /// Check if animation is complete
    bool isComplete() const
    {
        if (!timer.isValid()) {
            return true;
        }
        if (duration <= 0.0) {
            return true;
        }
        return timer.elapsed() >= duration;
    }

    /// Absolute visual top-left position at the current animation progress.
    /// At t=0 returns startPosition; at t=1 returns targetGeometry.topLeft().
    QPointF currentVisualPosition() const
    {
        const qreal p = progress();
        const qreal tx = targetGeometry.x();
        const qreal ty = targetGeometry.y();
        return QPointF(startPosition.x() + (tx - startPosition.x()) * p,
                       startPosition.y() + (ty - startPosition.y()) * p);
    }
};

/**
 * @brief Manages translate-only window slide animations
 *
 * When a window is snapped to a zone, the caller applies moveResize()
 * immediately to set the final geometry. This animator provides a visual
 * translation offset that slides the window from its old position to the
 * new one. No scale transforms are used, avoiding Wayland buffer desync
 * artifacts (flickering, zoom, size jumps).
 */
class WindowAnimator : public QObject
{
    Q_OBJECT

public:
    explicit WindowAnimator(QObject* parent = nullptr);

    // Configuration
    void setEnabled(bool enabled)
    {
        m_enabled = enabled;
    }
    bool isEnabled() const
    {
        return m_enabled;
    }
    void setDuration(qreal ms)
    {
        m_duration = ms;
    }
    qreal duration() const
    {
        return m_duration;
    }
    void setEasingCurve(const CubicBezierCurve& curve)
    {
        m_easing = curve;
    }
    const CubicBezierCurve& easingCurve() const
    {
        return m_easing;
    }
    void setMinDistance(int pixels)
    {
        m_minDistance = qMax(0, pixels);
    }
    int minDistance() const
    {
        return m_minDistance;
    }

    // Animation management
    bool hasActiveAnimations() const
    {
        return !m_animations.isEmpty();
    }
    bool hasAnimation(KWin::EffectWindow* window) const;
    bool startAnimation(KWin::EffectWindow* window, const QPointF& oldPosition, const QRect& targetGeometry);
    void removeAnimation(KWin::EffectWindow* window);
    void clear();

    // Animation state queries
    bool isAnimatingToTarget(KWin::EffectWindow* window, const QRect& targetGeometry) const;

    /// Current visual top-left position (lerped between start and target).
    /// Used to chain animations when redirecting to a new target mid-flight.
    QPointF currentVisualPosition(KWin::EffectWindow* window) const;

    // Per-frame: clean up completed animations. Called from prePaintScreen().
    void advanceAnimations();

    // Schedule targeted repaints for active animation regions. Called from postPaintScreen().
    void scheduleRepaints() const;

    // Paint helper — applies translate-only offset (no scale transforms).
    // The window visually slides from startPosition to its real frame position.
    void applyTransform(KWin::EffectWindow* window, KWin::WindowPaintData& data) const;

    // Bounding rect covering the full animation path (for repaint regions)
    QRectF animationBounds(KWin::EffectWindow* window) const;

private:
    QHash<KWin::EffectWindow*, WindowAnimation> m_animations;
    bool m_enabled = true;
    qreal m_duration = 150.0;
    CubicBezierCurve m_easing;
    int m_minDistance = 0;
};

} // namespace PlasmaZones
