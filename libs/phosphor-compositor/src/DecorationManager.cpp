// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorCompositor/DecorationManager.h"

#include <QLoggingCategory>
#include <QPointer>

namespace PhosphorCompositor {

Q_LOGGING_CATEGORY(lcDecoration, "phosphor.compositor.decoration", QtWarningMsg)

DecorationManager::DecorationManager(ICompositorBridge& bridge, QObject* parent)
    : QObject(parent)
    , m_bridge(bridge)
{
}

// ═══════════════════════════════════════════════════════════════════════════════
// Ownership
// ═══════════════════════════════════════════════════════════════════════════════

void DecorationManager::acquire(const QString& windowId, const Owner& owner, Placement placement)
{
    if (windowId.isEmpty()) {
        return;
    }
    Entry& entry = m_windows[windowId];
    if (entry.owners.isEmpty() && !entry.physicallyHidden) {
        // Re-acquired from an ownerless, un-hidden state (veto-only or
        // intent-only entry): the capability/prior-state snapshot may be
        // stale — KWin rules can change, the user can toggle their own
        // noBorder while we hold no claim — so refresh it for this
        // ownership epoch. While continuously owned (or still physically
        // hidden) the captured priorNoBorder must persist, so the latch
        // holds in those states.
        entry.evaluated = false;
    }
    if (!entry.owners.contains(owner)) {
        entry.owners.append(owner);
    }
    reconcile(windowId, entry, placement);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Bulk operations
// ═══════════════════════════════════════════════════════════════════════════════

void DecorationManager::restoreAll()
{
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
    QPointer<DecorationManager> self(this);
    for (const QString& windowId : std::as_const(toRestore)) {
        if (auto it = m_windows.find(windowId); it != m_windows.end()) {
            if (!it->owners.isEmpty() && !it->vetoed) {
                // A re-entrant acquire (via the restored-signal handlers of
                // earlier loop iterations) re-claimed this window
                // mid-teardown: the new epoch owns its decoration now —
                // leave it hidden. But TRANSFER the physical hide to the
                // new epoch: the re-entrant acquire evaluated against the
                // still-borderless window and latched OUR OLD hide as
                // priorNoBorder, which would make the new epoch's eventual
                // release a silent no-op (a permanently stranded title bar
                // — restoreNow bails on priorNoBorder and nothing else ever
                // calls setNoBorder). Every id in toRestore was eligible
                // and physically hidden by us at hide time, so the patch
                // values are correct (if eligibility flipped since, the
                // release-time setNoBorder(false) self-guards
                // compositor-side — the safer direction than re-creating
                // the strand).
                it->evaluated = true;
                it->eligible = true;
                it->priorNoBorder = false;
                it->physicallyHidden = true;
                continue;
            }
            // VETOED re-entrant entries (with or without owners) fall
            // through to the physical restore: the force-show invariant —
            // pins the decoration VISIBLE — wins over any owner. Reset the
            // stale snapshot the re-entrant acquire may have latched
            // against our old hide (priorNoBorder=true): once the restore
            // below decorates the window, a post-veto owner re-assert must
            // evaluate FRESH and hide it, not mistake it for
            // user-borderless and no-op (which would also strand the
            // owner's eventual release).
            it->evaluated = false;
        }
        if (WindowHandle w = resolveExact(windowId)) {
            m_bridge.setNoBorder(w, false);
            Q_EMIT windowDecorationRestored(windowId);
            if (!self) {
                return; // a restored-signal slot destroyed the manager
            }
        }
    }
}

void DecorationManager::forgetWindow(const QString& windowId)
{
    m_windows.remove(windowId);
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
            finishRelease(windowId, *it);
        } else if (hadVeto) {
            // Veto lifted with no rule owner: remaining owners re-assert.
            // reconcile here resolves to a hide (owners present) or a
            // no-op (none), so it cannot currently emit windowDecorationRestored
            // — but guard the pruneIfEmpty epilogue against a restored-signal
            // slot destroying the manager anyway, matching the force-show
            // contract so a future change to reconcile's restore branch can't
            // turn this into a UAF.
            QPointer<DecorationManager> self(this);
            reconcile(windowId, *it, Placement::AlreadyPlaced);
            if (!self) {
                return;
            }
            pruneIfEmpty(windowId);
        }
        return;
    }

