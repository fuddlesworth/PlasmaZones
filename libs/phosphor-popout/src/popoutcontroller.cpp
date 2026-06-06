// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorPopout/PopoutController.h>

#include <PhosphorPopout/IPopoutTransport.h>

#include <QHash>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace PhosphorPopout {

namespace {

// One row per open popout. Indexed in PopoutController::Impl by handle.
struct Entry
{
    QString popoutId;
    QString scope;
    ExclusiveMode exclusive;
};

// Save-and-restore RAII guard for the bool re-entrancy flag. A blind
// set-to-true / set-to-false pair is the obvious shape and breaks
// under re-entrancy. The inner call's restore clears the outer
// caller's guard while the outer is still mid-mutation, opening the
// very double-emit window the guard exists to close. The RAII form
// stores the prior value and restores it, so nested teardown
// windows nest instead of clobbering each other.
class ScopedTrue
{
public:
    explicit ScopedTrue(bool& flag)
        : m_flag(flag)
        , m_prev(flag)
    {
        m_flag = true;
    }
    ~ScopedTrue()
    {
        m_flag = m_prev;
    }

    ScopedTrue(const ScopedTrue&) = delete;
    ScopedTrue& operator=(const ScopedTrue&) = delete;

private:
    bool& m_flag;
    bool m_prev;
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

    // Drop an entry from our tables and fire the closed signals
    // without telling the transport. Used by the dismissed callback
    // where the transport already knows the surface is gone. The
    // modal-count invariant check and the signal sequence match
    // removeEntry.
    void removeEntryQuiet(const QString& handle)
    {
        const auto it = entries.constFind(handle);
        if (it == entries.constEnd()) {
            return;
        }
        const Entry copy = it.value();
        entries.erase(it);

        const bool wasModal = (copy.exclusive == ExclusiveMode::Modal);
        if (wasModal) {
            Q_ASSERT(modalCount > 0);
            --modalCount;
        }

        // modalActiveChanged fires before popoutClosed to mirror the
        // open()-side ordering (state transition signal first, logical
        // event signal second). A popoutClosed slot inspecting
        // isModalActive() then sees the post-transition state with the
        // notify having already fired.
        if (wasModal && modalCount == 0) {
            Q_EMIT self->modalActiveChanged();
        }
        Q_EMIT self->popoutClosed(copy.popoutId, handle);
    }

    // Caller-initiated teardown. Single source of truth for every
    // close call site. close, closeCooperativeInScope,
    // closeAllCooperatives, and closeAll all route through here so
    // the remove-then-close ordering, the modal-count invariant, and
    // the signal sequence stay consistent.
    void removeEntry(const QString& handle)
    {
        const auto it = entries.constFind(handle);
        if (it == entries.constEnd()) {
            return;
        }
        const Entry copy = it.value();

        // Drop from our table BEFORE telling the transport so any
        // queued dismissed callback for this handle finds no entry
        // and no-ops. Matches the contract documented at
        // IPopoutTransport.h.
        entries.erase(it);

        const bool wasModal = (copy.exclusive == ExclusiveMode::Modal);
        if (wasModal) {
            Q_ASSERT(modalCount > 0);
            --modalCount;
        }

        transport->closeSurface(handle);
        if (wasModal && modalCount == 0) {
            Q_EMIT self->modalActiveChanged();
        }
        Q_EMIT self->popoutClosed(copy.popoutId, handle);
    }

    // Close every Cooperative entry. Used when the first Modal opens
    // to suppress prior Cooperative popouts across all scopes.
    // Snapshot the handles before iterating so removeEntry's mutation
    // of entries does not invalidate the loop.
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
    // when a new Cooperative request is accepted. The prior one in
    // the same scope must vacate before the new one opens.
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
    // qFatal in BOTH debug and release. Q_ASSERT_X is a no-op in
    // release builds, which would let the next line dereference null
    // and crash with no useful diagnostic. The constructor's
    // documented precondition is non-null transport; honouring it
    // with a clear abort is friendlier than a SIGSEGV.
    if (transport == nullptr) {
        qFatal("PopoutController: transport must not be null");
    }

    // Install the dismissed callback exactly once. The transport
    // invokes it when a surface goes away on its own. We use it to
    // mirror the change in our entries table. Without this, isOpen
    // would lie about a popout that the compositor already tore down.
    //
    // The lambda captures `this`; `d` is reached via the member.
    // The destructor must detach this callback before d is destroyed.
    // See ~PopoutController.
    transport->setSurfaceDismissedCallback([this](const QString& handle) {
        if (d->inSelfTeardown) {
            return;
        }
        // Route through removeEntryQuiet so the transport is not
        // re-notified. The surface is already gone. The signal
        // sequence and modal-count invariant match the
        // caller-initiated removeEntry path.
        d->removeEntryQuiet(handle);
    });
}

PopoutController::~PopoutController()
{
    // Detach the dismissed callback BEFORE d is destroyed. The
    // transport may outlive the controller if the owner releases them
    // in the wrong order. Without this, any subsequent dismiss the
    // transport routes through the stale lambda dereferences a dead
    // d-pointer and crashes.
    //
    // d is non-null here. make_unique in the ctor either succeeded or
    // threw, in which case the destructor never runs. transport is
    // non-null because the ctor qFatal-aborts on a null transport.
    d->transport->setSurfaceDismissedCallback({});
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

    // Arbitration. Detached requests skip the scope-and-modal checks
    // entirely. Modals close existing cooperatives. Cooperatives are
    // rejected while a modal is up and swap any same-scope sibling.
    // The re-entrancy guard wraps each branch that actually mutates
    // entries. Branches that don't mutate skip the guard so a
    // same-thread re-entrant slot can't observe an unnecessary
    // self-teardown state. Those are the Detached and the rejected
    // Cooperative branches.
    switch (request.exclusive) {
    case ExclusiveMode::Detached:
        break;
    case ExclusiveMode::Modal: {
        ScopedTrue guard(d->inSelfTeardown);
        d->closeAllCooperatives();
        break;
    }
    case ExclusiveMode::Cooperative:
        if (d->isModalActive()) {
            return {};
        }
        {
            ScopedTrue guard(d->inSelfTeardown);
            d->closeCooperativeInScope(request.scope);
        }
        break;
    }

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
    // Route every teardown through the same removeEntry helper that
    // close, closeCooperativeInScope, and closeAllCooperatives use.
    // removeEntry decrements modalCount per entry and fires
    // modalActiveChanged when the count reaches zero, so the per-row
    // invariant (Q_ASSERT(modalCount > 0) before each decrement) is
    // preserved during the drain.
    //
    // Snapshot the keys BEFORE iterating. removeEntry mutates entries,
    // and iterating the live container would invalidate the iterator
    // mid-loop. Same pattern as closeAllCooperatives.
    ScopedTrue guard(d->inSelfTeardown);
    const auto handles = d->entries.keys();
    for (const QString& handle : handles) {
        d->removeEntry(handle);
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
