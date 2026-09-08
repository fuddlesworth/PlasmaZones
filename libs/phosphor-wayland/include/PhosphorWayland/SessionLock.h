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
 * authentication and no UI. Lock *surfaces* (one `ext_session_lock_surface_v1`
 * per output, presented by the compositor as the locked frame) are QWindows
 * marked through `LockSurface::get()`; the QPA plugin creates them against the
 * lock object this class holds, so they can only exist between `lock()` and
 * the lock's release (`canCreateSurfaces()`). Create them as soon as `lock()`
 * has been called, NOT after `locked()`: a compositor is free to withhold
 * `locked()` until every output carries a lock surface, and waiting for the
 * grant before creating them would then deadlock. `canCreateSurfaces()` is
 * true for exactly the window in which they may be created, which is why it
 * is not the same predicate as `isLocked()`.
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
    /// constructor still succeeds when unsupported; `lock()` then reports
    /// `finished()` rather than locking.
    static bool isSupported();

    /// Request that the session be locked. Answers with exactly one of
    /// `locked()` or `finished()` — including when the request never reaches
    /// the compositor (protocol unsupported, no integration, or the lock
    /// object could not be created), in which case `finished()` is emitted
    /// asynchronously. Callers set their own state before calling and have no
    /// other exit, so a silent return would strand them for good.
    ///
    /// The one case that answers with neither is a redundant call: a lock
    /// already in flight or already held by this object is ignored, because
    /// the reply for the outstanding request is still owed.
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

    /// True while a lock object exists (requested or granted), which is when a
    /// `LockSurface`-marked window can be mapped as a lock surface. Reads the
    /// process-wide state: at most one SessionLock holds a lock at a time.
    static bool canCreateSurfaces();

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
