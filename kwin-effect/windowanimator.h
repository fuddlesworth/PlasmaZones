// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../src/common/animationstyle.h"
#include "../src/common/springparams.h"

#include <easingcurve.h>

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <chrono>
#include <optional>
#include <variant>

namespace KWin {
class EffectWindow;
class WindowPaintData;
}

namespace PlasmaZones {

// EasingCurve and AnimationConfig live in src/compositor-common/easingcurve.h.
// This effect adds SpringAnimation (physics-based) plus style-aware window
// transitions on top of that shared baseline.

/**
 * @brief Damped harmonic oscillator animation (niri-inspired spring physics)
 *
 * Inherits the persistent config fields (dampingRatio, stiffness, epsilon) from
 * SpringParams and adds runtime-only fields (initialVelocity) plus physics
 * evaluation methods. Unlike EasingCurve, spring animations have no fixed
 * duration — they converge based on physics parameters.
 */
struct SpringAnimation : SpringParams
{
    qreal initialVelocity = 0.0; ///< Initial velocity (e.g. from gesture release)

    qreal evaluate(qreal t) const;
    bool isSettled(qreal t) const;
    qreal estimatedDuration() const;
    QString toString() const;
};

/**
 * @brief Resolved per-event animation parameters
 *
 * Produced by resolving a profile from AnimationProfileTree (see
 * src/core/animationprofile.h) for a specific event like "snapIn" or "snapOut".
 */
struct AnimationParams
{
    TimingMode timingMode = TimingMode::Easing;
    qreal duration = -1; ///< Milliseconds (-1 = use animator global default)
    QString easingCurveStr; ///< Bezier / named curve (empty = use global default)
    SpringAnimation spring; ///< Used when timingMode == TimingMode::Spring
    AnimationStyle style = AnimationStyle::Morph;
    qreal styleParam = 0.87; ///< Style-specific (e.g. minScale for Popin, slide fraction for SlideFade)
    bool enabled = true;
    QString shaderPath; ///< Path to custom GLSL fragment shader (reserved for Custom style)
    QString vertexShaderPath; ///< Optional vertex shader (reserved)
    int shaderSubdivisions = 1; ///< Grid subdivision for vertex deformation (reserved)

    /// Parse from a resolved AnimationProfile JSON object (schema from
    /// AnimationProfile::toJson() on the daemon side).
    static AnimationParams fromJson(const QJsonObject& obj);
};

/**
 * @brief Runtime state of a single active window animation
 *
 * Superset of compositor-common's WindowAnimation, extended with spring
 * physics, style, and shader metadata. Kept local to kwin-effect so
 * compositor-common stays free of per-effect extensions.
 */
struct ManagedAnimation
{
    QPointF startPosition;
    QSizeF startSize;
    QRect targetGeometry;
    std::chrono::milliseconds startTime{-1};
    qreal duration = 150.0; ///< ms (ignored for spring)
    std::variant<EasingCurve, SpringAnimation> timing;
    qreal cachedProgress = 0.0;
    qreal cachedSpringDuration = -1.0; ///< Precomputed estimatedDuration() for springs

    AnimationStyle style = AnimationStyle::Morph;
    qreal styleParam = 0.87;
    QString shaderPath;
    QString vertexShaderPath;
    int shaderSubdivisions = 1;

    bool isValid() const
    {
        return startTime.count() >= 0;
    }

    bool isSpring() const
    {
        return std::holds_alternative<SpringAnimation>(timing);
    }

    /// Style uses opacity → paint path must enable translucent rendering
    bool usesOpacity() const
    {
        return style == AnimationStyle::Slide || style == AnimationStyle::Popin || style == AnimationStyle::SlideFade
            || style == AnimationStyle::FadeIn || style == AnimationStyle::SlideUp || style == AnimationStyle::ScaleIn;
    }

    void updateProgress(std::chrono::milliseconds presentTime)
    {
        if (startTime.count() < 0) {
            startTime = presentTime;
            cachedProgress = 0.0;
            return;
        }
        const qreal elapsedMs = qreal((presentTime - startTime).count());

        if (isSpring()) {
            const auto& spring = std::get<SpringAnimation>(timing);
            cachedProgress = spring.evaluate(elapsedMs / 1000.0);
        } else {
            if (duration <= 0.0) {
                cachedProgress = 1.0;
                return;
            }
            const qreal t = qMin(1.0, elapsedMs / duration);
            cachedProgress = std::get<EasingCurve>(timing).evaluate(t);
        }
    }

    qreal progress() const
    {
        return cachedProgress;
    }

    bool isComplete(std::chrono::milliseconds presentTime) const
    {
        if (startTime.count() < 0) {
            return false;
        }
        const qreal elapsedMs = qreal((presentTime - startTime).count());
        if (isSpring()) {
            return std::get<SpringAnimation>(timing).isSettled(elapsedMs / 1000.0);
        }
        if (duration <= 0.0) {
            return true;
        }
        return elapsedMs >= duration;
    }

    QPointF currentVisualPosition() const
    {
        const qreal p = cachedProgress;
        const qreal tx = targetGeometry.x();
        const qreal ty = targetGeometry.y();
        return QPointF(startPosition.x() + (tx - startPosition.x()) * p,
                       startPosition.y() + (ty - startPosition.y()) * p);
    }

