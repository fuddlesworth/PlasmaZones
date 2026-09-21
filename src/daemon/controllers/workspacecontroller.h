// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorEngine/HandoffIntent.h>
#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <functional>

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>

namespace PhosphorEngine {
class WindowRegistry;
struct WindowMetadata;
}
namespace PhosphorScreens {
class ScreenManager;
}
namespace PhosphorWorkspaces {
class VirtualDesktopManager;
}

namespace PlasmaZones {

/// Daemon glue for dynamic per-monitor workspaces (constructed ONLY when the
/// feature is enabled — the gate wraps this object, not scattered ifs). Wires
/// VirtualDesktopManager and window-registry notifications into the
/// WorkspaceReconciler, maintains the per-desktop window census, computes the
/// geometry screen order, serializes the change-gated map stream, and executes
/// the reconciler's KWin requests. Engine reap/renumber fan-out and the D-Bus
/// publish stay in the daemon, connected to this controller's signals.
class WorkspaceController : public QObject
{
    Q_OBJECT

public:
    WorkspaceController(PhosphorWorkspaces::VirtualDesktopManager* vdm, PhosphorEngine::WindowRegistry* registry,
                        PhosphorScreens::ScreenManager* screens, QObject* parent = nullptr);
    /// Final state write (the debounced save may be pending at shutdown).
    ~WorkspaceController() override;

    /// Reads KWin's PerOutputVirtualDesktops from kwinrc. The layered gate's
    /// config arm (plan §7); the daemon refuses to start the controller when
    /// this is off and the consent latch has not turned it on.
    static bool kwinPerOutputEnabled();

    /// Begin: census seed, screen order, first-run adoption (deferred until
    /// every known screen has reported a current desktop, with a timeout
    /// fallback to the global current).
    void start();

    PhosphorWorkspaces::WorkspaceReconciler& reconciler();

    /// Whether adoption has completed. Until it has, this controller emits no
    /// reap and no renumber at all — the map is still the previous session's
    /// candidate — so the daemon's count-based desktop sweeps must keep
    /// running rather than standing down on the controller merely existing.
    /// Adoption can take up to three seconds (the per-output report grace).
    bool isAdopted() const;

    /// Inject the window → physical-screen resolver (the daemon backs it with
    /// the placement store's last managed-context screen). Powers the
    /// owner-wins reunion arm: a window sitting on a desktop owned by another
    /// output is moved to that output so it stays visible (plan §4.7's second
    /// half — the desktop's owner screen is where its windows belong).
    void setWindowScreenResolver(std::function<QString(const QString& windowId)> resolver);

    /// Inject the sticky (on-all-desktops) predicate, backed by the same
    /// WindowTrackingService answer the adaptor's move slot refuses on. The
    /// named-workspace verbs consult it before arming a move watchdog for a
    /// move the adaptor is going to refuse. Unset means "cannot tell", and
    /// every caller then behaves exactly as it did before the check existed.
    void setWindowStickyPredicate(std::function<bool(const QString& windowId)> predicate);

    /// Current wire payload (for the adaptor's replay query).
    QString currentMapJson() const;

    // ── Verbs (Phase 2). screenId is the PHYSICAL id of the acting screen;
    // windowId (move verbs) is the daemon-resolved active window. All walk
    // the screen's OWN slice (no wrap at the edges, niri semantics) and defer
    // behind the reconciler's ledger during structural churn. ─────────────────
    /// delta -1 = up, +1 = down.
    void focusWorkspace(const QString& screenId, int delta);
    /// Switch the screen to the 0-based slice index (focus-slot family).
    void focusWorkspaceAt(const QString& screenId, int sliceIndex);
    void moveWindowToWorkspace(const QString& screenId, const QString& windowId, int delta);
    /// The scrolling column variant: every window of the focused column moves
    /// together. `columnWindows` is enumerated by the daemon from the scroll
    /// engine (empty → OSD-level no-op, the screen is not scrolling).
    void moveColumnToWorkspace(const QString& screenId, const QStringList& columnWindows, int delta);
    /// Reorder the current workspace within its slice.
    void moveWorkspace(const QString& screenId, int delta);
    /// Re-own the current workspace to the neighbour output in `direction`
    /// ("left"/"right"), windows riding along; focuses it there.
    void moveWorkspaceToOutput(const QString& screenId, const QString& direction);

