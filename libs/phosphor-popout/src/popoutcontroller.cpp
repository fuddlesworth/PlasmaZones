// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorPopout/PopoutController.h>

#include <PhosphorPopout/IPopoutTransport.h>

#include <QHash>
#include <QString>

namespace PhosphorPopout {

namespace {

// One row per open popout. Indexed in PopoutController::Impl by handle.
struct Entry
{
    QString popoutId;
    QString scope;
    ExclusiveMode exclusive;
};

} // namespace

struct PopoutController::Impl
{
    Impl(PopoutController* owner, IPopoutTransport* t)
        : self(owner)
        , transport(t)
    {
    }

    PopoutController* self;
    IPopoutTransport* transport;

    // Active popouts, keyed by transport handle. The handle is the
    // identity for close routing. popoutId is the user-facing logical
    // name. The same popoutId can recur across reopens of the same
    // popout.
    QHash<QString, Entry> entries;

    // Modal count. Modals stack, so a u16-ish counter is fine. The
    // public isModalActive returns count > 0.
    int modalCount = 0;

    // Re-entrancy guard. Set while the controller is mid-mutation of
    // its own tables. The dismissed callback short-circuits when set
    // so iterations are not invalidated by callback-driven mutations.
    // Both closeAll and open set this around their suppression closes.
    bool inSelfTeardown = false;

    [[nodiscard]] bool isModalActive() const
    {
        return modalCount > 0;
    }

    // Find the handle currently used by a popoutId, or empty string.
    [[nodiscard]] QString handleForId(const QString& popoutId) const
    {
        for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
            if (it.value().popoutId == popoutId) {
                return it.key();
            }
        }
        return {};
    }

    // Remove an entry, tell the transport, fire the closed signal, and
    // update modal bookkeeping. Single source of truth for the three
    // teardown call sites. emitClosed allows closeAll to defer signal
    // emission to a final pass if it ever needs to.
    void removeEntry(const QString& handle)
    {
        const auto it = entries.constFind(handle);
        if (it == entries.constEnd()) {
            return;
        }
        const Entry copy = it.value();

        // Drop from our table BEFORE telling the transport so any
        // queued dismissed callback for this handle finds no entry
        // and no-ops. Matches the close() pattern documented at
        // IPopoutTransport.h.
        entries.erase(it);

        const bool wasModal = (copy.exclusive == ExclusiveMode::Modal);
        if (wasModal) {
            Q_ASSERT(modalCount > 0);
            --modalCount;
        }

        transport->closeSurface(handle);
        Q_EMIT self->popoutClosed(copy.popoutId, handle);
        if (wasModal && modalCount == 0) {
            Q_EMIT self->modalActiveChanged();
        }
    }

    // Close every Cooperative entry. Used when the first Modal opens to
    // suppress prior Cooperative popouts across all scopes. Snapshot
    // the handles before iterating so removeEntry's mutation of
    // entries does not invalidate the loop.
    void closeAllCooperatives()
    {
        QStringList victims;
        for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
            if (it.value().exclusive == ExclusiveMode::Cooperative) {
                victims.append(it.key());
            }
        }
        for (const QString& handle : victims) {
            removeEntry(handle);
        }
    }

    // Close the existing Cooperative entry in `scope`, if any. Used
    // when a new Cooperative request is accepted: the prior one in the
    // same scope must vacate before the new one opens.
    void closeCooperativeInScope(const QString& scope)
    {
        QString victim;
        for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
            if (it.value().exclusive == ExclusiveMode::Cooperative && it.value().scope == scope) {
                victim = it.key();
                break;
            }
        }
        if (!victim.isEmpty()) {
            removeEntry(victim);
        }
    }
};

PopoutController::PopoutController(IPopoutTransport* transport, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Impl>(this, transport))
{
    Q_ASSERT_X(transport != nullptr, "PopoutController", "transport must not be null");

    // Install the dismissed callback exactly once. The transport
    // invokes it when a surface goes away on its own. We use it to
    // mirror the change in our entries table. Without this, isOpen
    // would lie about a popout that the compositor already tore down.
    //
    // The lambda captures `this` and `d.get()`. The destructor must
    // detach this callback before d is destroyed. See ~PopoutController.
    transport->setSurfaceDismissedCallback([this](const QString& handle) {
        if (d->inSelfTeardown) {
            return;
        }
        const auto it = d->entries.constFind(handle);
        if (it == d->entries.constEnd()) {
            return;
        }
        const Entry copy = it.value();
        d->entries.erase(it);
        const bool wasModal = (copy.exclusive == ExclusiveMode::Modal);
        if (wasModal) {
            Q_ASSERT(d->modalCount > 0);
            --d->modalCount;
        }
        Q_EMIT popoutClosed(copy.popoutId, handle);
        if (wasModal && d->modalCount == 0) {
            Q_EMIT modalActiveChanged();
        }
    });
}

