// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowanimator.h"

#include <PhosphorAnimation/Curve.h>

#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>

#include <QLoggingCategory>

#include <utility>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

// ═══════════════════════════════════════════════════════════════════════
// Configuration
// ═══════════════════════════════════════════════════════════════════════

void WindowAnimator::setOutputClockResolver(OutputClockResolver resolver)
{
    m_outputClockResolver = std::move(resolver);
}

void WindowAnimator::setOnAnimationCompleteCallback(AnimationCompleteCallback callback)
{
    m_onAnimationCompleteCallback = std::move(callback);
}

void WindowAnimator::setClock(PhosphorAnimation::IMotionClock* clock)
{
    m_clock = clock;
}

PhosphorAnimation::IMotionClock* WindowAnimator::clock() const
{
    return m_clock;
}

void WindowAnimator::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

bool WindowAnimator::isEnabled() const
{
    return m_enabled;
}

void WindowAnimator::setProfile(const PhosphorAnimation::Profile& profile)
{
    m_profile = profile;
    clampProfile(m_profile);
}

const PhosphorAnimation::Profile& WindowAnimator::profile() const
{
    return m_profile;
}

void WindowAnimator::setCurve(std::shared_ptr<const PhosphorAnimation::Curve> curve)
{
    m_profile.curve = std::move(curve);
}

std::shared_ptr<const PhosphorAnimation::Curve> WindowAnimator::curve() const
{
    return m_profile.curve;
}

void WindowAnimator::setDuration(qreal ms)
{
    m_profile.duration = ms;
    clampProfile(m_profile);
}

qreal WindowAnimator::duration() const
{
    return m_profile.effectiveDuration();
}

void WindowAnimator::setMinDistance(int pixels)
{
    m_profile.minDistance = pixels;
    clampProfile(m_profile);
}

int WindowAnimator::minDistance() const
{
    return m_profile.effectiveMinDistance();
}

void WindowAnimator::setRetargetPolicy(PhosphorAnimation::RetargetPolicy policy)
{
    m_retargetPolicy = policy;
}

PhosphorAnimation::RetargetPolicy WindowAnimator::retargetPolicy() const
{
    return m_retargetPolicy;
}

// ═══════════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════════

bool WindowAnimator::hasActiveAnimations() const
{
    return !m_animations.empty();
}

bool WindowAnimator::hasAnimation(KWin::EffectWindow* handle) const
{
    return m_animations.contains(handle);
}

bool WindowAnimator::startAnimation(KWin::EffectWindow* handle, const QRectF& oldFrame, const QRectF& newFrame,
                                    const PhosphorAnimation::Profile* profileOverride)
{
    const auto r = startAnimationWithResult(handle, oldFrame, newFrame, profileOverride);
    return r == PhosphorAnimation::StartResult::Accepted || r == PhosphorAnimation::StartResult::AcceptedThenRemoved;
}

PhosphorAnimation::StartResult
WindowAnimator::startAnimationWithResult(KWin::EffectWindow* handle, const QRectF& oldFrame, const QRectF& newFrame,
                                         const PhosphorAnimation::Profile* profileOverride)
{
    using PhosphorAnimation::StartResult;

    if (!m_enabled) {
        return StartResult::Disabled;
    }
    if (!isHandleValid(handle)) {
        return StartResult::HandleInvalid;
    }

    PhosphorAnimation::IMotionClock* clk = clockForHandle(handle);
    if (!clk) {
        return StartResult::NoClock;
    }

    PhosphorAnimation::SnapPolicy::SnapParams params;
    // Per-call profile override — used by the per-window animation rule
    // cascade to apply a curve / duration override for one animation without
    // mutating m_profile. The override is already curve-resolved through
    // CurveRegistry by the caller and clamped here through the same path as
    // m_profile.
    if (profileOverride) {
        params.profile = *profileOverride;
        clampProfile(params.profile);
    } else {
        params.profile = m_profile;
    }
    params.retargetPolicy = m_retargetPolicy;
    const auto spec = PhosphorAnimation::SnapPolicy::createSnapSpec(oldFrame, newFrame, params, clk);
    if (!spec) {
        return StartResult::PolicyRejected;
    }

    PhosphorAnimation::AnimatedValue<QRectF> anim;
    if (!anim.start(oldFrame, newFrame, *spec)) {
        if (auto existing = m_animations.find(handle); existing != m_animations.end()) {
            PhosphorAnimation::AnimatedValue<QRectF> displaced = std::move(existing->second);
            m_animations.erase(existing);
            onAnimationReplaced(handle, displaced);
        }
        return StartResult::NoMotion;
    }

    // Displace + notify via move-assignment to preserve object identity
    // (re-entrancy safety — see original AnimationController commentary).
    if (auto existing = m_animations.find(handle); existing != m_animations.end()) {
        PhosphorAnimation::AnimatedValue<QRectF> displaced = std::move(existing->second);
        existing->second = std::move(anim);
        onAnimationReplaced(handle, displaced);
        // Re-lookup: hook may have mutated the map.
        auto placed = m_animations.find(handle);
        if (placed != m_animations.end()) {
            onAnimationStarted(handle, placed->second);
            return StartResult::Accepted;
        }
        return StartResult::AcceptedThenRemoved;
    }

    auto [it, inserted] = m_animations.emplace(handle, std::move(anim));
    onAnimationStarted(handle, it->second);
    return StartResult::Accepted;
}