    // ── By-id verbs (the workspace overview; workspacecontroller_overview.cpp).
    // Every screen id arrives canonical; every refusal is a debug log and no
    // change. All defer behind the reconciler's ledger like the delta verbs. ──
    /// Switch @p screenId to @p desktopId, which must be in its own slice.
    void focusWorkspaceById(const QString& screenId, const QString& desktopId);
    /// Move @p windowId to @p desktopId (owned by @p screenId) with a drop
    /// intent for the target engine's handoffReceive. Refused for sticky
    /// windows and for desktops the screen does not own.
    void moveWindowToWorkspaceById(const QString& windowId, const QString& screenId, const QString& desktopId,
                                   const PhosphorEngine::HandoffIntent& intent);
    /// Move @p windowId into a NEW workspace of @p screenId at gap
    /// @p sliceIndex (0 = above the first, sliceSize = below the last). A gap
    /// at or past the trailing empty reuses that workspace (niri); a smaller
    /// index inserts a reserved workspace and moves the window once the
    /// create settles.
    void moveWindowToNewWorkspace(const QString& windowId, const QString& screenId, int sliceIndex,
                                  const PhosphorEngine::HandoffIntent& intent);
    /// Reorder @p desktopId to @p newSliceIndex inside @p screenId's slice.
    bool reorderWorkspaceById(const QString& screenId, const QString& desktopId, int newSliceIndex);
    /// Re-own @p desktopId to @p targetScreenId at @p sliceIndex, its windows
    /// riding along, and show it there.
    bool moveWorkspaceToScreenById(const QString& desktopId, const QString& targetScreenId, int sliceIndex);
    /// Push a KWin name for a DYNAMIC workspace (a named one rewrites its
    /// declaration instead; the overview controller owns that split).
    bool renameDynamicWorkspace(const QString& desktopId, const QString& name);
    /// The windows the census counts on @p desktopId.
    QStringList windowsOnWorkspace(const QString& desktopId) const;

