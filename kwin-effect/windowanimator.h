// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QHash>
#include <QRect>
#include <QRectF>
#include <QElapsedTimer>

namespace KWin {
class EffectWindow;
class WindowPaintData;
}

namespace PlasmaZones {

/**
 * @brief Animation data for autotile window geometry transitions
 *
 * Stores the start/end geometry and progress for smooth window animations
 * when autotiling moves windows to their calculated positions.
 */
struct WindowAnimation {
    QRectF startGeometry;   ///< Window geometry at animation start
    QRectF endGeometry;     ///< Target window geometry
    QElapsedTimer timer;    ///< Timer for animation progress calculation
    qreal duration = 150.0; ///< Animation duration in milliseconds (default 150ms)

    /// Check if the animation timer has been started
    bool isValid() const {
        return timer.isValid();
    }

    /// Calculate current progress (0.0 to 1.0) with OutQuad easing
    qreal progress() const {
        if (!timer.isValid()) {
            return 0.0;
        }
        qreal t = qMin(1.0, timer.elapsed() / duration);
        // OutQuad easing: fast start, smooth deceleration
        return 1.0 - (1.0 - t) * (1.0 - t);
    }

    /// Check if animation is complete
    bool isComplete() const {
        if (!timer.isValid()) {
            return true; // Invalid animation is considered complete
        }
        return timer.elapsed() >= duration;
    }

    /// Interpolate geometry based on current progress
    QRectF currentGeometry() const {
        qreal p = progress();
        return QRectF(
            startGeometry.x() + (endGeometry.x() - startGeometry.x()) * p,
            startGeometry.y() + (endGeometry.y() - startGeometry.y()) * p,
            startGeometry.width() + (endGeometry.width() - startGeometry.width()) * p,
            startGeometry.height() + (endGeometry.height() - startGeometry.height()) * p
        );
    }
};

/**
 * @brief Manages autotile window animations
 *
 * Responsible for:
 * - Tracking animation state for windows
 * - Computing interpolated geometry during animations
 * - Determining when animations are complete
 *
 * It does NOT apply geometry directly - the effect handles that.
 */
class WindowAnimator : public QObject
{
    Q_OBJECT

public:
    explicit WindowAnimator(QObject* parent = nullptr);

    // Configuration
    void setAnimationsEnabled(bool enabled) { m_enabled = enabled; }
    bool animationsEnabled() const { return m_enabled; }
    void setAnimationDuration(qreal duration) { m_duration = duration; }
    qreal animationDuration() const { return m_duration; }

    // Animation management
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

    // Check if window is already animating to the same target
    bool isAnimatingToTarget(KWin::EffectWindow* window, const QRect& targetGeometry) const;

    // Redirect animation to new target (for rapid geometry changes)
    void redirectAnimation(KWin::EffectWindow* window, const QRect& newTarget);

private:
    QHash<KWin::EffectWindow*, WindowAnimation> m_animations;
    bool m_enabled = true;
    qreal m_duration = 150.0;
};

} // namespace PlasmaZones
