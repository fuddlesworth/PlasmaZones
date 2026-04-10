// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowanimator.h"

#include "../src/config/configkeys.h"

#include <animation_math.h>

#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>
#include <QJsonObject>
#include <QLineF>
#include <QLoggingCategory>
#include <QMarginsF>
#include <QVarLengthArray>
#include <QtMath>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

// SpringAnimation physics live in src/compositor-common/easingcurve.cpp.

// ═══════════════════════════════════════════════════════════════════════════════
// AnimationParams
// ═══════════════════════════════════════════════════════════════════════════════

AnimationParams AnimationParams::fromJson(const QJsonObject& obj)
{
    AnimationParams p;
    if (obj.isEmpty())
        return p;

    p.timingMode = static_cast<TimingMode>(obj.value(ConfigKeys::animProfileTimingModeKey()).toInt(0));
    p.duration = obj.value(ConfigKeys::animProfileDurationKey()).toInt(-1);
    p.easingCurveStr = obj.value(ConfigKeys::animProfileEasingCurveKey()).toString();

    if (obj.contains(ConfigKeys::animProfileSpringKey())) {
        QJsonObject sp = obj.value(ConfigKeys::animProfileSpringKey()).toObject();
        p.spring.dampingRatio = qBound(0.1, sp.value(ConfigKeys::animProfileSpringDampingKey()).toDouble(1.0), 10.0);
        p.spring.stiffness = qBound(1.0, sp.value(ConfigKeys::animProfileSpringStiffnessKey()).toDouble(800.0), 2000.0);
        p.spring.epsilon = qBound(0.00001, sp.value(ConfigKeys::animProfileSpringEpsilonKey()).toDouble(0.0001), 0.1);
        p.spring.initialVelocity = qBound(-10.0, sp.value(QLatin1String("initialVelocity")).toDouble(0.0), 10.0);
    }

    p.style = animationStyleFromString(obj.value(ConfigKeys::animProfileStyleKey()).toString());
    p.styleParam = obj.value(ConfigKeys::animProfileStyleParamKey()).toDouble(0.87);

    if (obj.contains(ConfigKeys::animProfileEnabledKey()))
        p.enabled = obj.value(ConfigKeys::animProfileEnabledKey()).toBool(true);

    p.shaderPath = obj.value(ConfigKeys::animProfileShaderPathKey()).toString();

    return p;
}

// ═══════════════════════════════════════════════════════════════════════════════
// WindowAnimator
// ═══════════════════════════════════════════════════════════════════════════════

WindowAnimator::WindowAnimator(QObject* parent)
    : QObject(parent)
{
}

bool WindowAnimator::hasAnimation(KWin::EffectWindow* window) const
{
    return m_animations.contains(window);
}

bool WindowAnimator::startAnimation(KWin::EffectWindow* window, const QPointF& oldPosition, const QSizeF& oldSize,
                                    const QRect& targetGeometry)
{
    AnimationParams params;
    params.timingMode = TimingMode::Easing;
    params.duration = -1; // use global
    params.style = AnimationStyle::Morph;
    return startAnimation(window, oldPosition, oldSize, targetGeometry, params);
}

bool WindowAnimator::startAnimation(KWin::EffectWindow* window, const QPointF& oldPosition, const QSizeF& oldSize,
                                    const QRect& targetGeometry, const AnimationParams& params)
{
    if (!window || !m_enabled || !params.enabled)
        return false;

    if (params.style == AnimationStyle::None)
        return false;

    // Skip degenerate targets
    if (targetGeometry.width() <= 0 || targetGeometry.height() <= 0)
        return false;

    // Skip if position change is below the minimum distance threshold
    // and size isn't changing either
    const QPointF newPos = targetGeometry.topLeft();
    const qreal dist = QLineF(oldPosition, newPos).length();
    const bool sizeChanging =
        qAbs(oldSize.width() - targetGeometry.width()) > 1.0 || qAbs(oldSize.height() - targetGeometry.height()) > 1.0;
    if (dist < qMax(1.0, qreal(m_minDistance)) && !sizeChanging)
        return false;

    // If replacing an existing animation, decrement the old opacity counter first
    // so the net accounting stays correct even if styles differ.
    auto existingIt = m_animations.find(window);
    if (existingIt != m_animations.end() && existingIt->usesOpacity()) {
        --m_opacityAnimationCount;
    }

    WindowAnimation anim;
    anim.startPosition = oldPosition;
    anim.startSize = oldSize;
    anim.targetGeometry = targetGeometry;
    anim.style = params.style;
    anim.styleParam = params.styleParam;
    anim.shaderPath = params.shaderPath;
    anim.vertexShaderPath = params.vertexShaderPath;
    anim.shaderSubdivisions = params.shaderSubdivisions;

    if (params.timingMode == TimingMode::Spring) {
        anim.timing = params.spring;
        anim.duration = 0; // unused for spring
    } else {
        anim.duration = (params.duration > 0) ? params.duration : m_duration;
        anim.timing = params.easingCurveStr.isEmpty() ? m_easing : EasingCurve::fromString(params.easingCurveStr);
    }

    // Pre-compute estimated spring duration to avoid per-frame forward scan
    if (auto* spring = std::get_if<SpringAnimation>(&anim.timing)) {
        anim.cachedSpringDuration = spring->estimatedDuration();
    }

    if (anim.usesOpacity())
        ++m_opacityAnimationCount;

    m_animations[window] = anim;
    window->addRepaintFull();

    qCDebug(lcEffect) << "Started animation from" << oldPosition << oldSize << "to" << newPos << targetGeometry.size()
                      << "style:" << static_cast<int>(params.style) << "timing:" << static_cast<int>(params.timingMode);
    return true;
}

