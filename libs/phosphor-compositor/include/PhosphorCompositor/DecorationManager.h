// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorcompositor_export.h>

#include <PhosphorCompositor/ICompositorBridge.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>

class QTimer;

namespace PhosphorCompositor {

/**
 * @brief The single owner of server-side decoration (title-bar) state.
 *
 * Every component that wants a window's title bar hidden — autotile retiles,
 * snap commits, window rules — registers as an OWNER here instead of calling
 * setNoBorder() itself. The decoration is hidden while at least one owner
 * holds the window (and no veto pins it visible) and restored to its prior
 * state when the last owner releases. This replaces the per-mode borderless
 * sets and the web of cross-mode "is the other mode still using it?" guards
 * that historically caused title-bar oscillation.
 *
 * Invariants this class centralizes (previously duplicated and divergent):
 *  - Capability gate: userCanSetNoBorder() — true for an SSD window even
 *    while it is CURRENTLY borderless (survives the autotile→snap handoff);
 *    false for CSD apps (GTK/Electron) and other non-toggleable windows,
 *    which are tracked as owned but never physically touched.
 *  - Prior-state capture: a window that was already borderless when first
 *    acquired (user's own compositor rule) is restored to borderless, never
 *    force-decorated.
 *  - Geometry preservation: the compositor holds the CLIENT size constant
 *    across a decoration change, so toggling the decoration on a placed
 *    window changes the frame height by the title-bar height. Hiding a
 *    placed window therefore captures moveResizeGeometry() — NOT
 *    frameGeometry(), which lags on Wayland until the client acks the
 *    configure — toggles, and re-asserts the target.
 *  - Deferred restore: decoration restores are synchronous Wayland
 *    round-trips (30–120 ms each); bulk restores (mode toggles) are queued
 *    and drained one per event-loop tick so concurrent animations stay
 *    smooth, with a fallback timer in case the drain trigger never fires.
 *    A window re-acquired while queued keeps its decoration hidden.
 */
class PHOSPHORCOMPOSITOR_EXPORT DecorationManager : public QObject
{
    Q_OBJECT

public:
    enum class OwnerKind {
        Autotile,
        Snap,
        Rule
    };

    /// An owner is a (kind, screen) pair. Autotile/Snap owners are per-screen
    /// so per-virtual-screen retiles release only their own claim and never
    /// disturb a sibling screen's. Rule owners are screen-agnostic (empty
    /// screenId).
    struct Owner
    {
        OwnerKind kind;
        QString screenId;
        bool operator==(const Owner&) const = default;
    };
    static Owner autotile(const QString& screenId)
    {
        return {OwnerKind::Autotile, screenId};
    }
    static Owner snap(const QString& screenId)
    {
        return {OwnerKind::Snap, screenId};
    }
    static Owner rule()
    {
        return {OwnerKind::Rule, QString()};
    }

    /// How the physical hide coordinates with geometry application.
    enum class Placement {
        /// The caller applies the zone geometry immediately after acquire
        /// (autotile tile path: hide first, applyWindowGeometry supplies the
        /// frame). The manager only toggles the decoration.
        CallerWillPlace,
        /// The window is already at (or moving toward) its target: capture
        /// moveResizeGeometry() → setNoBorder(true) → re-assert the target so
        /// the content grows to fill the zone (snap path, settings toggles,
        /// rule changes).
        AlreadyPlaced,
    };

    enum class Restore {
        Immediate, ///< restore synchronously when the last owner releases
        Deferred, ///< queue for drainPendingRestores() / the fallback timer
    };

    /// @p bridge must outlive the manager (the KWin effect owns both, with
    /// the bridge destroyed after the manager). Taken by reference so a null
    /// bridge is unrepresentable — no runtime guard needed on any call path.
    explicit DecorationManager(ICompositorBridge& bridge, QObject* parent = nullptr);
    /// Breaks any in-flight drain chains' self-reference cycles — a chain
    /// interrupted by manager destruction would otherwise leak its heap
    /// closure (the queued QTimer continuations die with the QObject, so
    /// the chain's own cycle-break never runs).
    ~DecorationManager() override;

