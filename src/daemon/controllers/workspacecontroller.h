// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <functional>

#include <QHash>
#include <QList>
#include <QObject>
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

    /// Current wire payload (for the adaptor's replay query).
    QString currentMapJson() const;

    // ── Verbs (Phase 2). screenId is the PHYSICAL id of the acting screen;
    // windowId (move verbs) is the daemon-resolved active window. All walk
    // the screen's OWN slice (no wrap at the edges, niri semantics) and defer
    // behind the reconciler's ledger during structural churn. ─────────────────
    /// delta -1 = up, +1 = down.
    void focusWorkspace(const QString& screenId, int delta);
    void moveWindowToWorkspace(const QString& screenId, const QString& windowId, int delta);
    /// 0-based slice index (the quick-shortcut slots pass slot-1).
    void moveWindowToWorkspaceAt(const QString& screenId, const QString& windowId, int sliceIndex);
    /// The scrolling column variant: every window of the focused column moves
    /// together. `columnWindows` is enumerated by the daemon from the scroll
    /// engine (empty → OSD-level no-op, the screen is not scrolling).
    void moveColumnToWorkspace(const QString& screenId, const QStringList& columnWindows, int delta);
    /// Reorder the current workspace within its slice.
    void moveWorkspace(const QString& screenId, int delta);
    /// Re-own the current workspace to the neighbour output in `direction`
    /// ("left"/"right"), windows riding along; focuses it there.
    void moveWorkspaceToOutput(const QString& screenId, const QString& direction);

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

Q_SIGNALS:
    /// → adaptor setScreenDesktopRequested (effect per-output switch).
    void screenDesktopSwitchRequested(const QString& screenId, int desktop);
    /// → adaptor moveWindowToWorkspaceVerb (handoff-based window relocation).
    void windowWorkspaceMoveRequested(const QString& windowId, const QString& targetScreenId, int targetDesktop,
                                      const QString& direction);
    /// Owner-wins snap-back fired for this screen (OSD hint hook; gated by
    /// the snapBackOsdHint setting daemon-side).
    void snapBackOccurred(const QString& screenId);
    /// Change-gated wire payload (plan §3.2) — the daemon relays this to
    /// WindowTrackingAdaptor::workspaceMapChanged.
    void workspaceMapPublished(const QString& mapJson);
    /// Engine fan-out relays (identity-based; the daemon drives all three
    /// engines + the placement store from these).
    void desktopReapRequested(int desktop);
    void desktopRenumberRequested(const QHash<int, int>& oldToNew);

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
    void adjustPopulation(int desktopInt, int delta);
    void publishIfChanged();
    void tryFirstAdoption();

    PhosphorWorkspaces::VirtualDesktopManager* m_vdm;
    PhosphorEngine::WindowRegistry* m_registry;
    PhosphorScreens::ScreenManager* m_screens;
    PhosphorWorkspaces::WorkspaceReconciler m_reconciler;
    /// Window census by desktop ID (translated at event time; ids are the
    /// fixed points across renumbering).
    QHash<QString, int> m_populationById;
    /// Per-window census desktop int at last sighting, so a metadata change
    /// adjusts the right bucket even after renumbering (translated on entry).
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
    /// Last applied named declarations (re-applied after adoption).
    QVariantList m_namedEntries;
    bool m_namedApplied = false;
    /// Map entry id for a declared name, or empty.
    QString desktopIdForName(const QString& name) const;

    // ── Move-verb watchdog (plan §4.2) ──────────────────────────────────────
    /// windowWorkspaceMoveRequested is fire-and-forget over D-Bus; with no
    /// effect loaded, nothing executes and nothing errors. Record the
    /// expectation and warn when no census arrival confirms it in time — the
    /// op itself is already dropped (nothing retries), the log is the
    /// diagnosis. Arrival confirmation lives in onMetadataChanged.
    void watchWindowMove(const QString& windowId, const QString& targetDesktopId);
    QHash<QString, QString> m_pendingWindowMoves; ///< windowId → expected desktopId
    /// Per-screen snap-back cooldown stamps (ms since epoch).
    QHash<QString, qint64> m_lastSnapBackMs;

    // ── State persistence (Phase 4) ─────────────────────────────────────────
    static QString stateFilePath();
    void loadStateFile();
    void saveStateFile() const;
    void scheduleStateSave();
    QTimer m_stateSaveTimer;
};

} // namespace PlasmaZones
