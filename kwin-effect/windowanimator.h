// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QHash>
#include <QRect>
#include <QRectF>
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
 * @brief Animation data for window geometry transitions
 *
 * Stores the start/end geometry and progress for smooth window animations.
 */
struct WindowAnimation
{
    QRectF startGeometry; ///< Window geometry at animation start
    QRectF endGeometry; ///< Target window geometry
    QElapsedTimer timer; ///< Timer for animation progress calculation
    qreal duration = 150.0; ///< Animation duration in milliseconds (default 150ms)
    CubicBezierCurve easing; ///< Easing curve for this animation

    /// Check if the animation timer has been started
    bool isValid() const
    {
        return timer.isValid();
    }

    /// Calculate current progress (0.0 to 1.0) with the configured easing
    qreal progress() const
    {
        if (!timer.isValid()) {
            return 0.0;
        }
        qreal t = qMin(1.0, timer.elapsed() / duration);
        return easing.evaluate(t);
    }

    /// Check if animation is complete
    bool isComplete() const
    {
        if (!timer.isValid()) {
            return true; // Invalid animation is considered complete
        }
        return timer.elapsed() >= duration;
    }

    /// Interpolate geometry based on current progress
    QRectF currentGeometry() const
    {
        qreal p = progress();
        return QRectF(startGeometry.x() + (endGeometry.x() - startGeometry.x()) * p,
                      startGeometry.y() + (endGeometry.y() - startGeometry.y()) * p,
                      startGeometry.width() + (endGeometry.width() - startGeometry.width()) * p,
                      startGeometry.height() + (endGeometry.height() - startGeometry.height()) * p);
    }
};

/**
 * @brief Manages window geometry animations
 *
 * Tracks animation state, computes interpolated geometry, and determines
 * when animations are complete. The effect applies geometry immediately via
 * moveResize(), then visually interpolates from old to new using paint transforms.
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
    bool startAnimation(KWin::EffectWindow* window, const QRectF& startGeometry, const QRect& endGeometry);
    void removeAnimation(KWin::EffectWindow* window);
    void clear();

    // Animation state queries
    bool isAnimationComplete(KWin::EffectWindow* window) const;
    QRectF currentGeometry(KWin::EffectWindow* window) const;
    QRect finalGeometry(KWin::EffectWindow* window) const;

    // Paint helper - applies transform to paint data
    void applyTransform(KWin::EffectWindow* window, KWin::WindowPaintData& data);

    // Get the bounding rect covering the full animation path (start ∪ end geometry)
    // Used by the effect to request screen-region repaints that prevent ghost artifacts
    QRectF animationBounds(KWin::EffectWindow* window) const;

    // Check if window is already animating to the same target
    bool isAnimatingToTarget(KWin::EffectWindow* window, const QRect& targetGeometry) const;

    // Redirect animation to new target (for rapid geometry changes)
    void redirectAnimation(KWin::EffectWindow* window, const QRect& newTarget);

private:
    QHash<KWin::EffectWindow*, WindowAnimation> m_animations;
    bool m_enabled = true;
    qreal m_duration = 150.0;
    CubicBezierCurve m_easing;
    int m_minDistance = 0;
};

} // namespace PlasmaZones
