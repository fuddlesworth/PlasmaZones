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
    explicit Impl(PopoutController* owner, IPopoutTransport* t)
        : self(owner)
        , transport(t)
    {
    }

    PopoutController* self;
    IPopoutTransport* transport;

    // Active popouts, keyed by transport handle. The handle is the
    // identity for close() routing. popoutId is the user-facing logical
    // name (potentially shared across reopens of the same popout).
    QHash<QString, Entry> entries;

    // Modal count. Modals stack, so a u16-ish counter is fine. The
    // public isModalActive() returns count > 0.
    int modalCount = 0;

    // Re-entrancy guard for the transport's dismissed callback. The
    // callback fires when a surface goes away on its own (focus loss,
    // user click-outside). Without the guard, closeAll() iterating
    // entries.keys() while the callback synchronously mutates entries
    // would invalidate the iteration. Setting closing=true tells the
    // dismissed callback to skip its bookkeeping; closeAll does it
    // itself.
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

    // Close every Cooperative entry. Used when the first Modal opens to
    // suppress prior Cooperative popouts across all scopes.
    void closeAllCooperatives()
    {
        const auto handles = entries.keys();
        for (const QString& handle : handles) {
            const auto it = entries.constFind(handle);
            if (it == entries.constEnd()) {
                continue;
            }
            if (it.value().exclusive != ExclusiveMode::Cooperative) {
                continue;
            }
            const Entry copy = it.value();
            transport->closeSurface(handle);
            entries.remove(handle);
            Q_EMIT self->popoutClosed(copy.popoutId, handle);
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
        if (victim.isEmpty()) {
            return;
        }
        const Entry copy = entries.value(victim);
        transport->closeSurface(victim);
        entries.remove(victim);
        Q_EMIT self->popoutClosed(copy.popoutId, victim);
    }
};

PopoutController::PopoutController(IPopoutTransport* transport, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Impl>(this, transport))
{
    // Install the dismissed callback exactly once. The transport
    // invokes it when a surface goes away on its own. We use it to
    // mirror the change in our entries table; without this, isOpen()
    // would lie about a popout that the compositor already tore down.
    if (transport) {
        transport->setSurfaceDismissedCallback([this](const QString& handle) {
            if (d->inSelfTeardown) {
                return;
            }
            const auto it = d->entries.constFind(handle);
            if (it == d->entries.constEnd()) {
                return;
            }
            const Entry copy = it.value();
            d->entries.remove(handle);
            const bool wasModal = (copy.exclusive == ExclusiveMode::Modal);
            if (wasModal) {
                if (d->modalCount > 0) {
                    --d->modalCount;
                }
            }
            Q_EMIT popoutClosed(copy.popoutId, handle);
            if (wasModal && d->modalCount == 0) {
                Q_EMIT modalActiveChanged();
            }
        });
    }
}

PopoutController::~PopoutController() = default;

QString PopoutController::open(const PopoutRequest& request)
{
    if (!d->transport) {
        return {};
    }

    // toggle() routes here when re-opening, so the popoutId-collision
    // case is the caller's concern (they should call toggle() to swap
    // an existing popout). If a popoutId is already open, treat this
    // as a no-op rather than opening a duplicate. Callers that want
    // the new instance must close() the old handle first.
    if (!request.popoutId.isEmpty() && !d->handleForId(request.popoutId).isEmpty()) {
        return {};
    }

    // Arbitration. Detached requests skip the scope-and-modal checks
    // entirely; modals close existing cooperatives; cooperatives are
    // rejected while a modal is up and swap any same-scope sibling.
    switch (request.exclusive) {
    case ExclusiveMode::Detached:
        break;
    case ExclusiveMode::Modal:
        d->closeAllCooperatives();
        break;
    case ExclusiveMode::Cooperative:
        if (d->isModalActive()) {
            return {};
        }
        d->closeCooperativeInScope(request.scope);
        break;
    }

    const QString handle = d->transport->openSurface(request);
    if (handle.isEmpty()) {
        // Transport refused. We already cleaned up any prior
        // cooperative in this scope above; that is intentional.
        // Rejecting the new open does not restore the old (the
        // previous popout was the user's prior intent; the transport
        // failure on the new request doesn't put us in any consistent
        // "go back" state).
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
    const auto it = d->entries.constFind(handle);
    if (it == d->entries.constEnd()) {
        return;
    }
    const Entry copy = it.value();
    // Remove from our table BEFORE telling the transport. If the
    // transport synchronously fires the dismissed callback inside
    // closeSurface (some implementations may), the callback will
    // find no entry and no-op, which is exactly what we want here:
    // the caller initiated the close, so we don't need the callback's
    // mirror update.
    d->entries.remove(handle);
    const bool wasModal = (copy.exclusive == ExclusiveMode::Modal);
    if (wasModal && d->modalCount > 0) {
        --d->modalCount;
    }
    d->transport->closeSurface(handle);
    Q_EMIT popoutClosed(copy.popoutId, handle);
    if (wasModal && d->modalCount == 0) {
        Q_EMIT modalActiveChanged();
    }
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
    if (!d->transport) {
        d->entries.clear();
        return;
    }
    d->inSelfTeardown = true;
    const bool hadModal = d->isModalActive();
    const auto handles = d->entries.keys();
    for (const QString& handle : handles) {
        const auto it = d->entries.constFind(handle);
        if (it == d->entries.constEnd()) {
            continue;
        }
        const Entry copy = it.value();
        d->entries.remove(handle);
        d->transport->closeSurface(handle);
        Q_EMIT popoutClosed(copy.popoutId, handle);
    }
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