void WindowAnimator::removeAnimation(KWin::EffectWindow* window)
{
    auto it = m_animations.find(window);
    if (it != m_animations.end()) {
        if (it->usesOpacity())
            --m_opacityAnimationCount;
        m_animations.erase(it);
        m_opacityAnimationCount = qMax(0, m_opacityAnimationCount);
    }
}

void WindowAnimator::clear()
{
    m_animations.clear();
    m_opacityAnimationCount = 0;
}

bool WindowAnimator::isAnimatingToTarget(KWin::EffectWindow* window, const QRect& targetGeometry) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd()) {
        return false;
    }
    return it->targetGeometry == targetGeometry;
}

QPointF WindowAnimator::currentVisualPosition(KWin::EffectWindow* window) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd()) {
        return window ? window->frameGeometry().topLeft() : QPointF();
    }
    return it->currentVisualPosition();
}

QSizeF WindowAnimator::currentVisualSize(KWin::EffectWindow* window) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd()) {
        return window ? window->frameGeometry().size() : QSizeF();
    }
    return it->currentVisualSize();
}

void WindowAnimator::advanceAnimations(std::chrono::milliseconds presentTime)
{
    // Collect removals on the stack instead of heap-allocating QHash::keys() per frame
    QVarLengthArray<KWin::EffectWindow*, 16> toRemove;

    for (auto it = m_animations.begin(); it != m_animations.end(); ++it) {
        KWin::EffectWindow* window = it.key();

        if (window->isDeleted()) {
            toRemove.append(window);
            continue;
        }

        it->updateProgress(presentTime);

        if (it->isComplete(presentTime)) {
            const QRectF bounds = animationBounds(window);
            toRemove.append(window);
            window->addRepaintFull();
            if (bounds.isValid()) {
                KWin::effects->addRepaint(bounds.toAlignedRect());
            }
            qCDebug(lcEffect) << "Window snap animation complete";
        }
    }

    for (KWin::EffectWindow* w : toRemove) {
        auto it = m_animations.find(w);
        if (it != m_animations.end()) {
            if (it->usesOpacity())
                --m_opacityAnimationCount;
            m_animations.erase(it);
        }
        // Notify listeners (e.g. shader redirection cleanup) after the entry
        // is removed so a slot that calls hasAnimation() sees the current state.
        Q_EMIT animationFinished(w);
    }
    m_opacityAnimationCount = qMax(0, m_opacityAnimationCount);
}

void WindowAnimator::scheduleRepaints() const
{
    for (auto it = m_animations.constBegin(); it != m_animations.constEnd(); ++it) {
        const QRectF bounds = animationBounds(it.key());
        if (bounds.isValid()) {
            KWin::effects->addRepaint(bounds.toAlignedRect());
        }
    }
}

void WindowAnimator::applyTransform(KWin::EffectWindow* window, KWin::WindowPaintData& data) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd() || !it->isValid())
        return;

    switch (it->style) {
    case AnimationStyle::None:
        return;
    case AnimationStyle::Slide:
        applySlideTransform(window, *it, data);
        break;
    case AnimationStyle::Popin:
        applyPopinTransform(window, *it, data);
        break;
    case AnimationStyle::SlideFade:
        applySlideFadeTransform(window, *it, data);
        break;
    case AnimationStyle::FadeIn:
    case AnimationStyle::SlideUp:
    case AnimationStyle::ScaleIn:
        // Overlay-only styles shouldn't reach window animations; fall back to morph
        qCDebug(lcEffect) << "Overlay style" << static_cast<int>(it->style) << "in window animation, using morph";
        applyMorphTransform(window, *it, data);
        break;
    case AnimationStyle::Morph:
    case AnimationStyle::Custom:
    default:
        applyMorphTransform(window, *it, data);
        break;
    }
}

void WindowAnimator::applyGeometryOnly(KWin::EffectWindow* window, KWin::WindowPaintData& data) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd() || !it->isValid())
        return;

    // Popin style: center-scale geometry without opacity.
    if (it->style == AnimationStyle::Popin) {
        const qreal p = it->cachedProgress;
        const qreal minScale = qBound(0.1, it->styleParam, 1.0);
        const qreal scale = qMax(0.01, minScale + (1.0 - minScale) * p);
        const QRectF frameGeo = window->frameGeometry();
        const qreal cx = frameGeo.width() * 0.5;
        const qreal cy = frameGeo.height() * 0.5;
        data.setXScale(data.xScale() * scale);
        data.setYScale(data.yScale() * scale);
        data += QPointF(cx * (1.0 - scale), cy * (1.0 - scale));
        return;
    }

    // SlideFade: partial translate/scale via slideFraction = styleParam.
    if (it->style == AnimationStyle::SlideFade) {
        const qreal slideFraction = qBound(0.0, it->styleParam, 1.0);
        applyGeometryInterpolation(window, *it, data, slideFraction);
        return;
    }

    // Morph / Slide / Custom / default: full geometry interpolation.
    applyGeometryInterpolation(window, *it, data);
}

std::optional<WindowAnimator::AnimationInfo> WindowAnimator::animationInfo(KWin::EffectWindow* window) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd() || !it->isValid())
        return std::nullopt;

    qreal effectiveDuration = it->duration;
    if (it->isSpring() && it->cachedSpringDuration > 0) {
        effectiveDuration = it->cachedSpringDuration * 1000.0;
    }
    return AnimationInfo{it->cachedProgress, effectiveDuration,    it->styleParam,
                         it->startPosition,  it->startSize,        it->targetGeometry,
                         it->shaderPath,     it->vertexShaderPath, it->shaderSubdivisions};
}

void WindowAnimator::applyGeometryInterpolation(KWin::EffectWindow* window, const WindowAnimation& anim,
                                                KWin::WindowPaintData& data, qreal slideFraction) const
{
    // Translate: desired visual position minus actual (frameGeometry is already at target).
    const QPointF desiredPos = anim.currentVisualPosition();
    const QPointF actualPos = window->frameGeometry().topLeft();
    const QPointF fullOffset = desiredPos - actualPos;
    data += (fullOffset * slideFraction);

    // Scale: smoothly morph from old size to target size.
    if (anim.hasScaleChange()) {
        const QSizeF desiredSize = anim.currentVisualSize();
        const QSizeF actualSize = window->frameGeometry().size();
        constexpr qreal MinDim = 1.0;
        const qreal targetSx = desiredSize.width() / qMax(actualSize.width(), MinDim);
        const qreal targetSy = desiredSize.height() / qMax(actualSize.height(), MinDim);
        const qreal sx = qBound(0.1, 1.0 + (targetSx - 1.0) * slideFraction, 10.0);
        const qreal sy = qBound(0.1, 1.0 + (targetSy - 1.0) * slideFraction, 10.0);
        data.setXScale(data.xScale() * sx);
        data.setYScale(data.yScale() * sy);
    }
}

void WindowAnimator::applyMorphTransform(KWin::EffectWindow* window, const WindowAnimation& anim,
                                         KWin::WindowPaintData& data) const
{
    applyGeometryInterpolation(window, anim, data);
}

void WindowAnimator::applySlideTransform(KWin::EffectWindow* window, const WindowAnimation& anim,
                                         KWin::WindowPaintData& data) const
{
    applyGeometryInterpolation(window, anim, data);
    // Opacity: fade in over first 30% of animation progress
    data.multiplyOpacity(qBound(0.0, anim.cachedProgress / 0.3, 1.0));
}

void WindowAnimator::applyPopinTransform(KWin::EffectWindow* window, const WindowAnimation& anim,
                                         KWin::WindowPaintData& data) const
{
    const qreal p = anim.cachedProgress;
    const qreal minScale = qBound(0.1, anim.styleParam, 1.0);
    const qreal scale = qMax(0.01, minScale + (1.0 - minScale) * p);

    const QRectF frameGeo = window->frameGeometry();

    // Scale from center: offset keeps the visual center stationary
    const qreal cx = frameGeo.width() * 0.5;
    const qreal cy = frameGeo.height() * 0.5;
    const qreal offsetX = cx * (1.0 - scale);
    const qreal offsetY = cy * (1.0 - scale);

    data.setXScale(data.xScale() * scale);
    data.setYScale(data.yScale() * scale);
    data += QPointF(offsetX, offsetY);

    data.multiplyOpacity(qBound(0.0, p, 1.0));
}