bool WindowAnimator::retarget(KWin::EffectWindow* handle, const QRectF& newFrame,
                              PhosphorAnimation::RetargetPolicy policy)
{
    return retargetWithResult(handle, newFrame, policy) == PhosphorAnimation::RetargetResult::Accepted;
}

bool WindowAnimator::retarget(KWin::EffectWindow* handle, const QRectF& newFrame)
{
    return retargetWithResult(handle, newFrame) == PhosphorAnimation::RetargetResult::Accepted;
}

PhosphorAnimation::RetargetResult WindowAnimator::retargetWithResult(KWin::EffectWindow* handle, const QRectF& newFrame,
                                                                     PhosphorAnimation::RetargetPolicy policy)
{
    using PhosphorAnimation::RetargetResult;

    if (!m_enabled) {
        return RetargetResult::Disabled;
    }
    if (!isHandleValid(handle)) {
        return RetargetResult::HandleInvalid;
    }
    auto it = m_animations.find(handle);
    if (it == m_animations.end()) {
        return RetargetResult::UnknownHandle;
    }

    // Snapshot displaced bounds before retarget overwrites state.
    const QMarginsF preservedPadding = expandedPadding(handle, it->second);
    const QRectF preservedBounds = it->second.bounds().marginsAdded(preservedPadding);
    const bool accepted = it->second.retarget(newFrame, policy);
    if (accepted) {
        onAnimationRetargeted(handle, it->second);
        return RetargetResult::Accepted;
    }

    // Degenerate retarget — reap immediately.
    if (it->second.isComplete()) {
        const QMarginsF padding = expandedPadding(handle, it->second);
        const QRectF postBounds = it->second.bounds().marginsAdded(padding);
        const QRectF bounds = preservedBounds.isValid() ? preservedBounds.united(postBounds) : postBounds;
        PhosphorAnimation::AnimatedValue<QRectF> finished = std::move(it->second);
        m_animations.erase(it);
        onAnimationComplete(handle, finished);
        if (bounds.isValid()) {
            onRepaintNeeded(handle, bounds);
        }
        return RetargetResult::DegenerateReap;
    }

    Q_ASSERT_X(false, "WindowAnimator::retargetWithResult",
               "AnimatedValue rejected retarget without marking complete — spec was never installed");
    return RetargetResult::InternalError;
}

PhosphorAnimation::RetargetResult WindowAnimator::retargetWithResult(KWin::EffectWindow* handle, const QRectF& newFrame)
{
    using PhosphorAnimation::RetargetResult;

    if (!m_enabled) {
        return RetargetResult::Disabled;
    }
    if (!isHandleValid(handle)) {
        return RetargetResult::HandleInvalid;
    }
    auto it = m_animations.find(handle);
    if (it == m_animations.end()) {
        return RetargetResult::UnknownHandle;
    }
    return retargetWithResult(handle, newFrame, it->second.spec().retargetPolicy);
}

void WindowAnimator::removeAnimation(KWin::EffectWindow* handle)
{
    m_animations.erase(handle);
}

void WindowAnimator::clear()
{
    m_animations.clear();
}

int WindowAnimator::reapAnimationsForClock(const PhosphorAnimation::IMotionClock* clock)
{
    if (!clock) {
        return 0;
    }
    QVarLengthArray<KWin::EffectWindow*, kMaxInlineHandlesPerTick> handles;
    for (const auto& [handle, anim] : m_animations) {
        if (anim.spec().clock == clock) {
            handles.append(handle);
        }
    }
    int reaped = 0;
    for (KWin::EffectWindow* handle : handles) {
        auto it = m_animations.find(handle);
        if (it == m_animations.end()) {
            continue;
        }
        if (it->second.spec().clock != clock) {
            continue;
        }
        const QMarginsF padding = expandedPadding(handle, it->second);
        const QRectF bounds = it->second.bounds().marginsAdded(padding);
        PhosphorAnimation::AnimatedValue<QRectF> reapedAnim = std::move(it->second);
        m_animations.erase(it);
        onAnimationReaped(handle, reapedAnim);
        if (bounds.isValid()) {
            onRepaintNeeded(handle, bounds);
        }
        ++reaped;
    }
    return reaped;
}