    Entry& entry = m_windows[windowId];
    if (*ruleValue) {
        entry.vetoed = false;
        // Route the hide through acquire(): it owns the stale-snapshot
        // refresh for a new ownership epoch (an ownerless veto-only entry
        // re-claimed here has the same staleness acquire's epoch refresh
        // exists for — without it a rule hide could later force-decorate a
        // window the user made borderless while we held no claim).
        acquire(windowId, rule(), Placement::AlreadyPlaced);
    } else {
        // Force-show: the veto wins over every owner and pins the decoration
        // visible until the rule changes or goes away.
        entry.vetoed = true;
        entry.owners.removeIf([](const Owner& o) {
            return o.kind == OwnerKind::Rule;
        });
        // reconcile can restore (and emit) here; guard the epilogue against
        // a restored-signal slot destroying the manager — same contract as
        // finishRelease.
        QPointer<DecorationManager> self(this);
        reconcile(windowId, entry, Placement::AlreadyPlaced);
        if (!self) {
            return;
        }
        pruneIfEmpty(windowId);
    }
}

void DecorationManager::clearAllRuleOverrides()
{
    const QStringList ids = m_windows.keys();
    QPointer<DecorationManager> self(this);
    for (const QString& windowId : ids) {
        setRuleOverride(windowId, std::nullopt);
        if (!self) {
            return; // a restored-signal slot destroyed the manager mid-walk
        }
    }
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
    const bool desired = !it->vetoed && !it->owners.isEmpty();
    if (!desired || !it->physicallyHidden) {
        return;
    }
    WindowHandle w = resolveExact(windowId);
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
        WindowHandle w = resolveExact(windowId);
        if (!w) {
            // Intent recorded; the window is gone or not yet resolvable.
            // A later acquire retries while ownership persists (resyncWindow
            // deliberately does not — it only re-asserts hides that already
            // happened physically).
            return;
        }
        if (!entry.evaluated) {
            // Snapshot the const queries into locals BEFORE writing entry
            // fields — same entry-reference discipline as the hideNow rule
            // below. ICompositorBridge's contract requires const queries not
            // to re-enter the manager (see the interface doc), so this is
            // ordering hygiene, not a workaround.
            const bool eligible = m_bridge.userCanSetNoBorder(w);
            const bool priorNoBorder = m_bridge.isNoBorder(w);
            entry.evaluated = true;
            entry.eligible = eligible;
            entry.priorNoBorder = priorNoBorder;
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
        // restore now. Re-assert geometry while owners remain — the window
        // is zone-placed and the frame change would leave it overflowing
        // its zone.
        restoreNow(windowId, entry, !entry.owners.isEmpty());
    }
}

void DecorationManager::finishRelease(const QString& windowId, Entry& entry)
{
    if (!entry.owners.isEmpty() || entry.vetoed) {
        // No prune here: the branch condition (owners remain, or vetoed)
        // contradicts the prune predicate, so it could never fire.
        return;
    }
    if (!entry.physicallyHidden) {
        pruneIfEmpty(windowId);
        return;
    }
    // restoreNow emits; guard the epilogue against a restored-signal slot
    // destroying the manager.
    QPointer<DecorationManager> self(this);
    restoreNow(windowId, entry, false);
    if (!self) {
        return;
    }
    pruneIfEmpty(windowId);
}

WindowHandle DecorationManager::resolveExact(const QString& windowId) const
{
    WindowHandle w = m_bridge.findWindowById(windowId);
    if (!w || m_bridge.windowId(w) != windowId) {
        // Bridge fuzzy fallbacks (appId matching for cross-session restore)
        // can resolve a same-app SIBLING for a dead id — never toggle a
        // window other than the one this entry tracks.
        return nullptr;
    }
    return w;
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
        if (target.isValid()) {
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

bool DecorationManager::restoreNow(const QString& windowId, Entry& entry, bool reassertGeometry)
{
    entry.physicallyHidden = false;
    if (entry.priorNoBorder) {
        return false; // defensive: we never hid such a window physically
    }
    WindowHandle w = resolveExact(windowId);
    if (!w) {
        return false; // window gone — decoration died with it
    }
    if (reassertGeometry) {
        const QRectF target = m_bridge.moveResizeGeometry(w);
        m_bridge.setNoBorder(w, false);
        if (target.isValid()) {
            m_bridge.moveResize(w, target);
        } else {
            // Symmetric with hideNow's AlreadyPlaced warning: a degenerate
            // move-resize geometry on a placed window is unexpected, and
            // without the re-assert the frame overflows the zone by the
            // title-bar height — surface it rather than fail silently.
            qCWarning(lcDecoration) << "restore: no valid move-resize target to re-assert for" << windowId;
        }
    } else {
        m_bridge.setNoBorder(w, false);
    }
    qCDebug(lcDecoration) << "restored decoration for" << windowId;
    Q_EMIT windowDecorationRestored(windowId);
    return true;
}

void DecorationManager::pruneIfEmpty(const QString& windowId)
{
    auto it = m_windows.find(windowId);
    if (it == m_windows.end()) {
        return;
    }
    if (it->owners.isEmpty() && !it->vetoed && !it->physicallyHidden) {
        m_windows.erase(it);
    }
}

} // namespace PhosphorCompositor
