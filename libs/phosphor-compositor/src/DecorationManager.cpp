// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorCompositor/DecorationManager.h"

#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>

#include <memory>

namespace PhosphorCompositor {

Q_LOGGING_CATEGORY(lcDecoration, "phosphor.compositor.decoration", QtWarningMsg)

namespace {
/// Bulk restores must complete even if the expected drain trigger (the
/// resnap geometry batch) never arrives — e.g. a mode toggle with no
/// resnappable windows.
constexpr int FallbackDrainMs = 500;
/// How many consecutive fallback-timer cycles a vetoed restore stays queued
/// before it happens anyway (explicit drains re-queue without counting).
/// The veto predicts "a re-acquire is imminent"; if it is wrong this bounds
/// the strand to ~MaxVetoRetries × the fallback interval.
constexpr int MaxVetoRetries = 6;
} // namespace

DecorationManager::DecorationManager(ICompositorBridge& bridge, QObject* parent)
    : QObject(parent)
    , m_fallbackIntervalMs(FallbackDrainMs)
    , m_bridge(bridge)
{
}

DecorationManager::~DecorationManager()
{
    // Null any in-flight drain chains: their queued QTimer continuations die
    // with this QObject, so the normal end-of-chain cycle-break never runs —
    // without this the heap closures (each holding a shared_ptr to itself)
    // would leak.
    for (const auto& chain : std::as_const(m_liveDrainChains)) {
        *chain = nullptr;
    }
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
    // Re-acquire cancels a queued deferred restore: the new claim is
    // authoritative and the decoration must stay hidden. This is the
    // unified form of the drain-time "screen re-entered autotile" re-check.
    cancelPendingRestore(windowId, entry);
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
    // Prune keeps hidden/vetoed/pending entries, so a mid-transfer window
    // (owner set emptied, decoration still hidden awaiting the follow-up
    // acquire) is unaffected — this only drops an entry that carries no
    // information at all, the one case every other owner-removal path
    // already prunes.
    pruneIfEmpty(windowId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Bulk operations
// ═══════════════════════════════════════════════════════════════════════════════

void DecorationManager::releaseAllOfKind(OwnerKind kind, Restore restore)
{
    const QStringList ids = m_windows.keys();
    QPointer<DecorationManager> self(this);
    for (const QString& windowId : ids) {
        releaseKind(windowId, kind, restore);
        if (!self) {
            return; // a restored-signal slot destroyed the manager mid-walk
        }
    }
}

void DecorationManager::restoreAll()
{
    cancelFallbackTimer();
    m_pendingRestore.clear();
    // Cancel in-flight drain chains: they would only tick over the cleared
    // table doing nothing, and a chain that had already restored something
    // before this teardown would emit a spurious post-teardown
    // drainFinished (a pointless full border rebuild). Same cycle-break as
    // the destructor; a currently-executing step (restoreAll called from a
    // windowDecorationRestored slot mid-drain) detects the null and stops.
    for (const auto& chain : std::as_const(m_liveDrainChains)) {
        *chain = nullptr;
    }
    m_liveDrainChains.clear();
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
            // reconcile here resolves to a hide (mode owners present) or a
            // no-op (none), so it cannot currently emit windowDecorationRestored
            // — but guard the pruneIfEmpty epilogue against a restored-signal
            // slot destroying the manager anyway, matching the force-show and
            // drain-step contract so a future change to reconcile's restore
            // branch can't turn this into a UAF.
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
        // Route the hide through acquire(): it owns the pending-restore
        // cancellation AND the stale-snapshot refresh for a new ownership
        // epoch (an ownerless veto-only entry re-claimed here has the same
        // staleness acquire's epoch refresh exists for — without it a rule
        // hide could later force-decorate a window the user made borderless
        // while we held no claim).
        acquire(windowId, rule(), Placement::AlreadyPlaced);
    } else {
        // Force-show: the veto wins over every owner and pins the decoration
        // visible until the rule changes or goes away.
        entry.vetoed = true;
        entry.owners.removeIf([](const Owner& o) {
            return o.kind == OwnerKind::Rule;
        });
        cancelPendingRestore(windowId, entry);
        // reconcile can restore (and emit) here; guard the epilogue against
        // a restored-signal slot destroying the manager — same contract as
        // the drain step and finishRelease.
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
// Deferred restore drain
// ═══════════════════════════════════════════════════════════════════════════════

void DecorationManager::drainPendingRestores()
{
    drainPendingRestoresInternal(/*fromFallback=*/false);
}

void DecorationManager::drainPendingRestoresInternal(bool fromFallback)
{
    // Cancel the fallback FIRST — even when the queue is already empty
    // (acquire/setRuleOverride may have emptied it after the timer was
    // armed), so a stale timer never fires a pointless drain later.
    cancelFallbackTimer();
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
    auto didWork = std::make_shared<bool>(false);
    m_liveDrainChains.append(step);
    *step = [this, pending, step, didWork, fromFallback]() {
        // Stack copies of EVERY captured value used after a potential
        // re-entrant teardown: the FIRST invocation executes the heap
        // closure itself (not a QTimer-stored copy), and a slot that
        // destroys the manager or calls restoreAll() mid-step nulls *step,
        // destroying the executing closure's captures. The locals pin them
        // to this stack frame — including the `this` pointer value, which
        // is itself a capture.
        DecorationManager* const mgr = this;
        const auto pendingLocal = pending;
        const auto stepLocal = step;
        const auto didWorkLocal = didWork;
        if (!*stepLocal) {
            // restoreAll() cancelled this chain between ticks. The QTimer
            // had already stored a copy of the closure, so the tick still
            // fires — but it must do nothing, and above all must not
            // re-schedule the now-null heap function.
            return;
        }
        if (pendingLocal->isEmpty()) {
            // Chain bookkeeping BEFORE the emit: a drainFinished slot may
            // synchronously destroy the manager, after which
            // m_liveDrainChains is gone. Breaking the shared_ptr
            // self-capture cycle here also prevents the heap std::function
            // (holding a shared_ptr to itself, `pending`, and the captured
            // context) from leaking after every drain. This branch only
            // ever executes from a QTimer-stored COPY of the closure (the
            // first invocation cannot reach it — drainPendingRestores
            // early-returns on an empty queue), so nulling the heap
            // original cannot destroy the executing closure's captures; the
            // stack copies above protect the NON-empty path of the first
            // (heap-executing) invocation instead.
            mgr->m_liveDrainChains.removeOne(stepLocal);
            *stepLocal = nullptr;
            // Emit only when the chain actually processed a restore: an
            // all-vetoed cycle (or one that only swept dead windows) changed
            // nothing, and signalling it would make the effect rebuild every
            // border for free each fallback retry.
            if (*didWorkLocal) {
                Q_EMIT mgr->drainFinished();
            }
            return;
        }
        QPointer<DecorationManager> self(mgr);
        const QString windowId = pendingLocal->takeFirst();
        auto it = mgr->m_windows.find(windowId);
        // Re-acquired (acquire cleared the flag) or already forgotten: the
        // queued restore is stale — leave the decoration alone.
        if (it != mgr->m_windows.end() && it->pendingRestore) {
            // Only FALLBACK cycles count against the veto budget: explicit
            // batch-completion drains can arrive in quick succession during
            // multi-batch resnap churn and must not burn the bound.
            const bool vetoHolds = mgr->m_restoreVeto && mgr->m_restoreVeto(windowId);
            if (vetoHolds && (!fromFallback || ++it->vetoRetries <= MaxVetoRetries)) {
                // Authoritative state says the window should stay hidden
                // (e.g. its screen re-entered autotile mid-drain). Keep the
                // restore QUEUED instead of dropping it: the expected
                // re-acquire cancels it, and if that re-acquire never lands
                // (the retile declined the window) the fallback timer
                // retries. After MaxVetoRetries fallback cycles the veto is
                // overridden and the restore happens anyway — bounded
                // staleness beats stranding an ownerless hidden window when
                // the "re-acquire is coming" prediction was wrong.
                mgr->m_pendingRestore.insert(windowId);
                mgr->armFallbackTimer();
            } else {
                it->pendingRestore = false;
                // Completion clears all three pieces of deferred-restore
                // state: the flag, the queue-set entry (a mid-chain
                // release(Deferred) may have re-inserted it after the chain
                // snapshotted its queue — the restore below satisfies that
                // defer too), and the retry counter, keeping the
                // cancelPendingRestore lockstep invariant.
                it->vetoRetries = 0;
                mgr->m_pendingRestore.remove(windowId);
                const bool restored = mgr->restoreNow(windowId, *it, false);
                if (!self || !*stepLocal) {
                    // A windowDecorationRestored slot destroyed the manager,
                    // or called restoreAll() which cancelled this chain.
                    return;
                }
                mgr->pruneIfEmpty(windowId);
                if (restored) {
                    *didWorkLocal = true;
                }
            }
        }
        QTimer::singleShot(0, mgr, *stepLocal);
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
        // restore now. Re-assert geometry while mode owners remain — the
        // window is zone-placed and the frame change would leave it
        // overflowing its zone.
        restoreNow(windowId, entry, !entry.owners.isEmpty());
    }
}

void DecorationManager::finishRelease(const QString& windowId, Entry& entry, Restore restore)
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
    if (restore == Restore::Immediate) {
        // restoreNow emits; guard the epilogue against a restored-signal
        // slot destroying the manager — same contract as the drain step.
        QPointer<DecorationManager> self(this);
        restoreNow(windowId, entry, false);
        if (!self) {
            return;
        }
        pruneIfEmpty(windowId);
        return;
    }
    entry.pendingRestore = true;
    // A fresh deferred release starts a fresh retry epoch: the bounded-veto
    // counter must count consecutive vetoes of THIS restore, not remnants of
    // an earlier cancelled one.
    entry.vetoRetries = 0;
    m_pendingRestore.insert(windowId);
    armFallbackTimer();
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

void DecorationManager::cancelPendingRestore(const QString& windowId, Entry& entry)
{
    if (!entry.pendingRestore) {
        return;
    }
    entry.pendingRestore = false;
    entry.vetoRetries = 0;
    m_pendingRestore.remove(windowId);
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
    if (it->owners.isEmpty() && !it->vetoed && !it->physicallyHidden && !it->pendingRestore) {
        m_windows.erase(it);
    }
}

void DecorationManager::cancelFallbackTimer()
{
    if (m_pendingFallback) {
        m_pendingFallback->stop();
        m_pendingFallback->deleteLater();
        m_pendingFallback = nullptr;
    }
}

void DecorationManager::setFallbackIntervalForTesting(int ms)
{
    m_fallbackIntervalMs = qMax(0, ms);
    if (m_pendingFallback && m_pendingFallback->isActive()) {
        m_pendingFallback->start(m_fallbackIntervalMs);
    }
}

void DecorationManager::armFallbackTimer()
{
    if (!m_pendingFallback) {
        m_pendingFallback = new QTimer(this);
        m_pendingFallback->setSingleShot(true);
        connect(m_pendingFallback, &QTimer::timeout, this, [this]() {
            drainPendingRestoresInternal(/*fromFallback=*/true);
        });
    }
    // Never restart an already-running countdown. The arm churn this guards
    // against: finishRelease arms on every new deferred release, and the
    // 2nd..Nth vetoed windows within one drain chain each re-arm — without
    // the guard, steady release/veto traffic would push the countdown (and
    // with it the MaxVetoRetries budget, which only fallback cycles advance)
    // out indefinitely. Explicit drains are NOT covered by this guard and DO
    // reset the countdown by design: drainPendingRestoresInternal cancels
    // the timer at drain entry, so a veto re-queue inside that drain arms a
    // fresh full interval — acceptable, because the drain itself just
    // re-evaluated the veto (it is the liveness event the fallback exists
    // to approximate), and once explicit drains stop, fallback cycles bound
    // the strand.
    if (m_pendingFallback->isActive()) {
        return;
    }
    m_pendingFallback->start(m_fallbackIntervalMs);
}

} // namespace PhosphorCompositor
