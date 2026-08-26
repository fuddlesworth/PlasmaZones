// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowanimator.h"

#include "plasmazoneseffect/shader_internal.h"
#include "compositor/effectlogging.h"

#include <PhosphorAnimation/Curve.h>

#include <effect/effect.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>

#include <QLoggingCategory>

#include <utility>

namespace PlasmaZones {

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

void WindowAnimator::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    if (!m_enabled && !m_animations.empty()) {
        // Reap in-flight animations on disable. advanceAnimations has no
        // m_enabled gate, so entries live from before the toggle would keep
        // driving the window toward their OLD target — and the snap path's
        // disabled fall-through commits the NEW geometry with a plain
        // moveResize and no retarget, exactly the "wrong animation" state
        // the retarget machinery exists to prevent. Same padded-damage
        // shape as reapAnimationsForClock so the final frames are cleaned.
        QVarLengthArray<KWin::EffectWindow*, kMaxInlineHandlesPerTick> handles;
        for (const auto& [handle, anim] : m_animations) {
            handles.append(handle);
        }
        for (KWin::EffectWindow* handle : handles) {
            auto it = m_animations.find(handle);
            if (it == m_animations.end()) {
                continue;
            }
            const QMarginsF padding = expandedPadding(handle, it->second);
            const QRectF bounds = it->second.bounds().marginsAdded(padding);
            PhosphorAnimation::AnimatedValue<QRectF> reapedAnim = std::move(it->second);
            m_animations.erase(it);
            // Deliberately NOT m_onAnimationCompleteCallback: that callback
            // calls endShaderTransition(), which does glDeleteProgram /
            // glDeleteTextures and is only valid with the compositor's GL
            // context current — i.e. inside a paint pass. setEnabled() fires
            // from a settings-change signal, between frames, with no current
            // context, so firing the completion callback here would delete GL
            // objects against the wrong (or no) context. The animator-driven
            // shader transition riding this reaped timeline is instead
            // reconciled on the next paint by the shader manager's own
            // damage-driven path. Only the debug-log reap hook runs here.
            onAnimationReaped(handle, reapedAnim);
            if (bounds.isValid()) {
                onRepaintNeeded(handle, bounds);
            }
        }
    }
}

bool WindowAnimator::isEnabled() const
{
    return m_enabled;
}

const PhosphorAnimation::Profile& WindowAnimator::profile() const
{
    return m_profile;
}

void WindowAnimator::setCurve(std::shared_ptr<const PhosphorAnimation::Curve> curve)
{
    m_profile.curve = std::move(curve);
}

void WindowAnimator::setDuration(qreal ms)
{
    m_profile.duration = ms;
    clampProfile(m_profile);
}