void WindowAnimator::applySlideFadeTransform(KWin::EffectWindow* window, const WindowAnimation& anim,
                                             KWin::WindowPaintData& data) const
{
    const qreal slideFraction = qBound(0.0, anim.styleParam, 1.0);
    applyGeometryInterpolation(window, anim, data, slideFraction);
    data.multiplyOpacity(qBound(0.0, anim.cachedProgress, 1.0));
}

QRectF WindowAnimator::animationBounds(KWin::EffectWindow* window) const
{
    auto it = m_animations.constFind(window);
    if (it == m_animations.constEnd()) {
        return QRectF();
    }

    // The window's expanded geometry (includes shadow/decoration padding).
    // Guard: once a window enters the "deleted" state, its Item tree may be
    // torn down and expandedGeometry() would dereference a null Item pointer.
    const QRectF expanded = (window && !window->isDeleted()) ? window->expandedGeometry() : QRectF(it->targetGeometry);

    // Shadow/decoration padding (constant)
    const QRectF frameGeo(it->targetGeometry);
    const qreal padLeft = expanded.x() - frameGeo.x();
    const qreal padTop = expanded.y() - frameGeo.y();
    const qreal padRight = expanded.right() - frameGeo.right();
    const qreal padBottom = expanded.bottom() - frameGeo.bottom();

    // Footprint at animation start (t=0)
    const QRectF atStart(it->startPosition.x() + padLeft, it->startPosition.y() + padTop,
                         it->startSize.width() - padLeft + padRight, it->startSize.height() - padTop + padBottom);

    QRectF bounds = expanded.united(atStart);

    // For overshooting curves (elastic, bounce, underdamped springs), sample to find extremes.
    bool needsSampling = it->isSpring() && std::get<SpringAnimation>(it->timing).dampingRatio < 1.0;

    if (!needsSampling && std::holds_alternative<EasingCurve>(it->timing)) {
        const auto& easing = std::get<EasingCurve>(it->timing);
        const bool isBounce = (easing.type == EasingCurve::Type::BounceIn || easing.type == EasingCurve::Type::BounceOut
                               || easing.type == EasingCurve::Type::BounceInOut);
        needsSampling = (easing.type == EasingCurve::Type::ElasticIn || easing.type == EasingCurve::Type::ElasticOut
                         || easing.type == EasingCurve::Type::ElasticInOut)
            || (isBounce && easing.amplitude > 1.0)
            || (easing.type == EasingCurve::Type::CubicBezier
                && (easing.y1 < 0.0 || easing.y1 > 1.0 || easing.y2 < 0.0 || easing.y2 > 1.0));
    }

    if (needsSampling) {
        const qreal dx = it->startPosition.x() - it->targetGeometry.x();
        const qreal dy = it->startPosition.y() - it->targetGeometry.y();
        const qreal dw = it->startSize.width() - it->targetGeometry.width();
        const qreal dh = it->startSize.height() - it->targetGeometry.height();

        // Use cached spring duration to avoid O(2000) forward scan per frame
        const qreal springTotalDuration = it->isSpring() ? it->cachedSpringDuration : 0.0;

        constexpr int nSamples = 50;
        for (int i = 1; i < nSamples; ++i) {
            qreal p;
            if (it->isSpring()) {
                const auto& spring = std::get<SpringAnimation>(it->timing);
                p = spring.evaluate(springTotalDuration * qreal(i) / nSamples);
            } else {
                p = std::get<EasingCurve>(it->timing).evaluate(qreal(i) / nSamples);
            }
            const qreal inv = 1.0 - p;
            QRectF sampledRect(it->targetGeometry.x() + dx * inv + padLeft, it->targetGeometry.y() + dy * inv + padTop,
                               it->targetGeometry.width() + dw * inv - padLeft + padRight,
                               it->targetGeometry.height() + dh * inv - padTop + padBottom);
            bounds = bounds.united(sampledRect);
        }
        bounds.adjust(-2.0, -2.0, 2.0, 2.0);
        return bounds;
    }

    // Fast path: non-overshooting curves — use shared helper for straight interpolation.
    const QMarginsF padding(padLeft, padTop, padRight, padBottom);
    return PlasmaZones::AnimationMath::computeOvershootBounds(it->startPosition, it->startSize, it->targetGeometry,
                                                              std::get<EasingCurve>(it->timing), padding);
}

} // namespace PlasmaZones
