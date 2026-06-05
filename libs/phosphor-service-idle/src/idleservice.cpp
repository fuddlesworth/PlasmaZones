// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceIdle/IdleService.h>

#include <PhosphorWayland/IdleNotifier.h>

namespace PhosphorServiceIdle {

IdleService::IdleService(QObject* parent)
    : QObject(parent)
{
}

bool IdleService::isSupported() const
{
    // Idle detection is the core of the service; the compositor must advertise
    // ext-idle-notify-v1 (surfaced by PhosphorWayland::IdleNotifier) for the
    // service to do anything. The inhibit path (zwp-idle-inhibit-v1) is queried
    // separately when inhibition lands.
    return PhosphorWayland::IdleNotifier::isSupported();
}

} // namespace PhosphorServiceIdle
