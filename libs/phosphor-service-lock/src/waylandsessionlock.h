// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal (not installed) ISessionLock backed by the foundation
// ext-session-lock-v1 client. The production state machine wires this to the
// compositor; it forwards PhosphorWayland::SessionLock's locked / finished
// signals into the seam and delegates the lock / unlock requests.

#include "isessionlock.h"

#include <PhosphorWayland/SessionLock.h>

namespace PhosphorServiceLock {

class WaylandSessionLock : public ISessionLock
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(WaylandSessionLock)

public:
    explicit WaylandSessionLock(QObject* parent = nullptr);

    void lock() override;
    void unlockAndDestroy() override;
    [[nodiscard]] bool isLocked() const override;

private:
    PhosphorWayland::SessionLock m_lock;
};

} // namespace PhosphorServiceLock
