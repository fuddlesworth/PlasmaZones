// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stagger_timer.h"

#include <QTimer>

#include <limits>

namespace PlasmaZones {

void applyStaggeredOrImmediate(QObject* parent, int count, int sequenceMode, int staggerInterval,
                               const std::function<void(int)>& applyFn, const std::function<void()>& onComplete)
{
    if (count <= 0) {
        if (onComplete) {
            onComplete();
        }
        return;
    }

    // If parent is null, QTimer::singleShot would be UB/crash — fall through to immediate path.
    const bool stagger = parent && (sequenceMode == 1) && (count > 1) && (staggerInterval > 0);

    if (stagger) {
        applyFn(0);
        // Use qint64 intermediate + clamp: QTimer::singleShot takes int ms, so
        // (i * staggerInterval) can silently overflow to negative for large
        // counts. Clamp at INT_MAX — beyond that the delays are pathological
        // anyway and clamping yields "fire as soon as possible after the cap".
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

} // namespace PlasmaZones
