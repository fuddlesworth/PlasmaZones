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
        /// (autotile tile path: hide first, applySnapGeometry supplies the
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

    explicit DecorationManager(ICompositorBridge* bridge, QObject* parent = nullptr);
    ~DecorationManager() override;

    // ── Ownership ──────────────────────────────────────────────────────
    void acquire(const QString& windowId, const Owner& owner, Placement placement = Placement::AlreadyPlaced);
    void release(const QString& windowId, const Owner& owner, Restore restore = Restore::Immediate);
    /// Release every owner of @p kind regardless of screen (float, fullscreen,
    /// window leaving the mode entirely).
    void releaseKind(const QString& windowId, OwnerKind kind, Restore restore = Restore::Immediate);
    /// Cross-screen transfer: drop @p kind owners on every screen EXCEPT
    /// @p keepScreenId. Never produces a physical toggle while the kept
    /// owner remains.
    void releaseOthersOfKind(const QString& windowId, OwnerKind kind, const QString& keepScreenId);

    // ── Bulk operations ────────────────────────────────────────────────
    /// Per-mode hide-title-bars toggle OFF / whole-mode teardown.
    void releaseAllOfKind(OwnerKind kind, Restore restore = Restore::Immediate);
    /// Mode disabled on one screen (e.g. autotile→snap toggle on screen X).
    void releaseScreen(OwnerKind kind, const QString& screenId, Restore restore);
    /// Daemon loss / effect teardown: synchronously restore every window we
    /// hid to its prior state, then drop all tracking and timers.
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
    /// Extra authoritative re-check evaluated per window at drain-step time
    /// (in addition to the built-in "window has owners again" check).
    /// Returning true skips the restore and leaves the decoration hidden.
    /// The KWin effect installs "is this window's screen autotiled now?".
    void setRestoreVeto(std::function<bool(const QString& windowId)> veto);

    // ── External-reset resync ──────────────────────────────────────────
    /// The compositor can silently reset noBorder (KWin does on desktop
    /// switches). If @p windowId should be hidden but the decoration came
    /// back, re-hide via the AlreadyPlaced sequence. No-op otherwise.
    void resyncWindow(const QString& windowId);

    // ── Queries ────────────────────────────────────────────────────────
    /// True when we physically suppressed the window's decoration.
    bool isBorderless(const QString& windowId) const;
    bool isOwned(const QString& windowId) const;
    bool hasOwnerOfKind(const QString& windowId, OwnerKind kind) const;
    bool isOwnedBy(const QString& windowId, const Owner& owner) const;
    bool isVetoed(const QString& windowId) const;

Q_SIGNALS:
    /// Emitted after a physical decoration restore (the effect refreshes
    /// border overlays for the window).
    void windowDecorationRestored(const QString& windowId);
    /// Emitted when a drain chain completes (the effect rebuilds all borders).
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
    };

    /// Drive the window's physical decoration state toward the desired state
    /// derived from its owners/veto. The only place a hide is initiated.
    void reconcile(const QString& windowId, Entry& entry, Placement placement);
    /// Shared owner-removal epilogue: restore (now or deferred) when the
    /// owner set emptied, then prune.
    void finishRelease(const QString& windowId, Entry& entry, Restore restore);
    void hideNow(WindowHandle w, Placement placement);
    void restoreNow(const QString& windowId, Entry& entry, bool reassertGeometry);
    /// Drop the entry if it carries no information anymore.
    void pruneIfEmpty(const QString& windowId);
    /// Re-arm the fallback timer that drains pending restores if no
    /// drainPendingRestores() call arrives.
    void armFallbackTimer();

    QHash<QString, Entry> m_windows;
    QSet<QString> m_pendingRestore;
    QPointer<QTimer> m_pendingFallback;
    std::function<bool(const QString&)> m_restoreVeto;
    ICompositorBridge* m_bridge;
};

} // namespace PhosphorCompositor
