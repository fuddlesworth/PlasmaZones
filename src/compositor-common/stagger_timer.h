// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <functional>

namespace PlasmaZones {

/**
 * @brief Apply a series of operations with optional stagger timing.
 *
 * When sequence mode is "one by one" (sequenceMode == 1) and stagger interval > 0,
 * each applyFn(i) call is delayed by i * staggerInterval ms (cascading).
 * Otherwise all calls are immediate.
 *
 * @param parent          QObject parent for timer ownership
 * @param count           Number of items to process
 * @param sequenceMode    0=all at once, 1=one by one in zone order
 * @param staggerInterval Ms between each window start when cascading
 * @param applyFn         Called with index [0, count). Must capture by value
 *                        (lambda may fire asynchronously via QTimer).
 * @param onComplete      Optional callback after all items are processed.
 *
 * @warning applyFn and onComplete fire asynchronously via QTimer::singleShot.
 * @p parent is used as the QTimer context: if it is destroyed before the timer
 * fires, Qt skips the invocation entirely (lambda is not called).
 *
 * HOWEVER, this parent guard does NOT protect objects captured *inside*
 * applyFn / onComplete. If a caller writes `[this](int i) { ... }` where
 * `this` is some subsystem whose lifetime is shorter than @p parent, the
 * captured `this` may dangle when the lambda fires. Callers MUST ensure one
 * of the following:
 *   (a) the captured receiver is the same object as @p parent, OR
 *   (b) the captured receiver is owned by @p parent and cannot outlive it, OR
 *   (c) the lambda uses a QPointer<T> weak capture and checks it before use.
 *
 * In practice all current callers fall under (b): they capture subsystems
 * owned by the compositor effect which is itself @p parent.
 */
void applyStaggeredOrImmediate(QObject* parent, int count, int sequenceMode, int staggerInterval,
                               const std::function<void(int)>& applyFn,
                               const std::function<void()>& onComplete = nullptr);

} // namespace PlasmaZones
