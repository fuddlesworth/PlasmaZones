// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/StaggerTimer.h>

#include <QTimer>

#include <limits>

namespace PhosphorAnimation {

void applyStaggeredOrImmediate(QObject* parent, int count, int sequenceMode, int staggerInterval,
                               const std::function<void(int)>& applyFn, const std::function<void()>& onComplete)
{
    if (count <= 0) {
        if (onComplete) {
            onComplete();
        }
        return;
    }

    // parent == nullptr disables the Qt context guard — QTimer::singleShot
    // would crash or leak. Fall through to the synchronous path in that
    // case; callers should pass a valid parent but we stay defensive.
    const bool stagger = parent && (sequenceMode == 1) && (count > 1) && (staggerInterval > 0);

    if (stagger) {
        applyFn(0);
        // qint64 intermediate + clamp prevents silent negative-overflow
        // for large counts: QTimer::singleShot takes int ms, so plain
        // (i * staggerInterval) can wrap. Clamp at INT_MAX — the delay
        // is already pathological at that point and the clamp yields
        // "fire as soon as possible after the cap".
        for (int i = 1; i < count; ++i) {
            const qint64 rawDelay = static_cast<qint64>(i) * static_cast<qint64>(staggerInterval);
            const int delay = rawDelay > std::numeric_limits<int>::max() ? std::numeric_limits<int>::max()
                                                                         : static_cast<int>(rawDelay);
            const bool isLast = (i == count - 1);
            QTimer::singleShot(delay, parent, [applyFn, onComplete, i, isLast]() {
                applyFn(i);
                if (isLast && onComplete) {
                    onComplete();
                }
            });
        }
    } else {
        for (int i = 0; i < count; ++i) {
            applyFn(i);
        }
        if (onComplete) {
            onComplete();
        }
    }
}

void applyStaggeredOrImmediate(QObject* parent, int count, SequenceMode sequenceMode, int staggerInterval,
                               const std::function<void(int)>& applyFn, const std::function<void()>& onComplete)
{
    applyStaggeredOrImmediate(parent, count, static_cast<int>(sequenceMode), staggerInterval, applyFn, onComplete);
}

} // namespace PhosphorAnimation
