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
 * @brief Easing curve supporting cubic bezier, elastic, and bounce types
 *
 * Bezier: P0=(0,0) and P3=(1,1) are implicit, defined by P1=(x1,y1) and P2=(x2,y2).
 * Stored in config as "x1,y1,x2,y2" (bezier) or "elastic-out:1.0,0.3" (named).
 * Detection: if string contains a letter -> named curve; otherwise -> bezier.
 */
struct EasingCurve
{
    enum class Type {
        CubicBezier,
        ElasticIn,
        ElasticOut,
        ElasticInOut,
        BounceIn,
        BounceOut,
        BounceInOut
    };

    Type type = Type::CubicBezier;

    // Bezier control points (used when type == CubicBezier)
    qreal x1 = 0.33;
    qreal y1 = 1.0;
    qreal x2 = 0.68;
    qreal y2 = 1.0;

    // Parameters for elastic and bounce curves
    qreal amplitude = 1.0;  // Elastic: overshoot intensity; Bounce: bounce height scale
    qreal period = 0.3;     // Elastic only: oscillation period
    int bounces = 3;        // Bounce only: number of bounces (1–8)

    /// Evaluate the easing curve at time x (0.0-1.0), returns eased value
    qreal evaluate(qreal x) const;

    /// Parse from config string; returns default OutCubic bezier on failure
    static EasingCurve fromString(const QString& str);

    /// Serialize to config string
    QString toString() const;

    bool operator==(const EasingCurve& other) const
    {
        if (type != other.type)
            return false;
        if (type == Type::CubicBezier) {
            return qFuzzyCompare(1.0 + x1, 1.0 + other.x1) && qFuzzyCompare(1.0 + y1, 1.0 + other.y1)
                && qFuzzyCompare(1.0 + x2, 1.0 + other.x2) && qFuzzyCompare(1.0 + y2, 1.0 + other.y2);
        }
        // Elastic types: compare amplitude and period
        if (type == Type::ElasticIn || type == Type::ElasticOut || type == Type::ElasticInOut) {
            return qFuzzyCompare(1.0 + amplitude, 1.0 + other.amplitude)
                && qFuzzyCompare(1.0 + period, 1.0 + other.period);
        }
        // Bounce types (BounceIn, BounceOut, BounceInOut): compare amplitude and bounce count
        return qFuzzyCompare(1.0 + amplitude, 1.0 + other.amplitude)
            && bounces == other.bounces;
    }
    bool operator!=(const EasingCurve& other) const
    {
        return !(*this == other);
    }

private:
    static qreal evaluateElasticOut(qreal t, qreal amp, qreal per);
    static qreal evaluateBounceOut(qreal t, qreal amp, int n);
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
    EasingCurve easing;      ///< Easing curve for this animation

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
    void setEasingCurve(const EasingCurve& curve)
    {
        m_easing = curve;
    }
    const EasingCurve& easingCurve() const
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
    EasingCurve m_easing;
    int m_minDistance = 0;
};

} // namespace PlasmaZones