PopoutController::~PopoutController()
{
    // Detach the dismissed callback BEFORE d is destroyed. The
    // transport may outlive the controller if the owner releases them
    // in the wrong order. Without this, any subsequent dismiss the
    // transport routes through the stale lambda dereferences a dead
    // d-pointer and crashes.
    if (d && d->transport) {
        d->transport->setSurfaceDismissedCallback({});
    }
}

QString PopoutController::open(const PopoutRequest& request)
{
    // toggle re-enters here when the popoutId is not yet open. If a
    // popoutId is already open, treat this as a no-op rather than
    // opening a duplicate. Callers that want the new instance must
    // close the old handle first. Applies to every ExclusiveMode,
    // including Modal: a second Modal with the same popoutId is
    // rejected like any other same-id collision.
    if (!request.popoutId.isEmpty() && !d->handleForId(request.popoutId).isEmpty()) {
        return {};
    }

    // Set the re-entrancy guard for the arbitration phase below.
    // closeAllCooperatives and closeCooperativeInScope iterate
    // entries while removeEntry mutates the same table. A transport
    // that violates the IPopoutTransport contract by firing the
    // dismissed callback synchronously from closeSurface would
    // otherwise re-enter this function and double-emit popoutClosed
    // for an already-removed handle.
    d->inSelfTeardown = true;

    // Arbitration. Detached requests skip the scope-and-modal checks
    // entirely. Modals close existing cooperatives. Cooperatives are
    // rejected while a modal is up and swap any same-scope sibling.
    switch (request.exclusive) {
    case ExclusiveMode::Detached:
        break;
    case ExclusiveMode::Modal:
        d->closeAllCooperatives();
        break;
    case ExclusiveMode::Cooperative:
        if (d->isModalActive()) {
            d->inSelfTeardown = false;
            return {};
        }
        d->closeCooperativeInScope(request.scope);
        break;
    }

    d->inSelfTeardown = false;

    const QString handle = d->transport->openSurface(request);
    if (handle.isEmpty()) {
        // Transport refused. Any prior cooperative in this scope was
        // already closed above. That is intentional. Restoring the
        // prior popout on transport failure would put us in an
        // inconsistent state.
        return {};
    }

    d->entries.insert(handle, Entry{request.popoutId, request.scope, request.exclusive});
    if (request.exclusive == ExclusiveMode::Modal) {
        const bool wasModalActive = d->modalCount > 0;
        ++d->modalCount;
        if (!wasModalActive) {
            Q_EMIT modalActiveChanged();
        }
    }
    Q_EMIT popoutOpened(request.popoutId, handle);
    return handle;
}

void PopoutController::close(const QString& handle)
{
    if (handle.isEmpty()) {
        return;
    }
    d->removeEntry(handle);
}

QString PopoutController::toggle(const PopoutRequest& request)
{
    if (request.popoutId.isEmpty()) {
        // Toggle without an id has no fixed referent. Treat as open.
        return open(request);
    }
    const QString existing = d->handleForId(request.popoutId);
    if (!existing.isEmpty()) {
        close(existing);
        return {};
    }
    return open(request);
}

bool PopoutController::isOpen(const QString& popoutId) const
{
    return !d->handleForId(popoutId).isEmpty();
}

void PopoutController::closeAll()
{
    d->inSelfTeardown = true;
    const auto handles = d->entries.keys();
    for (const QString& handle : handles) {
        const auto it = d->entries.constFind(handle);
        if (it == d->entries.constEnd()) {
            continue;
        }
        const Entry copy = it.value();
        d->entries.erase(it);
        d->transport->closeSurface(handle);
        Q_EMIT popoutClosed(copy.popoutId, handle);
    }
    const bool hadModal = d->modalCount > 0;
    d->modalCount = 0;
    d->inSelfTeardown = false;
    if (hadModal) {
        Q_EMIT modalActiveChanged();
    }
}

QString PopoutController::handleFor(const QString& popoutId) const
{
    return d->handleForId(popoutId);
}

bool PopoutController::isModalActive() const
{
    return d->isModalActive();
}

} // namespace PhosphorPopout