// ═══════════════════════════════════════════════════════════════════════
// State queries
// ═══════════════════════════════════════════════════════════════════════

bool WindowAnimator::isAnimatingToTarget(KWin::EffectWindow* handle, const QRectF& target) const
{
    auto it = m_animations.find(handle);
    if (it == m_animations.end()) {
        return false;
    }
    const QRectF to = it->second.to();
    return qAbs(to.x() - target.x()) < PhosphorAnimation::kRectSizeEpsilonPx
        && qAbs(to.y() - target.y()) < PhosphorAnimation::kRectSizeEpsilonPx
        && qAbs(to.width() - target.width()) < PhosphorAnimation::kRectSizeEpsilonPx
        && qAbs(to.height() - target.height()) < PhosphorAnimation::kRectSizeEpsilonPx;
}

QRectF WindowAnimator::currentValue(KWin::EffectWindow* handle, const QRectF& fallback) const
{
    auto it = m_animations.find(handle);
    if (it == m_animations.end()) {
        return fallback;
    }
    return it->second.value();
}

const PhosphorAnimation::AnimatedValue<QRectF>* WindowAnimator::animationFor(KWin::EffectWindow* handle) const
{
    auto it = m_animations.find(handle);
    return (it == m_animations.end()) ? nullptr : &it->second;
}

QRectF WindowAnimator::animationBounds(KWin::EffectWindow* handle) const
{
    auto it = m_animations.find(handle);
    if (it == m_animations.end()) {
        return QRectF();
    }
    const QMarginsF padding = expandedPadding(handle, it->second);
    return it->second.bounds().marginsAdded(padding);
}

// ═══════════════════════════════════════════════════════════════════════
// Per-frame
// ═══════════════════════════════════════════════════════════════════════

void WindowAnimator::advanceAnimations()
{
    QVarLengthArray<KWin::EffectWindow*, kMaxInlineHandlesPerTick> handles;
    handles.reserve(m_animations.size());
    for (const auto& [handle, _] : m_animations) {
        handles.append(handle);
    }

    for (KWin::EffectWindow* handle : handles) {
        auto it = m_animations.find(handle);
        if (it == m_animations.end()) {
            continue;
        }
        if (!isHandleValid(handle)) {
            PhosphorAnimation::AnimatedValue<QRectF> abandoned = std::move(it->second);
            m_animations.erase(it);
            onAnimationAbandoned(handle, abandoned);
            continue;
        }

        // Per-tick clock re-resolution for output migration.
        if (PhosphorAnimation::IMotionClock* resolved = clockForHandle(handle)) {
            PhosphorAnimation::IMotionClock* current = it->second.spec().clock;
            if (resolved != current && PhosphorAnimation::IMotionClock::epochCompatible(current, resolved)) {
                it->second.rebindClock(resolved);
            }
        }

        it->second.advance();

        // Re-lookup: advance() fires callbacks that may re-enter and rehash.
        it = m_animations.find(handle);
        if (it == m_animations.end()) {
            continue;
        }

        if (it->second.isComplete()) {
            const QMarginsF padding = expandedPadding(handle, it->second);
            const QRectF bounds = it->second.bounds().marginsAdded(padding);
            PhosphorAnimation::AnimatedValue<QRectF> finished = std::move(it->second);
            m_animations.erase(it);
            onAnimationComplete(handle, finished);
            if (bounds.isValid()) {
                onRepaintNeeded(handle, bounds);
            }
        }
    }
}

