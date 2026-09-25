// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal (not installed) seam for the compositor session lock the state
// machine drives. Production wraps PhosphorWayland::SessionLock
// (WaylandSessionLock); tests substitute a fake that drives the locked /
// finished edges directly. Keeping the state machine behind this interface is
// what lets the lock policy be unit-tested without a live compositor.

#include <QObject>

namespace PhosphorServiceLock {

/// The session-lock surface the state machine drives: request a lock, observe
/// whether the compositor granted (`locked`) or refused / ended it (`finished`),
/// and release it after a successful authentication.
class ISessionLock : public QObject
{
    Q_OBJECT

public:
    explicit ISessionLock(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    // Out-of-line (defined in isessionlock.cpp) so it anchors the vtable and
    // gives AUTOMOC a translation unit for the Q_OBJECT metaobject.
    ~ISessionLock() override;

    /// Request that the compositor lock the session. The compositor replies with
    /// exactly one of `locked` or `finished`.
    virtual void lock() = 0;

    /// Release the lock after a successful authentication. Valid only after
    /// `locked`; a no-op otherwise.
    virtual void unlockAndDestroy() = 0;

    /// True between `locked` and release (or a compositor-driven `finished`).
    [[nodiscard]] virtual bool isLocked() const = 0;

Q_SIGNALS:
    /// The compositor granted the lock; the session is now locked.
    void locked();
    /// The compositor refused the lock, or ended an active one. The lock object
    /// is over; a new `lock()` is required to try again.
    void finished();
};

} // namespace PhosphorServiceLock
