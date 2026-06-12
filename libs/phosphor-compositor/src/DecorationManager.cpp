// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorCompositor/DecorationManager.h"

#include <QLoggingCategory>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace PhosphorCompositor {

Q_LOGGING_CATEGORY(lcDecoration, "phosphor.compositor.decoration", QtWarningMsg)

namespace {
/// Bulk restores must complete even if the expected drain trigger (the
/// resnap geometry batch) never arrives — e.g. a mode toggle with no
/// resnappable windows.
constexpr int FallbackDrainMs = 500;
} // namespace

DecorationManager::DecorationManager(ICompositorBridge& bridge, QObject* parent)
    : QObject(parent)
    , m_bridge(bridge)
{
}

DecorationManager::~DecorationManager() = default;

// ═══════════════════════════════════════════════════════════════════════════════
// Ownership
// ═══════════════════════════════════════════════════════════════════════════════

void DecorationManager::acquire(const QString& windowId, const Owner& owner, Placement placement)
{
    if (windowId.isEmpty()) {
        return;
    }
    Entry& entry = m_windows[windowId];
    // Re-acquire cancels a queued deferred restore: the new claim is
    // authoritative and the decoration must stay hidden. This is the
    // unified form of the drain-time "screen re-entered autotile" re-check.
    if (entry.pendingRestore) {
        entry.pendingRestore = false;
        m_pendingRestore.remove(windowId);
    }
    if (entry.owners.isEmpty() && !entry.physicallyHidden) {
        // Re-acquired from an ownerless, un-hidden state (veto-only or
        // intent-only entry): the capability/prior-state snapshot may be
        // stale — KWin rules can change, the user can toggle their own
        // noBorder while we hold no claim — so refresh it for this
        // ownership epoch. While continuously owned (or still physically
        // hidden awaiting a deferred restore) the captured priorNoBorder
        // must persist, so the latch holds in those states.
        entry.evaluated = false;
    }
    if (!entry.owners.contains(owner)) {
        entry.owners.append(owner);
    }
    reconcile(windowId, entry, placement);
}

void DecorationManager::release(const QString& windowId, const Owner& owner, Restore restore)
{
    auto it = m_windows.find(windowId);
    if (it == m_windows.end()) {
        return;
    }
    if (it->owners.removeAll(owner) == 0) {
        return;
    }
    finishRelease(windowId, *it, restore);
}

void DecorationManager::releaseKind(const QString& windowId, OwnerKind kind, Restore restore)
{
    auto it = m_windows.find(windowId);
    if (it == m_windows.end()) {
        return;
    }
    const auto removed = it->owners.removeIf([kind](const Owner& o) {
        return o.kind == kind;
    });
    if (removed == 0) {
        return;
    }
    finishRelease(windowId, *it, restore);
}