void WindowAnimator::scheduleRepaints() const
{
    for (const auto& [handle, anim] : m_animations) {
        const QMarginsF padding = expandedPadding(handle, anim);
        const QRectF bounds = anim.bounds().marginsAdded(padding);
        if (bounds.isValid()) {
            onRepaintNeeded(handle, bounds);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
// KWin-specific: applyTransform
// ═══════════════════════════════════════════════════════════════════════

void WindowAnimator::applyTransform(KWin::EffectWindow* window, KWin::WindowPaintData& data) const
{
    if (!window || window->isDeleted()) {
        return;
    }

    const PhosphorAnimation::AnimatedValue<QRectF>* anim = animationFor(window);
    if (!anim || !anim->isAnimating()) {
        return;
    }

    const QRectF current = anim->value();

    // Translate: desired visual top-left offset from the actual frameGeometry.
    const QPointF desiredPos = current.topLeft();
    const QPointF actualPos = window->frameGeometry().topLeft();
    data += (desiredPos - actualPos);

    // Scale: smoothly morph from old size to target size.
    if (anim->hasSizeChange()) {
        const QSizeF desiredSize = current.size();
        const QSizeF actualSize = window->frameGeometry().size();
        constexpr qreal kMinActualDim = 1.0;
        constexpr qreal kMinScaleFactor = 0.01;
        constexpr qreal kMaxScaleFactor = 100.0;
        const qreal sx =
            qBound(kMinScaleFactor, desiredSize.width() / qMax(actualSize.width(), kMinActualDim), kMaxScaleFactor);
        const qreal sy =
            qBound(kMinScaleFactor, desiredSize.height() / qMax(actualSize.height(), kMinActualDim), kMaxScaleFactor);
        data.setXScale(data.xScale() * sx);
        data.setYScale(data.yScale() * sy);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Hook implementations (formerly virtual overrides)
// ═══════════════════════════════════════════════════════════════════════

void WindowAnimator::onAnimationStarted(KWin::EffectWindow* window,
                                        const PhosphorAnimation::AnimatedValue<QRectF>& anim)
{
    if (window) {
        window->addRepaintFull();
    }
    qCDebug(lcEffect) << "Started animation from" << anim.from() << "to" << anim.to()
                      << "duration:" << anim.spec().profile.effectiveDuration();
}

void WindowAnimator::onAnimationComplete(KWin::EffectWindow* window,
                                         const PhosphorAnimation::AnimatedValue<QRectF>& anim)
{
    qCDebug(lcEffect) << "Window snap animation complete:" << static_cast<const void*>(window)
                      << "target:" << anim.to();
    if (m_onAnimationCompleteCallback) {
        m_onAnimationCompleteCallback(window);
    }
}

void WindowAnimator::onAnimationReplaced(KWin::EffectWindow* window,
                                         const PhosphorAnimation::AnimatedValue<QRectF>& displaced)
{
    if (window && !window->isDeleted()) {
        const QRectF bounds = displaced.value();
        if (bounds.isValid()) {
            KWin::effects->addRepaint(KWin::Rect(bounds.toAlignedRect()));
        }
    }
    qCDebug(lcEffect) << "Window snap animation replaced:" << static_cast<const void*>(window)
                      << "displaced-from:" << displaced.from() << "displaced-to:" << displaced.to();
}

void WindowAnimator::onAnimationRetargeted(KWin::EffectWindow* window,
                                           const PhosphorAnimation::AnimatedValue<QRectF>& anim)
{
    qCDebug(lcEffect) << "Window snap animation retargeted:" << static_cast<const void*>(window)
                      << "new-from:" << anim.from() << "new-to:" << anim.to();
}

void WindowAnimator::onAnimationReaped(KWin::EffectWindow* window, const PhosphorAnimation::AnimatedValue<QRectF>& anim)
{
    qCDebug(lcEffect) << "Window snap animation reaped (output teardown):" << static_cast<const void*>(window)
                      << "target:" << anim.to();
}

void WindowAnimator::onAnimationAbandoned(KWin::EffectWindow* window,
                                          const PhosphorAnimation::AnimatedValue<QRectF>& anim)
{
    qCDebug(lcEffect) << "Window snap animation abandoned (handle invalidated):" << static_cast<const void*>(window)
                      << "target:" << anim.to();
}

void WindowAnimator::onRepaintNeeded(KWin::EffectWindow*, const QRectF& bounds) const
{
    if (bounds.isValid()) {
        KWin::effects->addRepaint(KWin::Rect(bounds.toAlignedRect()));
    }
}

bool WindowAnimator::isHandleValid(KWin::EffectWindow* window) const
{
    return window && !window->isDeleted();
}

QMarginsF WindowAnimator::expandedPadding(KWin::EffectWindow* window,
                                          const PhosphorAnimation::AnimatedValue<QRectF>& anim) const
{
    const QRectF expanded = (window && !window->isDeleted()) ? QRectF(window->expandedGeometry()) : anim.to();

    const QRectF frameGeo = anim.to();
    return QMarginsF(qMax(0.0, frameGeo.x() - expanded.x()), qMax(0.0, frameGeo.y() - expanded.y()),
                     qMax(0.0, expanded.right() - frameGeo.right()), qMax(0.0, expanded.bottom() - frameGeo.bottom()));
}

PhosphorAnimation::IMotionClock* WindowAnimator::clockForHandle(KWin::EffectWindow* window) const
{
    if (m_outputClockResolver && window && !window->isDeleted()) {
        if (auto* clk = m_outputClockResolver(window->screen())) {
            return clk;
        }
    }
    return m_clock;
}

// ═══════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════

void WindowAnimator::clampProfile(PhosphorAnimation::Profile& profile)
{
    if (profile.duration) {
        if (!std::isfinite(*profile.duration)) {
            profile.duration.reset();
        } else {
            profile.duration = qBound(qreal(0.0), *profile.duration, kMaxDurationMs);
        }
    }
    if (profile.minDistance) {
        profile.minDistance = qBound(0, *profile.minDistance, kMaxMinDistancePx);
    }
}

} // namespace PlasmaZones
