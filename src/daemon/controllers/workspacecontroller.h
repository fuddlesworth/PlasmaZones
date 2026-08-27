// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

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

Q_SIGNALS:
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
};

} // namespace PlasmaZones
