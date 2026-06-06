// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorWayland/SessionLock.h>

#include "qpa/layershellintegration.h"
#include "qpa/session_lock_protocol.h"

#include <QLoggingCategory>

#include <QtWaylandClient/private/qwaylanddisplay_p.h>

Q_LOGGING_CATEGORY(lcSessionLock, "phosphorwayland.sessionlock")

namespace PhosphorWayland {

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

    static void handleLocked(void* data, struct ext_session_lock_v1*)
    {
        auto* self = static_cast<Private*>(data);
        // self is null when the listener was severed at teardown; the isLocked
        // guard drops a spurious repeat (the protocol sends `locked` at most once).
        if (!self || self->isLocked)
            return;
        self->isLocked = true;
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
}

SessionLock::~SessionLock()
{
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
    auto* integration = LayerShellIntegration::instance();
    if (!integration)
        return;
    auto* manager = integration->sessionLockManager();
    if (!manager) {
        qCWarning(lcSessionLock) << "Compositor does not advertise ext_session_lock_manager_v1; cannot lock";
        return;
    }
    d->lockObj = ext_session_lock_manager_v1_lock(manager);
    if (!d->lockObj) {
        qCWarning(lcSessionLock) << "Failed to create the session lock object";
        return;
    }
    static const struct ext_session_lock_v1_listener listener = {
        .locked = Private::handleLocked,
        .finished = Private::handleFinished,
    };
    ext_session_lock_v1_add_listener(d->lockObj, &listener, d.get());
    d->flush();
}

void SessionLock::unlockAndDestroy()
{
    if (!d->lockObj || !d->isLocked)
        return; // unlock_and_destroy is a protocol error before `locked`.
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
