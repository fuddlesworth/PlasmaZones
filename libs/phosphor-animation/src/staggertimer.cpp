// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/StaggerTimer.h>

#include <QTimer>

#include <limits>
#include <memory>

namespace PhosphorAnimation {

void applyStaggeredOrImmediate(QObject* parent, int count, SequenceMode sequenceMode, int staggerInterval,
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
    const bool stagger = parent && (sequenceMode == SequenceMode::Cascade) && (count > 1) && (staggerInterval > 0);

    if (stagger) {
        applyFn(0);
        // Share onComplete via a shared_ptr captured by every timer
        // lambda — without this, a count=N cascade keeps N-1 copies of
        // the std::function (and its captured state) alive until each
        // timer fires.
        auto sharedOnComplete =
            onComplete ? std::make_shared<std::function<void()>>(onComplete) : std::shared_ptr<std::function<void()>>();
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
            QTimer::singleShot(delay, parent, [applyFn, sharedOnComplete, i, isLast]() {
                applyFn(i);
                if (isLast && sharedOnComplete && *sharedOnComplete) {
                    (*sharedOnComplete)();
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

} // namespace PhosphorAnimation