    QSizeF currentVisualSize() const
    {
        const qreal p = cachedProgress;
        const qreal tw = targetGeometry.width();
        const qreal th = targetGeometry.height();
        return QSizeF(startSize.width() + (tw - startSize.width()) * p,
                      startSize.height() + (th - startSize.height()) * p);
    }

    bool hasScaleChange() const
    {
        return qAbs(startSize.width() - targetGeometry.width()) > 1.0
            || qAbs(startSize.height() - targetGeometry.height()) > 1.0;
    }
};

/**
 * @brief Manages window snap animations (translate + scale + style)
 *
 * When a window is snapped to a zone, the caller applies moveResize()
 * immediately to set the final geometry. This animator provides visual
 * translation/scale/opacity transforms in paintWindow() that morph the
 * window from its old position/size to the new one.
 *
 * Timing is driven by presentTime (vsync-aligned) for frame-perfect
 * animation, and progress is cached once per frame to ensure consistent
 * position/size/opacity interpolation within a single paint cycle.
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

    /// Start animation with global duration/easing (Morph style).
    bool startAnimation(KWin::EffectWindow* window, const QPointF& oldPosition, const QSizeF& oldSize,
                        const QRect& targetGeometry);

    /// Start animation with per-event resolved parameters.
    bool startAnimation(KWin::EffectWindow* window, const QPointF& oldPosition, const QSizeF& oldSize,
                        const QRect& targetGeometry, const AnimationParams& params);

    /// Whether any active animation uses opacity (affects translucent-rendering decisions)
    bool hasOpacityAnimations() const
    {
        return m_opacityAnimationCount > 0;
    }

    /// Whether a specific window's animation uses opacity
    bool usesOpacity(KWin::EffectWindow* w) const
    {
        auto it = m_animations.constFind(w);
        return it != m_animations.constEnd() && it->usesOpacity();
    }

    void removeAnimation(KWin::EffectWindow* window);
    void clear();

    // Animation state queries
    bool isAnimatingToTarget(KWin::EffectWindow* window, const QRect& targetGeometry) const;

    /// Current visual top-left position (lerped between start and target).
    /// Used to chain animations when redirecting to a new target mid-flight.
    QPointF currentVisualPosition(KWin::EffectWindow* window) const;

    /// Current visual size (lerped between start and target).
    /// Used to chain animations when redirecting to a new target mid-flight.
    QSizeF currentVisualSize(KWin::EffectWindow* window) const;

    // Per-frame: update cached progress and clean up completed animations.
    // Called from prePaintScreen() with the compositor's presentTime.
    void advanceAnimations(std::chrono::milliseconds presentTime);

    // Schedule targeted repaints for active animation regions. Called from postPaintScreen().
    void scheduleRepaints() const;

    // Paint helper — applies translate, scale, and style-specific opacity.
    void applyTransform(KWin::EffectWindow* window, KWin::WindowPaintData& data) const;

    // Geometry-only variant — translate + scale but NO opacity.
    // Used when a custom GLSL fragment shader handles opacity / dissolve so
    // the C++ side provides only positioning; the shader outputs final alpha.
    void applyGeometryOnly(KWin::EffectWindow* window, KWin::WindowPaintData& data) const;

    // Bounding rect covering the full animation path (for repaint regions)
    QRectF animationBounds(KWin::EffectWindow* window) const;

    /// Snapshot of animation state for shader uniform binding
    struct AnimationInfo
    {
        qreal progress;
        qreal duration; ///< Effective duration in ms (computed from spring if applicable)
        qreal styleParam;
        QPointF startPosition;
        QSizeF startSize;
        QRect targetGeometry;
        QString shaderPath;
        QString vertexShaderPath;
        int shaderSubdivisions = 1;
    };

    /// Get animation info for shader uniforms (returns nullopt if no animation)
    std::optional<AnimationInfo> animationInfo(KWin::EffectWindow* window) const;

Q_SIGNALS:
    /// Emitted from advanceAnimations() when a window's animation completes
    /// and is removed from the active set. Listeners can clean up any per-window
    /// state (e.g. OffscreenEffect shader redirection).
    void animationFinished(KWin::EffectWindow* window);

private:
    // Shared translate + scale interpolation used by Morph, Slide, SlideFade
    void applyGeometryInterpolation(KWin::EffectWindow* window, const ManagedAnimation& anim,
                                    KWin::WindowPaintData& data, qreal slideFraction = 1.0) const;

    void applyMorphTransform(KWin::EffectWindow* window, const ManagedAnimation& anim,
                             KWin::WindowPaintData& data) const;
    void applySlideTransform(KWin::EffectWindow* window, const ManagedAnimation& anim,
                             KWin::WindowPaintData& data) const;
    void applyPopinTransform(KWin::EffectWindow* window, const ManagedAnimation& anim,
                             KWin::WindowPaintData& data) const;
    void applySlideFadeTransform(KWin::EffectWindow* window, const ManagedAnimation& anim,
                                 KWin::WindowPaintData& data) const;

    QHash<KWin::EffectWindow*, ManagedAnimation> m_animations;
    int m_opacityAnimationCount = 0;
    bool m_enabled = true;
    qreal m_duration = 150.0;
    EasingCurve m_easing;
    int m_minDistance = 0;
};

} // namespace PlasmaZones
