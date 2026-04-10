// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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

namespace KWin {
class EffectWindow;
class WindowPaintData;
}

namespace PlasmaZones {

// EasingCurve, SpringAnimation, WindowAnimation, and AnimationConfig live in
// src/compositor-common/easingcurve.h (shared across compositor plugins).
// This effect provides the runtime animator that drives them and the
// AnimationParams DTO for per-event profile resolution.

/**
 * @brief Resolved per-event animation parameters
 *
 * Produced by parsing a resolved AnimationProfile JSON returned from the
 * daemon (Settings.resolveAnimationProfile D-Bus method). Held locally in
 * kwin-effect because parsing depends on ConfigKeys accessors that live
 * in the daemon module.
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
    void applyGeometryInterpolation(KWin::EffectWindow* window, const WindowAnimation& anim,
                                    KWin::WindowPaintData& data, qreal slideFraction = 1.0) const;

    void applyMorphTransform(KWin::EffectWindow* window, const WindowAnimation& anim,
                             KWin::WindowPaintData& data) const;
    void applySlideTransform(KWin::EffectWindow* window, const WindowAnimation& anim,
                             KWin::WindowPaintData& data) const;
    void applyPopinTransform(KWin::EffectWindow* window, const WindowAnimation& anim,
                             KWin::WindowPaintData& data) const;
    void applySlideFadeTransform(KWin::EffectWindow* window, const WindowAnimation& anim,
                                 KWin::WindowPaintData& data) const;

    QHash<KWin::EffectWindow*, WindowAnimation> m_animations;
    int m_opacityAnimationCount = 0;
    bool m_enabled = true;
    qreal m_duration = 150.0;
    EasingCurve m_easing;
    int m_minDistance = 0;
};

} // namespace PlasmaZones
