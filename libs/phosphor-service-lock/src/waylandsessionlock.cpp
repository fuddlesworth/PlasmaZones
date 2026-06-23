// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "waylandsessionlock.h"

namespace PhosphorServiceLock {

WaylandSessionLock::WaylandSessionLock(QObject* parent)
    : ISessionLock(parent)
{
    // The device's signals carry the same (empty) payloads as the seam's, so
    // forward them directly rather than through relay lambdas.
    connect(&m_lock, &PhosphorWayland::SessionLock::locked, this, &ISessionLock::locked);
    connect(&m_lock, &PhosphorWayland::SessionLock::finished, this, &ISessionLock::finished);
}

void WaylandSessionLock::lock()
{
    m_lock.lock();
}

void WaylandSessionLock::unlockAndDestroy()
{
    m_lock.unlockAndDestroy();
}

bool WaylandSessionLock::isLocked() const
{
    return m_lock.isLocked();
}

} // namespace PhosphorServiceLock
