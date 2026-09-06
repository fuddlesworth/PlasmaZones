// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWayland/SessionLock.h>

#include "qpa/layershellintegration.h"
#include "qpa/session_lock_protocol.h"

#include <QLoggingCategory>

#include <QtWaylandClient/private/qwaylanddisplay_p.h>

Q_LOGGING_CATEGORY(lcSessionLock, "phosphorwayland.sessionlock")

namespace PhosphorWayland {
namespace {
// The single live instance, enforcing the one-per-process invariant the
// header documents. GUI-thread only, like every method on this class.
SessionLock* s_live = nullptr;
} // namespace

class SessionLock::Private
{
public:
    SessionLock* owner = nullptr;

    // The active lock object, or null when no lock is in flight / locked.
    struct ext_session_lock_v1* lockObj = nullptr;
    // True between the `locked` event and release (unlock_and_destroy or a
    // compositor-driven `finished`). Distinguishes the two teardown paths:
    // a locked object must be released with unlock_and_destroy; an unlocked
    // (in-flight) object with destroy.
    bool isLocked = false;

    void flush()
    {
        auto* integration = LayerShellIntegration::instance();
        if (integration && integration->display())
            integration->display()->flushRequests();
    }

    // Publish (or retract) the lock object for the QPA layer: lock surfaces
    // are created against it in LayerShellIntegration::createShellSurface.
    static void publishLock(struct ext_session_lock_v1* lock)
    {
        if (auto* integration = LayerShellIntegration::instance())
            integration->setActiveSessionLock(lock);
    }

    // A lock() that never reached the compositor still owes the caller the one
    // reply the contract promises. `finished` is the correct verdict: the
    // session will not lock. Queued, so a caller that set its own state before
    // calling lock() is not re-entered from inside the call.
    void reportLockUnavailable()
    {
        QMetaObject::invokeMethod(
            owner,
            [owner = this->owner] {
                Q_EMIT owner->finished();
            },
            Qt::QueuedConnection);
    }

    static void handleLocked(void* data, struct ext_session_lock_v1*)
    {
        auto* self = static_cast<Private*>(data);
        // self is null when the listener was severed at teardown; the isLocked
        // guard drops a spurious repeat (the protocol sends `locked` at most once).
        if (!self || self->isLocked)
            return;
        self->isLocked = true;
        if (auto* integration = LayerShellIntegration::instance()) {
            integration->setActiveSessionLockGranted(true);
        }
        Q_EMIT self->owner->lockedChanged();
        Q_EMIT self->owner->locked();
    }

