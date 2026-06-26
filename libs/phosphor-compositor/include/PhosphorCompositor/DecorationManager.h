// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorcompositor_export.h>

#include <PhosphorCompositor/ICompositorBridge.h>

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

#include <optional>

namespace PhosphorCompositor {

/**
 * @brief The single owner of server-side decoration (title-bar) state.
 *
 * Every component that wants a window's title bar hidden registers as an
 * OWNER here instead of calling setNoBorder() itself. The decoration is
 * hidden while at least one owner holds the window (and no veto pins it
 * visible) and restored to its prior state when the last owner releases.
 * Title-bar hiding flows entirely through the window-rule layer
 * (setRuleOverride / clearAllRuleOverrides); there is exactly one owner kind.
 *
 * Invariants this class centralizes:
 *  - Capability gate: userCanSetNoBorder() — false for CSD apps
 *    (GTK/Electron) and other non-toggleable windows, which are tracked as
 *    owned but never physically touched.
 *  - Prior-state capture: a window that was already borderless when first
 *    acquired (user's own compositor rule) is restored to borderless, never
 *    force-decorated.
 *  - Geometry preservation: the compositor holds the CLIENT size constant
 *    across a decoration change, so toggling the decoration on a placed
 *    window changes the frame height by the title-bar height. Hiding a
 *    placed window therefore captures moveResizeGeometry() — NOT
 *    frameGeometry(), which lags on Wayland until the client acks the
 *    configure — toggles, and re-asserts the target.
 */
class PHOSPHORCOMPOSITOR_EXPORT DecorationManager : public QObject
{
    Q_OBJECT

public:
    enum class OwnerKind {
        Rule
    };

    /// An owner is a (kind, screen) pair. Rule owners are screen-agnostic
    /// (empty screenId).
    struct Owner
    {
        OwnerKind kind;
        QString screenId;
        bool operator==(const Owner&) const = default;
    };
    static Owner rule()
    {
        return {OwnerKind::Rule, QString()};
    }

    /// How the physical hide coordinates with geometry application.
    enum class Placement {
        /// The caller applies the zone geometry immediately after acquire.
        /// The manager only toggles the decoration.
        CallerWillPlace,
        /// The window is already at (or moving toward) its target: capture
        /// moveResizeGeometry() → setNoBorder(true) → re-assert the target so
        /// the content grows to fill the zone (rule changes).
        AlreadyPlaced,
    };

    /// @p bridge must outlive the manager (the KWin effect owns both, with
    /// the bridge destroyed after the manager). Taken by reference so a null
    /// bridge is unrepresentable — no runtime guard needed on any call path.
    explicit DecorationManager(ICompositorBridge& bridge, QObject* parent = nullptr);

    // ── Ownership ──────────────────────────────────────────────────────
    void acquire(const QString& windowId, const Owner& owner, Placement placement = Placement::AlreadyPlaced);

    // ── Bulk operations ────────────────────────────────────────────────
    /// Daemon loss / effect teardown: synchronously restore every window we
    /// hid to its prior state, then drop all tracking.
    void restoreAll();
    /// Window destroyed: drop all state for it. Zero compositor calls — the
    /// decoration dies with the window.
    void forgetWindow(const QString& windowId);

    // ── Window-rule layer (tri-state) ──────────────────────────────────
    /// nullopt: rule has no opinion — clear Rule owner and veto.
    /// true:    rule hides — acquire a Rule owner (clears any veto).
    /// false:   rule force-shows — VETO. The veto wins over every owner and
    ///          pins the decoration visible; owners re-assert when it lifts.
    void setRuleOverride(const QString& windowId, std::optional<bool> ruleValue);
    /// Rules went away wholesale (rule set emptied / teardown): clear every
    /// Rule owner and veto, restoring where no other owner remains.
    void clearAllRuleOverrides();

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

private:
    struct Entry
    {
        QVector<Owner> owners; // small-N linear scan
        bool vetoed = false;
        bool evaluated = false; ///< capability/prior state captured
        bool eligible = false; ///< userCanSetNoBorder at evaluation
        bool priorNoBorder = false; ///< decoration state before our first hide
        bool physicallyHidden = false;
    };

    /// Drive the window's physical decoration state toward the desired state
    /// derived from its owners/veto. The only place a hide is initiated.
    void reconcile(const QString& windowId, Entry& entry, Placement placement);
    /// Shared owner-removal epilogue: restore when the owner set emptied,
    /// then prune.
    void finishRelease(const QString& windowId, Entry& entry);
    /// Resolve @p windowId to a handle whose CURRENT id matches exactly.
    /// Bridge lookups may fall back to fuzzy app-level matching and resolve
    /// a same-app sibling for a dead id — physical decoration toggles must
    /// never act on a window other than the one the entry tracks.
    WindowHandle resolveExact(const QString& windowId) const;
    void hideNow(WindowHandle w, Placement placement);
    /// @return true when a physical restore happened (and
    /// windowDecorationRestored was emitted); false when the window is gone
    /// or was never physically hidden (priorNoBorder).
    bool restoreNow(const QString& windowId, Entry& entry, bool reassertGeometry);
    /// Drop the entry if it carries no information anymore.
    void pruneIfEmpty(const QString& windowId);

    QHash<QString, Entry> m_windows;
    ICompositorBridge& m_bridge;
};

} // namespace PhosphorCompositor
