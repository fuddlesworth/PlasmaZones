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
    // applyFn is null-checked for the same reason onComplete is at all three
    // of its call sites: an empty std::function throws bad_function_call, and
    // in the compositor process that is the session. Every caller passes a
    // real lambda today, so this is hardening — but the count and parent
    // guards above already show this function does not trust its inputs, and
    // applyFn was the one input it did.
    if (!applyFn) {
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
        // applyFn shares the same way, and needs to more than onComplete does.
        // It is the one that carries the batch: a caller typically captures the
        // whole placement list in it, so a by-value capture per timer would
        // make a count=N cascade hold N-1 copies of that list — quadratic in
        // the window count, for the lifetime of the longest delay.
        auto sharedApply = std::make_shared<std::function<void(int)>>(applyFn);
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
            QTimer::singleShot(delay, parent, [sharedApply, sharedOnComplete, i, isLast]() {
                (*sharedApply)(i);
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