    // ── Named workspaces (Phase 3) ──────────────────────────────────────────
    /// Apply the config declarations (QVariantMaps: name/output/position).
    /// Deferred until adoption; re-run on every declaration change.
    void applyNamedDeclarations(const QVariantList& entries);
    /// Focus / move-the-active-window-to the named workspace (the dynamic
    /// per-name shortcut targets). The acting screen for a focus is the
    /// name's OWNER screen (a named workspace shows where it lives).
    void focusNamedWorkspace(const QString& name);
    void moveWindowToNamedWorkspace(const QString& name, const QString& windowId);
    /// Whether `name` is realized in the map right now (the RouteToWorkspace
    /// rule resolver's guard — an unrealized name falls through to the
    /// positional desktop route).
    bool hasNamedWorkspace(const QString& name) const;
    /// Verdict of the RouteToWorkspace rule arm (see
    /// routeWindowToNamedWorkspace).
    enum class WorkspaceRouteVerdict {
        /// A structural op is in flight, or the realized name's desktop cannot
        /// be resolved right now. The name IS declared, so the cascade's
        /// positional RouteToDesktop is NOT a valid stand-in: that number was
        /// authored as the fallback for an UNDECLARED name, and applying it to
        /// a momentarily unresolvable one silently lands the window on a
        /// different desktop. The window stays where it spawned.
        Unresolvable = -1,
        /// The name is not realized in the map. The positional route applies.
        Unrealized = 0,
    };
    /// The RouteToWorkspace rule arm: issue the move NOW and report the
    /// REALIZED desktop number so the caller's placement context can resolve
    /// on it. A value > 0 is the desktop the window was routed to (the sticky
    /// "already there" case reports its desktop too — the rule asked for the
    /// window to be on that workspace and it is). Values <= 0 are
    /// WorkspaceRouteVerdict.
    ///
    /// @p moveOutput decides the OUTPUT leg. Under per-output virtual desktops
    /// the target workspace belongs to its OWNER monitor's slice, so a window
    /// that only changes desktop never appears on the screen that shows that
    /// workspace. The caller passes true when nothing else in the rule cascade
    /// owns the window's monitor, and false when an explicit RouteToScreen
    /// does (two contradictory output moves otherwise). The window's placement
    /// is kept honest independently: @p ownerScreenOut reports the workspace's
    /// owner screen so the caller can resolve the snap / tiling placement
    /// against the DESTINATION monitor rather than the spawn one.
    ///
    /// @p ownerScreenOut is written only when a move is actually issued, and
    /// is left untouched otherwise. Note that includes one > 0 return: a
    /// sticky window is already on every workspace, so the call reports the
    /// realized desktop (to suppress the caller's positional fallback) while
    /// issuing no move and naming no owner. Reporting one there pinned the
    /// placement to a monitor the window never reaches, so a caller must
    /// treat an untouched value as "no destination screen", not as an error.
    int routeWindowToNamedWorkspace(const QString& name, const QString& windowId, bool moveOutput,
                                    QString* ownerScreenOut = nullptr);

Q_SIGNALS:
    /// → adaptor setScreenDesktopRequested (effect per-output switch).
    void screenDesktopSwitchRequested(const QString& screenId, int desktop);
    /// → adaptor moveWindowToWorkspaceVerb (handoff-based window relocation).
    /// @p moveOutput false issues the desktop half only, leaving the window on
    /// its current monitor (the RouteToWorkspace open-path arm; see
    /// routeWindowToNamedWorkspace).
    /// @p targetDesktopId is the destination's STABLE id, carried beside the
    /// number so the compositor-side move can name the desktop rather than a
    /// position. Empty for the verbs that genuinely mean "whatever is at that
    /// position" (the directional moves); set by the named-workspace route,
    /// whose whole premise is an identity that outlives renumbering.
    void windowWorkspaceMoveRequested(const QString& windowId, const QString& targetScreenId, int targetDesktop,
                                      const QString& targetDesktopId, const QString& direction, bool moveOutput);
    /// The overview's window move: same relay as windowWorkspaceMoveRequested
    /// plus the drop intent the target engine places by.
    void windowWorkspaceMoveWithIntentRequested(const QString& windowId, const QString& targetScreenId,
                                                int targetDesktop, const QString& targetDesktopId,
                                                const PhosphorEngine::HandoffIntent& intent);
    /// Owner-wins snap-back fired for this screen (OSD hint hook; gated by
    /// the snapBackOsdHint setting daemon-side).
    void snapBackOccurred(const QString& screenId);
    /// Windows that mapped onto a workspace during its removal window were
    /// swept by KWin and re-routed to the owner's current workspace (plan
    /// §4.3 destroy step 4); OSD hint hook, same gating as snap-back.
    void windowDisplacedByRemoval(const QString& screenId);
    /// Change-gated wire payload (plan §3.2) — the daemon relays this to
    /// WindowTrackingAdaptor::workspaceMapChanged.
    void workspaceMapPublished(const QString& mapJson);
    /// Engine fan-out relays (identity-based; the daemon drives all three
    /// engines + the placement store from these).
    /// The WHOLE removed set of one settled change, not one signal per
    /// desktop: the daemon's fan-out schedules a config save and a state save
    /// at its tail, and per-desktop emissions made that N saves (each preceded
    /// by a full allModes() scan) for one removal batch.
    void desktopsReapRequested(const QList<int>& desktops);
    void desktopRenumberRequested(const QHash<int, int>& oldToNew);

public:
    /// The map, the census and the reconciler all key screens by the id the
    /// KWin effect REPORTS; ScreenManager and the settings UI hand out other
    /// spellings. Every externally sourced screen id passes through here,
    /// including every id the overview adaptor receives on the wire.
    static QString canonicalScreenId(const QString& connectorOrId);

private:
    void wireVirtualDesktops();
    void wireWindows();
    void wireScreens();
    void refreshScreenOrder();
    void onWindowAppeared(const QString& instanceId);
    void onWindowDisappeared(const QString& instanceId);
    void onMetadataChanged(const QString& instanceId, const PhosphorEngine::WindowMetadata& oldMeta,
                           const PhosphorEngine::WindowMetadata& newMeta);
    /// Effective census desktop for a window: its own desktop int, or 0 for
    /// sticky / multi-desktop / unknown (counts toward no desktop).
    static int censusDesktop(const PhosphorEngine::WindowMetadata& meta);
    /// The one census mutation. Every path that adds or drops a window from a
    /// desktop's count goes through here so the clamp, the empty-row drop and
    /// the reconciler notification cannot drift apart.
    void adjustPopulationById(const QString& desktopId, int delta);
    /// Number-keyed convenience over adjustPopulationById; translates through
    /// the VDM's current list.
    void adjustPopulation(int desktopInt, int delta);
    void publishIfChanged();
    void tryFirstAdoption();
    /// The shared post-adoption tail: realize the declarations parked while
    /// adoption was pending, then run the owner-wins reunion check over the
    /// census seeded before adoption (both are refused while !m_adopted).
    /// Both adoption paths (tryFirstAdoption and the start() timeout
    /// fallback) must run it.
    void applyNamedDeclarationsAfterAdoption();

    PhosphorWorkspaces::VirtualDesktopManager* m_vdm;
    PhosphorEngine::WindowRegistry* m_registry;
    PhosphorScreens::ScreenManager* m_screens;
    PhosphorWorkspaces::WorkspaceReconciler m_reconciler;
    /// Window count per desktop ID (translated at event time; ids are the
    /// fixed points across renumbering). desktopId → population.
    QHash<QString, int> m_populationById;
    /// The desktop ID a window was last counted against — an id STRING, not
    /// the desktop int, so a metadata change adjusts the right bucket even
    /// after renumbering (the int is translated on entry).
    /// instanceId → desktopId.
    QHash<QString, QString> m_windowCensusDesktopId;
    QString m_lastPublishedJson;
    bool m_adopted = false;

