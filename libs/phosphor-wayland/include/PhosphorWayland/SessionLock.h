// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorwayland_export.h>

#include <QObject>

#include <memory>

namespace PhosphorWayland {

/**
 * @brief Client-side wrapper around `ext_session_lock_manager_v1` /
 *        `ext_session_lock_v1`.
 *
 * Requests that the compositor lock the session and observes the outcome: the
 * compositor replies with either `locked()` (this client now owns the locked
 * session and is responsible for authenticating and releasing it) or
 * `finished()` (the request was denied, or an existing lock already owns the
 * session). On a successful authentication the owner calls `unlockAndDestroy()`.
 *
 * This is the foundation primitive a lock service composes; it carries no
 * authentication, no UI, and (deliberately) no lock *surfaces*. Per the
 * protocol a real lock screen must create an `ext_session_lock_surface_v1` for
 * every output before the compositor presents the locked frame and sends
 * `locked()`; that rendering layer is a shell concern wired in a later phase.
 * Until surfaces are added the compositor decides, per its own policy and time
 * limit, when (or whether) to emit `locked()`.
 *
 * Security guarantee (from the protocol): if the client dies while the session
 * is locked, the compositor must NOT unlock. Accordingly this object never
 * destroys a lock for which `locked()` was received without going through
 * `unlockAndDestroy()`: a bare destroy in that state is a protocol error and
 * would also defeat the guarantee.
 *
 * Construct one per process. Threading: every method MUST be called from the
 * GUI thread.
 */
class PHOSPHORWAYLAND_EXPORT SessionLock : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(SessionLock)
    Q_PROPERTY(bool locked READ isLocked NOTIFY lockedChanged)

public:
    explicit SessionLock(QObject* parent = nullptr);
    ~SessionLock() override;

    /// True iff the compositor advertises `ext_session_lock_manager_v1`. The
    /// constructor still succeeds when unsupported, but `lock()` is a no-op.
    static bool isSupported();

    /// Request that the session be locked. The compositor replies with exactly
    /// one of `locked()` or `finished()`. A no-op if a lock is already in
    /// progress, the session is already locked by this object, or the protocol
    /// is unsupported.
    void lock();

    /// Release the lock after a successful authentication: sends
    /// `unlock_and_destroy` and flushes. Valid only after `locked()` was
    /// emitted; a no-op otherwise. Emits `lockedChanged()` but NOT `finished()`:
    /// `finished()` signals a compositor-driven end of the lock, whereas this is
    /// the client-driven unlock, so a consumer must model unlock success off this
    /// call (or `lockedChanged()`), not off `finished()`.
    void unlockAndDestroy();

    /// True between `locked()` and `unlockAndDestroy()` (or a compositor-driven
    /// `finished()`).
    [[nodiscard]] bool isLocked() const;

Q_SIGNALS:
    /// The session is now locked; this client owns the lock and must call
    /// `unlockAndDestroy()` to release it.
    void locked();

    /// The compositor will not (or will no longer) lock the session: the lock
    /// object has been torn down and the request is over. Emitted at most once
    /// per `lock()`.
    void finished();

    /// `isLocked()` changed.
    void lockedChanged();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorWayland
