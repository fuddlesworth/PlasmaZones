// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQml
import Phosphor.Service.Lock
import Phosphor.Service.Session

// Composition root for the lock-before-sleep handshake. phosphor-service-lock
// (2.9, authentication + the ext-session-lock-v1 surface) and
// phosphor-service-session (2.10, the logind edge: capabilities, actions,
// inhibitors, PrepareForSleep) stay independent libraries; this is the one
// place that instantiates both and wires them together, so the shell owns the
// integration and neither service depends on the other.
//
// The handshake (see docs/phosphor-shell-design/04-implementation-plan.md
// section 2.10): session holds a logind delay inhibitor on "sleep", so when the
// system is about to sleep logind waits on us. We lock, and once the lock
// surface is up we release the inhibitor and suspend proceeds with the session
// already locked. This closes the race that a plugin shell cannot: an
// externally-initiated suspend (lid, `systemctl suspend`, idle) locks first.
QtObject {
    id: coordinator

    property SessionHost session: SessionHost {}
    property LockService lock: LockService {}

    property Connections sessionConnections: Connections {
        target: coordinator.session

        // A lock was requested through logind (a hardware lock key, `loginctl
        // lock-session`, or our own session.lock()): raise the lock surface.
        function onLockRequested(): void {
            coordinator.lock.lock();
        }

        // The system is about to sleep and logind is holding on our delay
        // inhibitor: lock now, before the machine suspends.
        function onAboutToSleep(): void {
            // If the session is already locked (a manual lock, or a prior suspend
            // that left it locked), the lock surface is already up: release the
            // inhibitor immediately so suspend proceeds. Relying on a lock state
            // transition here would never fire (lock() is a no-op when already
            // locked) and would stall suspend until the library's safety timeout.
            if (coordinator.lock.locked) {
                coordinator.session.allowSleep();
                return;
            }
            coordinator.lock.lock();
        }
    }

    property Connections lockConnections: Connections {
        target: coordinator.lock

        // Once the lock surface is actually up, drop the delay inhibitor so a
        // pending suspend proceeds with the session already locked. Harmless
        // when no suspend is pending (allowSleep just clears an already-released
        // inhibitor).
        function onLockedChanged(): void {
            if (coordinator.lock.locked)
                coordinator.session.allowSleep();
        }
    }
}
