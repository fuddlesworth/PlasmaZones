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
 * @warning applyFn fires asynchronously via QTimer::singleShot. Lambdas must
 * not capture pointers with lifetimes shorter than @p parent. Capture by value
 * or ensure captured objects outlive the parent QObject.
 */
void applyStaggeredOrImmediate(QObject* parent, int count, int sequenceMode, int staggerInterval,
                               const std::function<void(int)>& applyFn,
                               const std::function<void()>& onComplete = nullptr);

} // namespace PlasmaZones