    // ── Ownership ──────────────────────────────────────────────────────
    void acquire(const QString& windowId, const Owner& owner, Placement placement = Placement::AlreadyPlaced);
    void release(const QString& windowId, const Owner& owner, Restore restore = Restore::Immediate);
    /// Release every owner of @p kind regardless of screen (float, fullscreen,
    /// window leaving the mode entirely).
    void releaseKind(const QString& windowId, OwnerKind kind, Restore restore = Restore::Immediate);
    /// Cross-screen transfer: drop @p kind owners on every screen EXCEPT
    /// @p keepScreenId. Never produces a physical toggle — even when the
    /// kept screen's owner is not registered yet (the caller acquires it
    /// next); the decoration deliberately stays hidden across the hop.
    void releaseOthersOfKind(const QString& windowId, OwnerKind kind, const QString& keepScreenId);

    // ── Bulk operations ────────────────────────────────────────────────
    /// Per-mode hide-title-bars toggle OFF / whole-mode teardown.
    /// NOTE: there is deliberately no per-screen bulk release. Owners are
    /// per-screen but NOT per-desktop, and the mode-toggle path must apply
    /// desktop policy (sticky-window guards, current-desktop filters) per
    /// window before releasing — see AutotileHandler::slotScreensChanged.
    void releaseAllOfKind(OwnerKind kind, Restore restore = Restore::Immediate);
    /// Daemon loss / effect teardown: synchronously restore every window we
    /// hid to its prior state, then drop all tracking, timers, and any
    /// in-flight drain chains. The installed restore veto deliberately
    /// survives: it is wired once at effect construction and stays valid
    /// for the next daemon session (the post-restoreAll queue is empty, so
    /// it is not consulted until new deferred releases arrive).
    void restoreAll();
    /// Window destroyed: drop all state for it. Zero compositor calls — the
    /// decoration dies with the window.
    void forgetWindow(const QString& windowId);

    // ── Window-rule layer (tri-state) ──────────────────────────────────
    /// nullopt: rule has no opinion — clear Rule owner and veto.
    /// true:    rule hides — acquire a Rule owner (clears any veto).
    /// false:   rule force-shows — VETO. The veto wins over every owner and
    ///          pins the decoration visible; owners re-assert when it lifts.
    /// Geometry is re-asserted across veto-driven toggles while the window
    /// has mode owners (it is zone-placed).
    void setRuleOverride(const QString& windowId, std::optional<bool> ruleValue);
    /// Rules went away wholesale (rule set emptied / teardown): clear every
    /// Rule owner and veto, restoring where no mode owner remains.
    void clearAllRuleOverrides();

    // ── Deferred restore drain ─────────────────────────────────────────
    /// Drain queued restores, one decoration toggle per event-loop tick.
    /// Cancels the fallback timer. Re-entrant safe (snapshot-and-clear).
    void drainPendingRestores();
    /// Test seam: shrink the fallback-retry interval (default 500 ms) so
    /// timer-interplay tests run in milliseconds. Production never calls it.
    /// Negative values clamp to 0 (QTimer::start would warn on them).
    /// Restarts an already-running countdown with the new interval — the
    /// arm path deliberately never restarts an active timer (see
    /// armFallbackTimer), so ordering this call after a defer must apply
    /// the shrunk interval here.
    void setFallbackIntervalForTesting(int ms);
    /// Extra authoritative re-check evaluated per window at drain-step time
    /// (in addition to the built-in "window has owners again" check).
    /// Returning true keeps the restore QUEUED (re-armed via the fallback
    /// timer) so a re-acquire that never lands still restores eventually —
    /// it must therefore only return true while a re-acquire is genuinely
    /// expected. The KWin effect installs "window's screen is autotiled,
    /// hide-title-bars is on, and the window is not floating".
    /// The predicate MUST NOT call back into the manager (it runs between an
    /// entry lookup and writes through that entry; a re-entrant mutation
    /// could rehash the table under the held iterator).
    void setRestoreVeto(std::function<bool(const QString& windowId)> veto);

    // ── External-reset resync ──────────────────────────────────────────
    /// The compositor can silently reset noBorder (KWin does on desktop
    /// switches). If @p windowId should be hidden but the decoration came
    /// back, re-hide via the AlreadyPlaced sequence. No-op otherwise.
    void resyncWindow(const QString& windowId);

