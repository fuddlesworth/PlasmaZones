// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QObject>

#include <functional>

namespace PhosphorAnimation {

/**
 * @brief Apply a sequence of operations, optionally cascaded with stagger.
 *
 * When @p sequenceMode is `1` (one-by-one) and @p staggerInterval > 0,
 * each `applyFn(i)` call is delayed by `i * staggerInterval` ms via
 * `QTimer::singleShot`, producing the visual cascade familiar from
 * tiling shells. Otherwise all calls are immediate and @p onComplete
 * fires synchronously at the end.
 *
 * The integer @p sequenceMode matches `PhosphorAnimation::SequenceMode`
 * (0 = AllAtOnce, 1 = Cascade). The int signature is preserved here so
 * D-Bus/config integers can flow straight through without translation;
 * higher-level API surfaces (Profile, AnimationConfig) use the enum.
 *
 * @param parent          QObject that owns the QTimer context.
 * @param count           Number of items to process.
 * @param sequenceMode    `0` = all at once; `1` = cascade.
 * @param staggerInterval Milliseconds between cascade starts.
 * @param applyFn         Called once per index `i ∈ [0, count)`.
 * @param onComplete      Optional callback after all items are applied.
 *
 * ## Parent-guard contract
 *
 * `QTimer::singleShot(delay, parent, lambda)` cancels the lambda if
 * @p parent is destroyed before the timer fires — that covers the
 * common case where the cascade outlives its initiator.
 *
 * The parent guard **does not** protect objects captured *inside*
 * @p applyFn / @p onComplete. If you write `[this](int i){ ... }` and
 * `this` is some subsystem whose lifetime is shorter than @p parent,
 * captured `this` can dangle when the lambda fires later.
 *
 * Callers must ensure one of:
 *   - (a) the captured receiver IS @p parent, or
 *   - (b) the captured receiver is owned by @p parent and cannot
 *         outlive it, or
 *   - (c) the lambda uses a `QPointer<T>` weak capture and checks it
 *         before use.
 *
 * In PlasmaZones today all callers satisfy (b) — they capture
 * subsystems owned by the compositor effect which is itself @p parent.
 */
PHOSPHORANIMATION_EXPORT void applyStaggeredOrImmediate(QObject* parent, int count, int sequenceMode,
                                                        int staggerInterval, const std::function<void(int)>& applyFn,
                                                        const std::function<void()>& onComplete = nullptr);

/**
 * @brief Type-safe overload taking @ref SequenceMode.
 *
 * Forwards to the int-taking form via `static_cast`. Callers that hold a
 * `SequenceMode` enum (Profile / AnimationConfig / anything above the
 * D-Bus marshalling boundary) should prefer this overload rather than
 * casting to int at every call site.
 */
PHOSPHORANIMATION_EXPORT void applyStaggeredOrImmediate(QObject* parent, int count, SequenceMode sequenceMode,
                                                        int staggerInterval, const std::function<void(int)>& applyFn,
                                                        const std::function<void()>& onComplete = nullptr);

} // namespace PhosphorAnimation
