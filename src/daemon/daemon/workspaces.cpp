// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// Daemon — dynamic per-monitor workspaces wiring
//
// Constructs the WorkspaceController behind the feature gate (setting +
// KWin per-output mode) and fans its identity-based reap/renumber out to all
// three placement engines and the unified placement store. The controller
// owns the model and the census; this file owns only what needs daemon
// members: the engine loop, the store transform, and the D-Bus stream relay.
// ═══════════════════════════════════════════════════════════════════════════════

#include "daemon/daemon.h"

#include "config/settings.h"
#include "core/platform/logging.h"
#include "daemon/controllers/workspacecontroller.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>

namespace PlasmaZones {

void Daemon::initializeWorkspaces()
{
    if (!m_settings || !m_settings->workspacesEnabled()) {
        return;
    }
    if (!WorkspaceController::kwinPerOutputEnabled()) {
        qCWarning(lcDaemon) << "dynamic workspaces enabled but KWin per-output virtual desktops is off; "
                               "feature stays dormant until the setting is turned on";
        return;
    }
    if (!m_virtualDesktopManager || !m_windowRegistry || !m_screenManager) {
        return;
    }

    m_workspaceController = std::make_unique<WorkspaceController>(m_virtualDesktopManager.get(), m_windowRegistry.get(),
                                                                  m_screenManager.get());

    // Identity-based engine fan-out: reap the removed desktops, then shift the
    // survivors — in every engine, whichever mode its screens run.
    const auto forEachEngine = [this](const std::function<void(PhosphorEngine::PlacementEngineBase*)>& fn) {
        for (PhosphorEngine::PlacementEngineBase* engine :
             {m_autotileEngine.get(), m_snapEngine.get(), m_scrollEngine.get()}) {
            if (engine) {
                fn(engine);
            }
        }
    };
    connect(m_workspaceController.get(), &WorkspaceController::desktopReapRequested, this,
            [this, forEachEngine](int desktop) {
                forEachEngine([desktop](PhosphorEngine::PlacementEngineBase* engine) {
                    engine->reapDesktopState(desktop);
                });
                // The unified placement store's per-record desktop tag: a
                // record on the dead desktop degrades to 0 (unknown) and the
                // effect's next report re-stamps it; KWin relocates the real
                // window either way.
                if (m_windowTrackingAdaptor) {
                    if (auto* service = m_windowTrackingAdaptor->service()) {
                        service->placementStore().transform([desktop](PhosphorEngine::WindowPlacement& record) {
                            if (record.virtualDesktop == desktop) {
                                record.virtualDesktop = 0;
                                return true;
                            }
                            return false;
                        });
                    }
                }
            });
    connect(m_workspaceController.get(), &WorkspaceController::desktopRenumberRequested, this,
            [this, forEachEngine](const QHash<int, int>& oldToNew) {
                forEachEngine([&oldToNew](PhosphorEngine::PlacementEngineBase* engine) {
                    engine->renumberDesktopState(oldToNew);
                });
                if (m_windowTrackingAdaptor) {
                    if (auto* service = m_windowTrackingAdaptor->service()) {
                        service->placementStore().transform([&oldToNew](PhosphorEngine::WindowPlacement& record) {
                            if (record.virtualDesktop <= 0) {
                                return false; // 0 = sticky/unknown sentinel
                            }
                            const int mapped = oldToNew.value(record.virtualDesktop, record.virtualDesktop);
                            if (mapped == record.virtualDesktop) {
                                return false;
                            }
                            record.virtualDesktop = mapped;
                            return true;
                        });
                    }
                }
            });

    // Change-gated stream to the effect (replay handled by the adaptor query).
    connect(m_workspaceController.get(), &WorkspaceController::workspaceMapPublished, this,
            [this](const QString& mapJson) {
                if (m_windowTrackingAdaptor) {
                    m_windowTrackingAdaptor->setWorkspaceMapPayload(mapJson);
                }
            });

    // Authoritative gate arm: the effect probes the running compositor's mode
    // at bringup. A divergence from the kwinrc read that admitted us here
    // means the file changed without a reconfigure — surface it loudly; the
    // controller keeps running (KWin's next reconfigure resolves it).
    connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::perOutputDesktopsModeReported, this, [](bool enabled) {
        if (!enabled) {
            qCWarning(lcDaemon) << "compositor reports per-output virtual desktops OFF while dynamic workspaces "
                                   "are active; kwinrc and the running KWin disagree (missing reconfigure?)";
        }
    });

    m_workspaceController->start();
    qCInfo(lcDaemon) << "dynamic workspaces active";
}

} // namespace PlasmaZones