    // ── Queries ────────────────────────────────────────────────────────
    // The state-observation surface: production code expresses everything
    // through the ownership calls above, so these exist for the behavioral
    // test spec (and future render-layer integration) to assert manager
    // state without reaching into internals.
    /// True when we physically suppressed the window's decoration.
    bool isBorderless(const QString& windowId) const;
    bool isOwned(const QString& windowId) const;
    bool isOwnedBy(const QString& windowId, const Owner& owner) const;
    bool isVetoed(const QString& windowId) const;

Q_SIGNALS:
    /// Emitted after a physical decoration restore (the effect refreshes
    /// border overlays for the window). Slots may synchronously re-enter
    /// the manager — including destroying it or calling restoreAll() —
    /// every emit site guards its epilogue accordingly.
    void windowDecorationRestored(const QString& windowId);
    /// Emitted when a drain chain completes having processed at least one
    /// restore (the effect rebuilds all borders). An all-vetoed chain —
    /// every queued restore re-queued for the fallback retry — emits
    /// nothing: zero decorations changed, so a rebuild would be pure churn.
    void drainFinished();

private:
    struct Entry
    {
        QVector<Owner> owners; // small-N linear scan
        bool vetoed = false;
        bool evaluated = false; ///< capability/prior state captured
        bool eligible = false; ///< userCanSetNoBorder at evaluation
        bool priorNoBorder = false; ///< decoration state before our first hide
        bool physicallyHidden = false;
        bool pendingRestore = false;
        /// Consecutive FALLBACK-TIMER drain cycles whose restore the veto
        /// re-queued (explicit batch-completion drains re-queue without
        /// counting — multi-batch resnap churn must not burn the budget).
        /// Bounds the veto: after MaxVetoRetries fallback cycles with no
        /// re-acquire the restore happens anyway — bounded staleness beats
        /// stranding an ownerless hidden window when the effect-side
        /// "a re-acquire is coming" prediction turns out wrong.
        int vetoRetries = 0;
    };

    /// Drive the window's physical decoration state toward the desired state
    /// derived from its owners/veto. The only place a hide is initiated.
    void reconcile(const QString& windowId, Entry& entry, Placement placement);
    /// Shared owner-removal epilogue: restore (now or deferred) when the
    /// owner set emptied, then prune.
    void finishRelease(const QString& windowId, Entry& entry, Restore restore);
    /// Cancel a queued deferred restore (flag + queue set + retry counter
    /// together — the three must never desync across the cancel sites).
    void cancelPendingRestore(const QString& windowId, Entry& entry);
    /// The drain implementation. @p fromFallback marks fallback-timer-
    /// initiated drains — only those count against the bounded-veto budget.
    void drainPendingRestoresInternal(bool fromFallback);
    /// Resolve @p windowId to a handle whose CURRENT id matches exactly.
    /// Bridge lookups may fall back to fuzzy app-level matching and resolve
    /// a same-app sibling for a dead id — physical decoration toggles must
    /// never act on a window other than the one the entry tracks.
    WindowHandle resolveExact(const QString& windowId) const;
    void hideNow(WindowHandle w, Placement placement);
    /// @return true when a physical restore happened (and
    /// windowDecorationRestored was emitted); false when the window is gone
    /// or was never physically hidden (priorNoBorder). Drain chains use the
    /// result to decide whether the chain "did work" — a chain that only
    /// swept dead windows must not fire drainFinished.
    bool restoreNow(const QString& windowId, Entry& entry, bool reassertGeometry);
    /// Drop the entry if it carries no information anymore.
    void pruneIfEmpty(const QString& windowId);
    /// Re-arm the fallback timer that drains pending restores if no
    /// drainPendingRestores() call arrives.
    void armFallbackTimer();
    /// Stop and release the fallback timer (drain start, teardown).
    void cancelFallbackTimer();

    QHash<QString, Entry> m_windows;
    QSet<QString> m_pendingRestore;
    QPointer<QTimer> m_pendingFallback;
    int m_fallbackIntervalMs; // FallbackDrainMs unless shrunk for tests
    std::function<bool(const QString&)> m_restoreVeto;
    /// In-flight drain chains (re-entrant drains snapshot-and-clear, so
    /// several can coexist). A chain removes itself on termination; the
    /// destructor nulls survivors to break their self-reference cycles.
    QVector<std::shared_ptr<std::function<void()>>> m_liveDrainChains;
    ICompositorBridge& m_bridge;
};

} // namespace PhosphorCompositor
