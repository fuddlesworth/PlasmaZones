// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceLock/LockService.h>

#include <PhosphorWayland/SessionLock.h>

namespace PhosphorServiceLock {

LockService::LockService(QObject* parent)
    : QObject(parent)
{
}

bool LockService::isSupported() const
{
    // The compositor must advertise ext-session-lock-v1 (surfaced by
    // PhosphorWayland::SessionLock) for the service to lock the session.
    return PhosphorWayland::SessionLock::isSupported();
}

} // namespace PhosphorServiceLock