void DecorationManager::releaseOthersOfKind(const QString& windowId, OwnerKind kind, const QString& keepScreenId)
{
    auto it = m_windows.find(windowId);
    if (it == m_windows.end()) {
        return;
    }
    // Pure owner-set surgery for cross-screen transfers: never a physical
    // toggle, even if the kept screen's owner is not registered yet (the
    // caller acquires it next). The decoration staying hidden across the
    // hop is exactly the point.
    it->owners.removeIf([kind, &keepScreenId](const Owner& o) {
        return o.kind == kind && o.screenId != keepScreenId;
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// Bulk operations
// ═══════════════════════════════════════════════════════════════════════════════

void DecorationManager::releaseAllOfKind(OwnerKind kind, Restore restore)
{
    const QStringList ids = m_windows.keys();
    for (const QString& windowId : ids) {
        releaseKind(windowId, kind, restore);
    }
}

void DecorationManager::restoreAll()
{
    if (m_pendingFallback) {
        m_pendingFallback->stop();
        m_pendingFallback->deleteLater();
        m_pendingFallback = nullptr;
    }
    m_pendingRestore.clear();
    // Snapshot the ids needing a physical restore and clear ALL tracking
    // before touching the compositor: setNoBorder and the restored signal
    // can synchronously re-enter the manager, and emitting while iterating
    // m_windows would run on invalidated iterators.
    QVector<QString> toRestore;
    toRestore.reserve(m_windows.size());
    for (auto it = m_windows.constBegin(); it != m_windows.constEnd(); ++it) {
        if (it->physicallyHidden && !it->priorNoBorder) {
            toRestore.append(it.key());
        }
    }
    m_windows.clear();
    // Teardown restores synchronously: the daemon/effect is going away and
    // there is no later tick to defer to.
    for (const QString& windowId : std::as_const(toRestore)) {
        if (WindowHandle w = m_bridge.findWindowById(windowId)) {
            m_bridge.setNoBorder(w, false);
            Q_EMIT windowDecorationRestored(windowId);
        }
    }
}

void DecorationManager::forgetWindow(const QString& windowId)
{
    m_windows.remove(windowId);
    m_pendingRestore.remove(windowId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window-rule layer
// ═══════════════════════════════════════════════════════════════════════════════

void DecorationManager::setRuleOverride(const QString& windowId, std::optional<bool> ruleValue)
{
    if (windowId.isEmpty()) {
        return;
    }
    if (!ruleValue.has_value()) {
        auto it = m_windows.find(windowId);
        if (it == m_windows.end()) {
            return;
        }
        const bool hadVeto = it->vetoed;
        it->vetoed = false;
        const auto removed = it->owners.removeIf([](const Owner& o) {
            return o.kind == OwnerKind::Rule;
        });
        if (removed > 0) {
            finishRelease(windowId, *it, Restore::Immediate);
        } else if (hadVeto) {
            // Veto lifted with no rule owner: remaining mode owners re-assert.
            reconcile(windowId, *it, Placement::AlreadyPlaced);
            pruneIfEmpty(windowId);
        }
        return;
    }

    Entry& entry = m_windows[windowId];
    if (*ruleValue) {
        entry.vetoed = false;
        if (entry.pendingRestore) {
            entry.pendingRestore = false;
            m_pendingRestore.remove(windowId);
        }
        if (!entry.owners.contains(rule())) {
            entry.owners.append(rule());
        }
        reconcile(windowId, entry, Placement::AlreadyPlaced);
    } else {
        // Force-show: the veto wins over every owner and pins the decoration
        // visible until the rule changes or goes away.
        entry.vetoed = true;
        entry.owners.removeIf([](const Owner& o) {
            return o.kind == OwnerKind::Rule;
        });
        if (entry.pendingRestore) {
            entry.pendingRestore = false;
            m_pendingRestore.remove(windowId);
        }
        reconcile(windowId, entry, Placement::AlreadyPlaced);
        pruneIfEmpty(windowId);
    }
}

void DecorationManager::clearAllRuleOverrides()
{
    const QStringList ids = m_windows.keys();
    for (const QString& windowId : ids) {
        setRuleOverride(windowId, std::nullopt);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Deferred restore drain
// ═══════════════════════════════════════════════════════════════════════════════

void DecorationManager::drainPendingRestores()
{
    // Cancel the fallback FIRST — even when the queue is already empty
    // (acquire/setRuleOverride may have emptied it after the timer was
    // armed), so a stale timer never fires a pointless drain later.
    if (m_pendingFallback) {
        m_pendingFallback->stop();
        m_pendingFallback->deleteLater();
        m_pendingFallback = nullptr;
    }
    if (m_pendingRestore.isEmpty()) {
        return;
    }
    // Snapshot and clear first so re-entrant drains (fallback timer racing a
    // batch-complete callback, or a second mode toggle mid-drain) start a
    // fresh chain instead of mutating the one in flight.
    auto pending = std::make_shared<QList<QString>>(m_pendingRestore.values());
    m_pendingRestore.clear();

    // One decoration toggle per event-loop tick: each restore is a 30–120 ms
    // synchronous Wayland round-trip, and a tight loop drops frames from the
    // snap/OSD animations running concurrently. The shared_ptr<function>
    // self-capture keeps the chain alive across QTimer reschedules.
    auto step = std::make_shared<std::function<void()>>();
    *step = [this, pending, step]() {
        if (pending->isEmpty()) {
            Q_EMIT drainFinished();
            // Break the shared_ptr self-capture cycle — the heap
            // std::function holds a shared_ptr to itself and would leak
            // (with `pending` and the captured context) after every drain.
            // Resetting here is safe: every invocation after the first
            // executes a COPY of *step (QTimer::singleShot stores the copy),
            // and the first invocation can never take this branch because
            // drainPendingRestores early-returns on an empty queue — so the
            // running lambda's own captures outlive this reset.
            *step = nullptr;
            return;
        }
        const QString windowId = pending->takeFirst();
        auto it = m_windows.find(windowId);
        // Re-acquired (acquire cleared the flag) or already forgotten: the
        // queued restore is stale — leave the decoration alone.
        if (it != m_windows.end() && it->pendingRestore) {
            if (m_restoreVeto && m_restoreVeto(windowId)) {
                // Authoritative state says the window should stay hidden
                // (e.g. its screen re-entered autotile mid-drain). Keep the
                // restore QUEUED instead of dropping it: the expected
                // re-acquire cancels it, and if that re-acquire never lands
                // (the retile declined the window) the fallback timer
                // retries rather than stranding an ownerless hidden window.
                m_pendingRestore.insert(windowId);
                armFallbackTimer();
            } else {
                it->pendingRestore = false;
                restoreNow(windowId, *it, false);
                pruneIfEmpty(windowId);
            }
        }
        QTimer::singleShot(0, this, *step);
    };
    (*step)();
}

void DecorationManager::setRestoreVeto(std::function<bool(const QString&)> veto)
{
    m_restoreVeto = std::move(veto);
}

// ═══════════════════════════════════════════════════════════════════════════════
// External-reset resync
// ═══════════════════════════════════════════════════════════════════════════════

void DecorationManager::resyncWindow(const QString& windowId)
{
    auto it = m_windows.find(windowId);
    if (it == m_windows.end()) {
        return;
    }
    const bool desired = !it->vetoed && !it->owners.isEmpty() && !it->pendingRestore;
    if (!desired || !it->physicallyHidden) {
        return;
    }
    WindowHandle w = m_bridge.findWindowById(windowId);
    if (!w || m_bridge.isNoBorder(w)) {
        return; // gone, or still suppressed — nothing drifted
    }
    qCDebug(lcDecoration) << "resync: compositor reset noBorder under" << windowId << "— re-hiding";
    hideNow(w, Placement::AlreadyPlaced);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Queries
// ═══════════════════════════════════════════════════════════════════════════════

bool DecorationManager::isBorderless(const QString& windowId) const
{
    auto it = m_windows.constFind(windowId);
    return it != m_windows.constEnd() && it->physicallyHidden;
}

bool DecorationManager::isOwned(const QString& windowId) const
{
    auto it = m_windows.constFind(windowId);
    return it != m_windows.constEnd() && !it->owners.isEmpty();
}

bool DecorationManager::hasOwnerOfKind(const QString& windowId, OwnerKind kind) const
{
    auto it = m_windows.constFind(windowId);
    if (it == m_windows.constEnd()) {
        return false;
    }
    return std::any_of(it->owners.cbegin(), it->owners.cend(), [kind](const Owner& o) {
        return o.kind == kind;
    });
}

bool DecorationManager::isOwnedBy(const QString& windowId, const Owner& owner) const
{
    auto it = m_windows.constFind(windowId);
    return it != m_windows.constEnd() && it->owners.contains(owner);
}

bool DecorationManager::isVetoed(const QString& windowId) const
{
    auto it = m_windows.constFind(windowId);
    return it != m_windows.constEnd() && it->vetoed;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Internals
// ═══════════════════════════════════════════════════════════════════════════════

void DecorationManager::reconcile(const QString& windowId, Entry& entry, Placement placement)
{
    const bool desired = !entry.vetoed && !entry.owners.isEmpty();

    if (desired && !entry.physicallyHidden) {
        WindowHandle w = m_bridge.findWindowById(windowId);
        if (!w) {
            // Intent recorded; the window is gone or not yet resolvable.
            // A later acquire retries while ownership persists (resyncWindow
            // deliberately does not — it only re-asserts hides that already
            // happened physically).
            return;
        }
        if (!entry.evaluated) {
            entry.evaluated = true;
            entry.eligible = m_bridge.userCanSetNoBorder(w);
            entry.priorNoBorder = m_bridge.isNoBorder(w);
        }
        if (!entry.eligible) {
            // CSD (GTK/Electron) / override-redirect / rule-forced
            // decoration: tracked as owned, never physically touched.
            return;
        }
        if (entry.priorNoBorder) {
            // Already borderless before we ever touched it (user's own
            // compositor rule, prior session): nothing to hide, and restore
            // must return to borderless — i.e. also nothing.
            return;
        }
        // Mark state BEFORE the physical toggle: the bridge calls inside
        // hideNow can synchronously re-enter the manager (KWin emits
        // geometry/decoration signals whose handlers acquire/release), and
        // a re-entrant insert may rehash m_windows and invalidate `entry`.
        // Nothing may touch `entry` after hideNow.
        entry.physicallyHidden = true;
        hideNow(w, placement);
        qCDebug(lcDecoration) << "hid decoration for" << windowId;
        return;
    }

    if (!desired && entry.physicallyHidden) {
        // Veto flipped (or owners vanished through a non-release path):
        // restore now. Re-assert geometry while mode owners remain — the
        // window is zone-placed and the frame change would leave it
        // overflowing its zone.
        restoreNow(windowId, entry, !entry.owners.isEmpty());
    }
}

void DecorationManager::finishRelease(const QString& windowId, Entry& entry, Restore restore)
{
    if (!entry.owners.isEmpty() || entry.vetoed) {
        pruneIfEmpty(windowId);
        return;
    }
    if (!entry.physicallyHidden) {
        pruneIfEmpty(windowId);
        return;
    }
    if (restore == Restore::Immediate) {
        restoreNow(windowId, entry, false);
        pruneIfEmpty(windowId);
        return;
    }
    entry.pendingRestore = true;
    m_pendingRestore.insert(windowId);
    armFallbackTimer();
}

void DecorationManager::hideNow(WindowHandle w, Placement placement)
{
    if (placement == Placement::AlreadyPlaced) {
        // Capture the target BEFORE the decoration toggle. The compositor
        // holds the CLIENT size constant across the change, so the frame
        // shrinks by the title-bar height; re-asserting the target grows the
        // content to fill the zone. moveResizeGeometry() — not
        // frameGeometry(), which lags on Wayland until the configure ack —
        // is the rect the compositor is already moving toward.
        const QRectF target = m_bridge.moveResizeGeometry(w);
        m_bridge.setNoBorder(w, true);
        if (target.isValid() && !target.isEmpty()) {
            m_bridge.moveResize(w, target);
        } else {
            // No valid target to re-assert: the frame will shrink by the
            // title-bar height until the next placement. Loud because a
            // degenerate move-resize geometry on a placed window is
            // unexpected — surface it rather than under-fill silently.
            qCWarning(lcDecoration) << "hide: no valid move-resize target to re-assert for" << m_bridge.windowId(w);
        }
    } else {
        // CallerWillPlace: the caller applies the zone geometry immediately
        // after; toggling first means that placement already sees the final
        // frame/client relationship.
        m_bridge.setNoBorder(w, true);
    }
}

void DecorationManager::restoreNow(const QString& windowId, Entry& entry, bool reassertGeometry)
{
    entry.physicallyHidden = false;
    if (entry.priorNoBorder) {
        return; // defensive: we never hid such a window physically
    }
    WindowHandle w = m_bridge.findWindowById(windowId);
    if (!w) {
        return; // window gone — decoration died with it
    }
    if (reassertGeometry) {
        const QRectF target = m_bridge.moveResizeGeometry(w);
        m_bridge.setNoBorder(w, false);
        if (target.isValid() && !target.isEmpty()) {
            m_bridge.moveResize(w, target);
        }
    } else {
        m_bridge.setNoBorder(w, false);
    }
    qCDebug(lcDecoration) << "restored decoration for" << windowId;
    Q_EMIT windowDecorationRestored(windowId);
}

void DecorationManager::pruneIfEmpty(const QString& windowId)
{
    auto it = m_windows.find(windowId);
    if (it == m_windows.end()) {
        return;
    }
    if (it->owners.isEmpty() && !it->vetoed && !it->physicallyHidden && !it->pendingRestore) {
        m_windows.erase(it);
    }
}

void DecorationManager::armFallbackTimer()
{
    if (!m_pendingFallback) {
        m_pendingFallback = new QTimer(this);
        m_pendingFallback->setSingleShot(true);
        m_pendingFallback->setInterval(FallbackDrainMs);
        connect(m_pendingFallback, &QTimer::timeout, this, &DecorationManager::drainPendingRestores);
    }
    m_pendingFallback->start();
}

} // namespace PhosphorCompositor