    // ── Verb support (workspacecontroller_verbs.cpp) ────────────────────────
    /// Run now if no structural op is in flight, else queue until the ledger
    /// quiets (plan §4.5: verb translation waits out the renumbering window).
    void runWhenQuiet(std::function<void()> fn);
    void drainQuietQueue();
    /// Issue the per-screen switch for a desktop id (ledgered SetCurrent →
    /// effect command with the int resolved at emit time).
    void switchScreenToDesktop(const QString& screenId, const QString& desktopId);
    QList<std::function<void()>> m_quietQueue;
    /// Set while drainQuietQueue walks the batch, so a mapChanged emitted by a
    /// drained verb cannot re-enter the drain.
    bool m_draining = false;
    /// Last applied named declarations (re-applied after adoption).
    QVariantList m_namedEntries;
    bool m_namedApplied = false;
    /// Open-path RouteToWorkspace requests that arrived before adoption
    /// (windowId → declared name). desktopIdForName refuses while !m_adopted,
    /// which is exactly the login / session-restore population such rules are
    /// written for, and nothing re-drove them afterwards. Re-issued once
    /// adoption completes, for windows that still exist. The output-leg
    /// decision is parked with the name: it came from the rule cascade that is
    /// no longer in hand when the drain re-issues the route.
    struct ParkedNamedRoute
    {
        QString name;
        bool moveOutput = false;
    };
    QHash<QString, ParkedNamedRoute> m_parkedNamedRoutes;
    void drainParkedNamedRoutes();
    /// Map entry id for a declared name, or empty.
    QString desktopIdForName(const QString& name) const;
    /// Run @p fn with the desktop's live 1-based number once the desktop
    /// manager knows it (now, or on the list refresh that carries it), or
    /// with 0 when the ledger timeout passes first.
    void whenDesktopNumbered(const QString& desktopId, std::function<void(int)> fn);
    /// Move every rider of @p desktopId to it on @p targetScreen (the window
    /// half of a workspace transfer), then show the workspace there. Shared
    /// by the directional and the by-id transfer.
    void relocateRidersAndShow(const QStringList& riders, const QString& desktopId, const QString& targetScreen,
                               const QString& direction);

    // ── Move-verb watchdog (plan §4.2) ──────────────────────────────────────
    /// windowWorkspaceMoveRequested is fire-and-forget over D-Bus; with no
    /// effect loaded, nothing executes and nothing errors. Record the
    /// expectation and warn when no census arrival confirms it in time — the
    /// op itself is already dropped (nothing retries), the log is the
    /// diagnosis. Arrival confirmation lives in onMetadataChanged.
    ///
    /// Returns false when the move must not be issued at all: a sticky window
    /// is on every workspace already and the adaptor drops the desktop move,
    /// so arming a watch for it only buys a spurious "saw no arrival" warning.
    /// Every caller uses the answer to skip its own emit.
    [[nodiscard]] bool watchWindowMove(const QString& windowId, const QString& targetDesktopId);
    QHash<QString, QString> m_pendingWindowMoves; ///< windowId → expected desktopId
    /// windowId → the sequence of the watch that owns the entry above, so a
    /// superseded watch's timer cannot retire its successor's expectation.
    QHash<QString, quint64> m_windowMoveSequences;
    quint64 m_windowMoveSequence = 0;
    /// Per-screen snap-back cooldown stamps (ms since epoch).
    QHash<QString, qint64> m_lastSnapBackMs;

    // ── Owner-wins reunion + destroy-race displacement (plan §4.3/§4.7) ─────
    /// If the window's desktop is owned by a screen other than the one the
    /// window sits on, issue the cross-screen move that reunites them.
    void reuniteWindowWithOwner(const QString& instanceId, const QString& desktopId);
    std::function<QString(const QString&)> m_windowScreenResolver;
    /// Sticky predicate (see setWindowStickyPredicate). Null until wired.
    std::function<bool(const QString&)> m_windowStickyPredicate;
    /// Per-window reunion cooldown stamps (ms since epoch): a reunion is an
    /// output move on an unchanged desktop, so no census arrival clears it —
    /// the cooldown is what stops a slow effect from drawing repeat issues.
    QHash<QString, qint64> m_lastReunionMs;
    /// Windows with a reunion body sitting in the quiet queue. Keeps a burst
    /// of reports during structural churn from queueing one body per report.
    QSet<QString> m_pendingReunions;
    struct DisplacedByRemoval
    {
        QString ownerScreenId;
        QStringList windowIds;
    };
    /// Census snapshot per doomed desktop (removal-race arm); consumed when
    /// the removal lands.
    QHash<QString, DisplacedByRemoval> m_displacedByRemoval;

    // ── State persistence (Phase 4) ─────────────────────────────────────────
    static QString stateFilePath();
    void loadStateFile();
    void saveStateFile() const;
    void scheduleStateSave();
    QTimer m_stateSaveTimer;
};

} // namespace PlasmaZones