    static void handleFinished(void* data, struct ext_session_lock_v1*)
    {
        auto* self = static_cast<Private*>(data);
        if (!self)
            return;
        // The compositor is done with this lock object; tear it down with the
        // protocol-correct destructor (unlock_and_destroy iff `locked` was
        // sent, destroy otherwise) so the proxy is freed exactly once.
        if (self->lockObj) {
            publishLock(nullptr);
            if (self->isLocked)
                ext_session_lock_v1_unlock_and_destroy(self->lockObj);
            else
                ext_session_lock_v1_destroy(self->lockObj);
            self->lockObj = nullptr;
            self->flush();
        }
        if (self->isLocked) {
            self->isLocked = false;
            Q_EMIT self->owner->lockedChanged();
        }
        Q_EMIT self->owner->finished();
    }
};

SessionLock::SessionLock(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->owner = this;

    // "Construct one per process" is a real invariant, not advice, so say so
    // when it is broken. Two live instances both point the lock's user_data at
    // their own Private, and whichever is destroyed first severs it for the
    // object the other owns, leaving that one deaf to locked / finished. The
    // second instance therefore does not adopt and does not touch user_data.
    // A hot reload is NOT this case: the old instance is destroyed before the
    // new one is built, so s_live is null by then and adoption runs.
    if (s_live) {
        qCWarning(lcSessionLock) << "a SessionLock already exists in this process; this one will not adopt or "
                                    "release the session lock. Construct exactly one.";
    } else {
        s_live = this;
    }

    // Re-adopt a lock this process already holds. The shell builds its
    // LockService from QML, so a hot reload destroys this object and builds a
    // new one — and ~SessionLock deliberately does NOT destroy a live lock,
    // because the protocol requires the session to stay locked when the client
    // goes away. Without adoption the new instance starts at nullptr, nothing
    // can ever call unlock_and_destroy on the old object, and a fresh lock()
    // is answered with `finished` (this client already holds one): the session
    // stays locked at the compositor and the password can no longer release
    // it.
    //
    // Only the listener's user_data was severed on teardown, not the listener
    // itself, so adoption re-points it rather than calling add_listener twice
    // (which wl_proxy refuses).
    if (auto* integration = s_live == this ? LayerShellIntegration::instance() : nullptr) {
        if (auto* existing = integration->activeSessionLock()) {
            d->lockObj = existing;
            d->isLocked = integration->activeSessionLockGranted();
            wl_proxy_set_user_data(reinterpret_cast<struct wl_proxy*>(existing), d.get());
            qCInfo(lcSessionLock) << "adopted the session lock this process already holds; locked =" << d->isLocked;
        }
    }
}

SessionLock::~SessionLock()
{
    const bool wasLive = s_live == this;
    if (wasLive)
        s_live = nullptr;
    // Severing is unconditional: it is what keeps a queued locked/finished
    // event from dispatching into this freed Private, and that hazard does not
    // care whether this was the live instance.
    if (d->lockObj) {
        // Sever the listener's back-pointer so any still-queued locked/finished
        // event dispatches with data == nullptr and is dropped (the Private is
        // being freed). We deliberately do NOT destroy the proxy: if the
        // session is locked, destroying is an invalid_destroy protocol error
        // and would break the protocol's must-stay-locked-if-the-client-dies
        // guarantee; if a lock is still in flight, a destroy could race the
        // locked event into the same error. The proxy is reclaimed when the
        // wl_display tears down.
        wl_proxy_set_user_data(reinterpret_cast<struct wl_proxy*>(d->lockObj), nullptr);
        // The object STAYS published, along with whether it was granted, so a
        // SessionLock built after a hot reload can adopt it (see the
        // constructor). Retracting it here would strand the lock: nothing
        // could then release it and the session would be stuck locked.
        // Only the live instance owns the published state. A second one
        // recording its own grant here would overwrite the real lock's.
        if (auto* integration = wasLive ? LayerShellIntegration::instance() : nullptr) {
            integration->setActiveSessionLockGranted(d->isLocked);
        }
    }
}

bool SessionLock::isSupported()
{
    auto* integration = LayerShellIntegration::instance();
    return integration && integration->sessionLockManager();
}

void SessionLock::lock()
{
    if (d->lockObj)
        return; // a lock is already in flight or held.
    if (s_live != this) {
        // The constructor already warned. Going further would create a second
        // lock object and publish it over the live one, so the surfaces of the
        // real lock would start being parented to this one instead. The caller
        // still gets its one finished() rather than waiting forever.
        qCWarning(lcSessionLock) << "refusing to lock from a second SessionLock instance";
        d->reportLockUnavailable();
        return;
    }
    auto* integration = LayerShellIntegration::instance();
    if (!integration) {
        qCWarning(lcSessionLock) << "No Wayland integration; cannot lock";
        d->reportLockUnavailable();
        return;
    }
    auto* manager = integration->sessionLockManager();
    if (!manager) {
        qCWarning(lcSessionLock) << "Compositor does not advertise ext_session_lock_manager_v1; cannot lock";
        d->reportLockUnavailable();
        return;
    }
    d->lockObj = ext_session_lock_manager_v1_lock(manager);
    if (!d->lockObj) {
        qCWarning(lcSessionLock) << "Failed to create the session lock object";
        d->reportLockUnavailable();
        return;
    }
    static const struct ext_session_lock_v1_listener listener = {
        .locked = Private::handleLocked,
        .finished = Private::handleFinished,
    };
    ext_session_lock_v1_add_listener(d->lockObj, &listener, d.get());
    // Surfaces may be created from here on: the protocol wants one per
    // output before the compositor presents the locked frame, so this must
    // not wait for `locked`.
    Private::publishLock(d->lockObj);
    d->flush();
}

bool SessionLock::canCreateSurfaces()
{
    auto* integration = LayerShellIntegration::instance();
    return integration && integration->activeSessionLock();
}

void SessionLock::unlockAndDestroy()
{
    if (!d->lockObj || !d->isLocked)
        return; // unlock_and_destroy is a protocol error before `locked`.
    Private::publishLock(nullptr);
    ext_session_lock_v1_unlock_and_destroy(d->lockObj);
    d->lockObj = nullptr;
    d->isLocked = false;
    d->flush();
    Q_EMIT lockedChanged();
}

bool SessionLock::isLocked() const
{
    return d->isLocked;
}

} // namespace PhosphorWayland
