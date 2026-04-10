// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stagger_timer.h"

#include <QTimer>

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
        for (int i = 1; i < count; ++i) {
            const int delay = i * staggerInterval;
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