void WindowAnimator::setMinDistance(int pixels)
{
    m_profile.minDistance = pixels;
    clampProfile(m_profile);
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

bool WindowAnimator::hasAnimationsIntersecting(const QRectF& region) const
{
    for (const auto& [handle, anim] : m_animations) {
        // Same bounds the repaint sites damage: the full swept range padded
        // out to the expanded geometry, so a cross-screen morph counts on
        // both outputs for its whole flight.
        const QRectF bounds = anim.bounds().marginsAdded(expandedPadding(handle, anim));
        if (bounds.isValid() && bounds.intersects(region)) {
            return true;
        }
    }
    return false;
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
    // PreserveVelocity is the fixed policy for fresh starts: the
    // set-retarget-policy knob had no caller anywhere (audit census) and was
    // removed rather than left looking configurable.
    params.retargetPolicy = PhosphorAnimation::RetargetPolicy::PreserveVelocity;
    auto spec = PhosphorAnimation::SnapPolicy::createSnapSpec(oldFrame, newFrame, params, clk);
    if (!spec) {
        return StartResult::PolicyRejected;
    }
    // Bound a STATEFUL (spring) curve's lifetime by the SAME rule the shader leg
    // uses. A spring ignores profile.duration and runs on its physics, so
    // clampProfile above cannot reach it: without this, the shader transition for
    // one curve is cut at MaxAnimationDurationMs (2 s) while this geometry
    // animation keeps requesting frames out to the curve's raw settleTime —
    // seconds later, with no shader left on the window. Slider-reachable, not just
    // hand-editable: "spring:10,0.15" settles in 3.5 s. Routing both legs through
    // resolveTransitionLifetimeMs makes them provably agree rather than
    // coincidentally agree.
    //
    // A stateless curve is unaffected — but NOT because the helper hands the
    // nominal straight back: it qBounds every result into [50, 2000], stateless
    // included, so a nominal above that ceiling does come back clamped. The
    // reason it does not matter is that `AnimatedValue::advance` consults
    // `maxLifetimeMs` only in the STATEFUL branch.
    spec->maxLifetimeMs = ShaderInternal::resolveTransitionLifetimeMs(qRound(spec->profile.effectiveDuration()),
                                                                      spec->profile.curve.get());

    PhosphorAnimation::AnimatedValue<QRectF> anim;
    if (!anim.start(oldFrame, newFrame, *spec)) {
        if (auto existing = m_animations.find(handle); existing != m_animations.end()) {
            PhosphorAnimation::AnimatedValue<QRectF> displaced = std::move(existing->second);
            m_animations.erase(existing);
            // As with the reap path in setEnabled(), the displaced timeline's
            // animator-driven shader transition is not torn down via the
            // completion callback here: startAnimation can be reached outside a
            // paint pass, and endShaderTransition()'s GL deletes need the
            // compositor context current. onAnimationReplaced only issues a
            // padded repaint; the shader manager reconciles the superseded
            // transition on the next paint.
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

    // Release path: DROP the animation rather than leaving it in the map heading
    // to its OLD target. No caller distinguishes an InternalError — one tests
    // for DegenerateReap and the other discards the result entirely — so it
    // falls through either way, and by then `moveResize(targetFrame)` has
    // already committed the new geometry, so a surviving stale animation would
    // visibly drive the window to the wrong rect and snap on completion.
    // Degrading to "no animation" is contained; degrading to "wrong animation"
    // is not. Unreachable today (it means AnimatedValue had no spec installed),
    // which is why the assert stays.
    Q_ASSERT_X(false, "WindowAnimator::retargetWithResult",
               "AnimatedValue rejected retarget without marking complete — spec was never installed");
    if (auto stale = m_animations.find(handle); stale != m_animations.end()) {
        // United with `preservedBounds`, mirroring the DegenerateReap path above.
        // Post-failure bounds alone are not enough: the condition this branch
        // describes is "no spec was installed", in which case `bounds()` is a
        // default-constructed QRectF and the erase would schedule NO repaint at
        // all, leaving the last animated frame on screen. `preservedBounds` was
        // captured before `retarget()` and is already correct.
        const QRectF postBounds = stale->second.bounds().marginsAdded(expandedPadding(handle, stale->second));
        const QRectF bounds = preservedBounds.isValid() ? preservedBounds.united(postBounds) : postBounds;
        m_animations.erase(stale);
        if (bounds.isValid()) {
            onRepaintNeeded(handle, bounds);
        }
    }
    return RetargetResult::InternalError;
}

PhosphorAnimation::RetargetResult WindowAnimator::retargetWithResult(KWin::EffectWindow* handle, const QRectF& newFrame)
{
    using PhosphorAnimation::RetargetResult;

    // The guards are repeated here even though the three-arg overload runs them
    // again, and deliberately so: they establish the RESULT ORDER. Dropping them
    // and letting the delegate own them would make a disabled animator with an
    // unknown handle report UnknownHandle instead of Disabled, which is an
    // observable change in the returned enum. Two extra branches on a
    // non-per-frame path is the cheaper side of that trade.
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

    // Translate ONLY — the position is animated, the content is never resampled.
    //
    // This used to also setXScale/setYScale from the animated size over the live
    // frame size whenever the animation had a size change. KWin has already
    // committed the FINAL geometry and the client has already painted at the
    // final size by the time this runs, so that scale took finished content and
    // squeezed it into the in-flight rect, easing back to 1.0 only at the end —
    // a visible stretch-then-settle on every size-changing leg (discussion
    // #868). WindowPaintData offers no crop, so the honest fallback is to draw
    // the window at its true size and animate only where it sits.
    //
    // Reachable only when NO shader owns the geometry: paintWindow calls this
    // solely under `!shaderOwnsGeometry`, and the built-in default for the snap
    // legs (window-morph) declares iFromRect. So this is the path a user takes
    // by choosing "None" — and "None" must not mean "the stretching one". The
    // vertex-only fragment path reaches the same conclusion for packs.
    const QPointF desiredPos = current.topLeft();
    const QPointF actualPos = window->frameGeometry().topLeft();
    data += (desiredPos - actualPos);
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
    // Padded and routed through onRepaintNeeded, like every other repaint site in
    // this file. The bare frame rect left a decorated window's shadow and border
    // region undamaged, so displacing an animation mid-drag could leave a shadow
    // trail. `expandedPadding` handles a deleted window itself, so the explicit
    // isDeleted() check is no longer needed here.
    const QRectF bounds = displaced.value().marginsAdded(expandedPadding(window, displaced));
    if (bounds.isValid()) {
        onRepaintNeeded(window, bounds);
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
    // CONSERVATIVE, not exact: the margins compare the live expandedGeometry
    // against anim.to(), which agree only while the window actually sits at
    // the animation's target. A moveResize away from a live animation's `to`
    // (large scrolling-engine column moves) inflates the derived margins by
    // the travel distance. The qMax(0.0, ...) floor makes the error one-sided
    // — bounds only GROW, so the per-output transformed-windows flag can be
    // over-set on a neighbouring output but never missed.
    const QRectF expanded = (window && !window->isDeleted()) ? QRectF(window->expandedGeometry()) : anim.to();

    const QRectF frameGeo = anim.to();
    QMarginsF margins(qMax(0.0, frameGeo.x() - expanded.x()), qMax(0.0, frameGeo.y() - expanded.y()),
                      qMax(0.0, expanded.right() - frameGeo.right()), qMax(0.0, expanded.bottom() - frameGeo.bottom()));

    // Cover what applyTransform actually DRAWS, which is no longer what
    // anim.bounds() describes. bounds() is the union of the from/to rects, but
    // the transform translates the window at its FINAL size to every position
    // along that sweep, so the drawn rect can reach past the union — a snap
    // travelling LEFT into a larger zone draws (old position + final width),
    // which for from(1000,0 400x300) → to(0,0 1200x900) overruns the union's
    // right edge by 800px. Undamaged, that band keeps whatever was under it and
    // the window trails.
    //
    // The shortfall on an axis is at most (final extent - the SMALLEST extent
    // the sweep passes through): bounds.right is never less than
    // position(t) + width(t) for any sampled t, and the drawn right is
    // position(t) + finalWidth. Bounding it that way needs no position term, so
    // it stays correct under the overshoot samples bounds() already folds in,
    // and it self-zeroes for a pure move (all widths equal). One-sided like the
    // margins above: bounds only ever grow.
    const QSizeF finalSize = (window && !window->isDeleted()) ? window->frameGeometry().size() : anim.to().size();
    const qreal minWidth = qMin(anim.from().width(), anim.to().width());
    const qreal minHeight = qMin(anim.from().height(), anim.to().height());
    margins.setRight(margins.right() + qMax(0.0, finalSize.width() - minWidth));
    margins.setBottom(margins.bottom() + qMax(0.0, finalSize.height() - minHeight));
    return margins;
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
        // Non-positive is RESET, not clamped to zero. The library validator
        // rejects `duration <= 0` outright, and AnimatedValue treats a 0 ms
        // duration as instant-complete — clamping would turn a bad value into a
        // silently missing animation, where resetting falls back to the library
        // default like every other rejection here.
        if (!std::isfinite(*profile.duration) || *profile.duration <= 0.0) {
            profile.duration.reset();
        } else {
            profile.duration = qMin(*profile.duration, kMaxDurationMs);
        }
    }
    if (profile.minDistance) {
        profile.minDistance = qBound(0, *profile.minDistance, kMaxMinDistancePx);
    }
}

} // namespace PlasmaZones
