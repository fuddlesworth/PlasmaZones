// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/MotionSpec.h>

#include <type_traits>
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
 *
 * @param emitValueChanged When `false`, the `valueChanged` signal is
 *                         suppressed. Used by `finishImpl` — the
 *                         spec's `onValueChanged` callback (installed
 *                         by `makeStartSpec`) already fires
 *                         `valueChanged` from inside
 *                         `AnimatedValue::finish`, so this helper's
 *                         snapshot-diff emit would double-count the
 *                         terminal-value transition. `startImpl` /
 *                         `retargetImpl` / `cancelImpl` all keep
 *                         the default (true) because no callback
 *                         fires for their initial / cancel transitions.
 */
template<typename Derived, typename T>
void emitChangedSignals(Derived* self, const AnimatedValue<T>& av, const AnimatedValueSnapshot<T>& prev,
                        bool emitValueChanged = true)
{
    if (av.from() != prev.from) {
        Q_EMIT self->fromChanged();
    }
    if (av.to() != prev.to) {
        Q_EMIT self->toChanged();
    }
    if (emitValueChanged && av.value() != prev.value) {
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
 *
 * `AnimatedValue::finish()` fires the spec's `onValueChanged`
 * callback (which in turn emits `self->valueChanged()`) and then
 * `onComplete` (which emits `animatingChanged` + `completeChanged`).
 * The snapshot-diff path below therefore skips `valueChanged` —
 * re-emitting from the diff would produce a spurious double-tick for
 * the terminal-value transition. `animatingChanged` and
 * `completeChanged` are ALSO emitted by the spec callback, but
 * re-emitting them from the snapshot-diff is harmless because those
 * are boolean-edge signals and QML bindings are idempotent on
 * repeated same-value notifications; we keep the diff emits for them
 * so the signal still fires when `finish()` is called on an already-
 * completed animation (idempotent no-op path — the callback never
 * fires because the early return in `AnimatedValue::finish()` bypasses
 * both callbacks).
 */
template<typename Derived, typename T>
void finishImpl(Derived* self, AnimatedValue<T>& av)
{
    const auto prev = AnimatedValueSnapshot<T>::capture(av);
    av.finish();
    emitChangedSignals(self, av, prev, /*emitValueChanged=*/false);
}

/**
 * @brief Snapshot of the wrapper-level (NOT AnimatedValue<T>-level)
 *        observables, captured via the wrapper's own accessors.
 *
 * Built for the Color wrapper, whose accessors dispatch through an
 * active-instance ternary across two `AnimatedValue<QColor, Space>`
 * fields — the `AnimatedValueSnapshot<T>::capture` path does not
 * apply because there is no single `AnimatedValue<T>` to snapshot.
 * `runWithDiffEmit` (below) drives this snapshot-then-diff-emit
 * pattern off the wrapper's `from()/to()/value()/isAnimating()/
 * isComplete()` instead.
 *
 * Reused for `setColorSpace`'s flip path, `start`, `retarget`,
 * `cancel`, and `finish` — all five sites previously hand-rolled the
 * same five `prev*` locals + five conditional `Q_EMIT` blocks.
 */
template<typename T>
struct WrapperStateSnapshot
{
    T from;
    T to;
    T value;
    bool animating;
    bool complete;
};

/**
 * @brief Capture-mutate-diff-emit helper for wrappers whose accessors
 *        cannot be expressed as a single `AnimatedValue<T>` (e.g.
 *        the Color wrapper's per-space dispatch).
 *
 * 1. Snapshots `self->from()`, `self->to()`, `self->value()`,
 *    `self->isAnimating()`, `self->isComplete()` BEFORE @p op runs.
 * 2. Calls @p op (the user-supplied mutation — `start`, `retarget`,
 *    `cancel`, `finish`, or the `setColorSpace` flip body).
 * 3. Re-reads each accessor and emits the matching `*Changed()`
 *    signal on @p self only for the fields that actually changed.
 *
 * @param emitValueChanged Mirrors `emitChangedSignals` — set to
 *                         `false` from `finish` paths whose underlying
 *                         core fires `onValueChanged` itself, to avoid
 *                         the double-tick on the terminal-value
 *                         transition.
 *
 * The helper is parameterised by `Self` rather than coupled to a
 * particular wrapper class so a future per-mode wrapper (e.g. one
 * that dispatches between two `AnimatedValue<T, ModeA>` and
 * `AnimatedValue<T, ModeB>` instances on a different policy axis)
 * can reuse the same snapshot/diff-emit machinery.
 */
template<typename Self, typename Op>
auto runWithDiffEmit(Self* self, Op&& op, bool emitValueChanged = true)
{
    using ValueT = decltype(self->value());
    const WrapperStateSnapshot<ValueT> prev{self->from(), self->to(), self->value(), self->isAnimating(),
                                            self->isComplete()};

    // op() may return void or a forwarded result (e.g. `bool` from
    // AnimatedValue::start / retarget). The if-constexpr split keeps
    // both shapes well-formed without forcing every call site to
    // wrap a void op in a return-tagged lambda.
    if constexpr (std::is_void_v<decltype(op())>) {
        op();
        if (self->from() != prev.from) {
            Q_EMIT self->fromChanged();
        }
        if (self->to() != prev.to) {
            Q_EMIT self->toChanged();
        }
        if (emitValueChanged && self->value() != prev.value) {
            Q_EMIT self->valueChanged();
        }
        if (self->isAnimating() != prev.animating) {
            Q_EMIT self->animatingChanged();
        }
        if (self->isComplete() != prev.complete) {
            Q_EMIT self->completeChanged();
        }
        return;
    } else {
        auto result = op();
        if (self->from() != prev.from) {
            Q_EMIT self->fromChanged();
        }
        if (self->to() != prev.to) {
            Q_EMIT self->toChanged();
        }
        if (emitValueChanged && self->value() != prev.value) {
            Q_EMIT self->valueChanged();
        }
        if (self->isAnimating() != prev.animating) {
            Q_EMIT self->animatingChanged();
        }
        if (self->isComplete() != prev.complete) {
            Q_EMIT self->completeChanged();
        }
        return result;
    }
}

} // namespace PhosphorAnimation::detail
