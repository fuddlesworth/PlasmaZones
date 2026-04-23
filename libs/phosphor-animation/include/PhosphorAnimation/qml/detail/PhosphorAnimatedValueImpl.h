// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/MotionSpec.h>

#include <utility>

namespace PhosphorAnimation::detail {

/**
 * @brief Shared per-T helpers that implement the bulk of the
 *        `PhosphorAnimated{Real,Point,Size,Rect}` lifecycle API.
 *
 * These helpers exist to delete the near-identical `startImpl`,
 * `retarget`, `cancel`, `finish`, and `onSync` bodies that otherwise
 * duplicate across four typed subclasses. Each helper takes:
 *
 *   - `av`     — reference to the owning class's `AnimatedValue<T>`
 *                member.
 *   - `self`   — pointer to the derived Q_OBJECT so the helper can
 *                invoke `Q_EMIT self->fooChanged()` on whatever typed
 *                signals the subclass declares. The helpers know
 *                nothing about Qt's signal machinery beyond the names
 *                `fromChanged / toChanged / valueChanged /
 *                animatingChanged / completeChanged`, which every
 *                `PhosphorAnimated*` wrapper in this library
 *                declares. A subclass that renames any of these would
 *                stop compiling here — that's the intended coupling.
 *
 * Intentionally NOT a CRTP base class:
 *   - `Q_OBJECT` does not cooperate with templated inheritance (MOC
 *     can't parse it), so a CRTP chain would force either a helper-
 *     base that is `Q_OBJECT`-aware via template specialisation (too
 *     fragile) or a non-`Q_OBJECT` CRTP that forwards every signal
 *     through the derived class (the boilerplate we're trying to
 *     eliminate).
 *   - A free-function helper set with `self`-pointer access keeps the
 *     subclass in full control of its signals and Q_INVOKABLE surface
 *     while sharing the state-change bookkeeping.
 *
 * The Color wrapper is deliberately NOT routed through these helpers —
 * it holds two `AnimatedValue<QColor, Space>` instances (Linear +
 * OkLab) with dispatch accessors, and the start/retarget/cancel/
 * finish paths differ enough (per-space dispatch inside the call) that
 * templating them would cost more than the duplication.
 */

/**
 * @brief Snapshot of the queryable AnimatedValue state used by the
 *        check-before-emit helpers.
 *
 * Captured BEFORE the call to `av.start/retarget/cancel/finish`;
 * compared AFTER to decide which NOTIFY signals fire.
 *
 * Rationale for check-before-emit: project rule "Only emit signals
 * when value actually changes" — pre-PR-344 every one of these
 * emitted unconditionally on every start() / retarget() / cancel() /
 * finish(), forcing QML binding re-eval up to 5 times per call even
 * for no-op state flips.
 */
template<typename T>
struct AnimatedValueSnapshot
{
    T from;
    T to;
    T value;
    bool animating;
    bool complete;

    static AnimatedValueSnapshot capture(const AnimatedValue<T>& av)
    {
        return AnimatedValueSnapshot{av.from(), av.to(), av.value(), av.isAnimating(), av.isComplete()};
    }
};

/**
 * @brief Emit the change notifications for any field that differs
 *        between @p prev and @p av.
 *
 * The Q_EMIT chain lives here rather than at the call site because
 * every wrapper fires exactly the same signals in exactly the same
 * order. Using `Q_EMIT self->name()` rather than a bare `Q_EMIT
 * name()` means the helper can be instantiated from a free scope —
 * it does not need to live inside the derived class's member
 * context.
 */
template<typename Derived, typename T>
void emitChangedSignals(Derived* self, const AnimatedValue<T>& av, const AnimatedValueSnapshot<T>& prev)
{
    if (av.from() != prev.from) {
        Q_EMIT self->fromChanged();
    }
    if (av.to() != prev.to) {
        Q_EMIT self->toChanged();
    }
    if (av.value() != prev.value) {
        Q_EMIT self->valueChanged();
    }
    if (av.isAnimating() != prev.animating) {
        Q_EMIT self->animatingChanged();
    }
    if (av.isComplete() != prev.complete) {
        Q_EMIT self->completeChanged();
    }
}

/**
 * @brief Build the `MotionSpec<T>` the subclass hands into
 *        `AnimatedValue<T>::start`, wiring the per-tick value
 *        callback and the completion callback to `Q_EMIT
 *        self->valueChanged / animatingChanged / completeChanged`.
 *
 * `profile` comes from the subclass's `PhosphorAnimatedValueBase::
 * profile()` accessor; `clock` from `resolveClock()`. This helper
 * owns the MotionSpec assembly so the four typed subclasses do not
 * each hand-roll the same lambda bodies.
 */
template<typename Derived, typename T>
MotionSpec<T> makeStartSpec(Derived* self, IMotionClock* clock, const Profile& profile)
{
    MotionSpec<T> spec;
    spec.profile = profile;
    spec.clock = clock;
    spec.onValueChanged = [self](const T&) {
        Q_EMIT self->valueChanged();
    };
    spec.onComplete = [self]() {
        Q_EMIT self->animatingChanged();
        Q_EMIT self->completeChanged();
    };
    return spec;
}

/**
 * @brief Shared body of each wrapper's `startImpl(from, to, clock)`.
 *
 * Returns false and does nothing if @p clock is null (symmetric with
 * the pre-refactor hand-written bodies).
 */
template<typename Derived, typename T>
bool startImpl(Derived* self, AnimatedValue<T>& av, T from, T to, IMotionClock* clock, const Profile& profile)
{
    if (!clock) {
        return false;
    }
    const auto prev = AnimatedValueSnapshot<T>::capture(av);
    const bool ok = av.start(std::move(from), std::move(to), makeStartSpec<Derived, T>(self, clock, profile));
    emitChangedSignals(self, av, prev);
    return ok;
}

/**
 * @brief Shared body of each wrapper's `retarget(to)`.
 */
template<typename Derived, typename T>
bool retargetImpl(Derived* self, AnimatedValue<T>& av, T to)
{
    const auto prev = AnimatedValueSnapshot<T>::capture(av);
    const bool ok = av.retarget(std::move(to));
    emitChangedSignals(self, av, prev);
    return ok;
}

/**
 * @brief Shared body of each wrapper's `cancel()`.
 *
 * cancel() cannot change `from/to/value` (AnimatedValue::cancel only
 * flips the running flags) so only the animating/complete signals
 * are candidates to fire — but the generic helper re-uses the full
 * snapshot path for consistency and future-proofing (if the core
 * contract ever widens to clear `m_current`, the helper picks up the
 * new emissions for free).
 */
template<typename Derived, typename T>
void cancelImpl(Derived* self, AnimatedValue<T>& av)
{
    const auto prev = AnimatedValueSnapshot<T>::capture(av);
    av.cancel();
    emitChangedSignals(self, av, prev);
}

/**
 * @brief Shared body of each wrapper's `finish()`.
 */
template<typename Derived, typename T>
void finishImpl(Derived* self, AnimatedValue<T>& av)
{
    const auto prev = AnimatedValueSnapshot<T>::capture(av);
    av.finish();
    emitChangedSignals(self, av, prev);
}

} // namespace PhosphorAnimation::detail
