// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Workspace overview bring-up and teardown, plus the workspace OSD hints that
// the open overview suppresses. Its own TU rather than a tail on
// workspaces.cpp: that file sits at the size ceiling, and the overview is a
// consumer of the workspace map rather than part of its lifecycle.

#include "daemon/daemon.h"

#include "config/settings.h"
#include "core/platform/logging.h"
#include "daemon/controllers/overviewcontroller.h"
#include "daemon/controllers/shortcutmanager.h"
#include "daemon/controllers/workspacecontroller.h"
#include "daemon/overlayservice.h"
#include "dbus/overviewadaptor.h"
#include "dbus/scrollingadaptor/scrollingadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "phosphor_i18n.h"

#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>

namespace PlasmaZones {

QString Daemon::trackedWindowScreen(const QString& windowId) const
{
    // An engine's live tracking wins; the placement store's last
    // managed-context screen covers untracked (floating) windows that still
    // carry a record. Empty means "cannot vouch".
    for (PhosphorEngine::PlacementEngineBase* engine :
         {m_scrollEngine.get(), m_autotileEngine.get(), m_snapEngine.get()}) {
        if (engine && engine->isWindowTracked(windowId)) {
            return engine->screenForTrackedWindow(windowId);
        }
    }
    if (m_windowTrackingAdaptor) {
        if (auto* service = m_windowTrackingAdaptor->service()) {
            if (const auto record = service->placementStore().peekExact(windowId)) {
                return record->screenId;
            }
        }
    }
    return QString();
}

bool Daemon::isTrackedWindowSticky(const QString& windowId) const
{
    // The SAME answer the adaptor's move slot refuses on (crossmode.cpp asks
    // WindowTrackingService::isWindowSticky after shadowWindowId, and
    // shadowWindowId is that service's own canonicalizeForLookup, so a single
    // call lands on the identical key). Read through the daemon on every
    // call rather than captured, so a torn-down service cannot be
    // dereferenced; no service means "cannot tell".
    if (!m_windowTrackingAdaptor) {
        return false;
    }
    auto* service = m_windowTrackingAdaptor->service();
    return service && service->isWindowSticky(windowId);
}

bool Daemon::overviewOpen() const
{
    return m_overviewController && m_overviewController->isOpen();
}

void Daemon::wireWorkspaceOsdHints(QObject* wiring)
{
    // Both hints layer their own toggle ON TOP of the shared navigation gate
    // (OSD style None, global suppression, the per-context rule), and both
    // are suppressed while the overview is open: the zoomed-out view already
    // shows where the workspace went, and an OSD would paint under the
    // effect's opaque view anyway.
    const auto hintAllowed = [this](const QString& screenId) {
        return m_settings && m_settings->workspacesSnapBackOsdHint() && m_overlayService
            && navigationOsdAllowed(screenId) && !overviewOpen();
    };
    // Owner-wins snap-back hint (plain prose; toggleable).
    connect(m_workspaceController.get(), &WorkspaceController::snapBackOccurred, wiring,
            [this, hintAllowed](const QString& screenId) {
                if (hintAllowed(screenId)) {
                    m_overlayService->showDisabledOsd(
                        PhosphorI18n::tr("That workspace is on another monitor.", "OSD hint"), screenId);
                }
            });
    // A window mapped onto a workspace during its removal window and KWin
    // swept it to an arbitrary neighbour; the controller re-issued a move to
    // the owner's current workspace (plan §4.3 destroy step 4).
    connect(m_workspaceController.get(), &WorkspaceController::windowDisplacedByRemoval, wiring,
            [this, hintAllowed](const QString& screenId) {
                if (hintAllowed(screenId)) {
                    m_overlayService->showDisabledOsd(
                        PhosphorI18n::tr("That workspace closed. The window moved to the current one.", "OSD hint"),
                        screenId);
                }
            });
}

void Daemon::initializeOverview()
{
    if (m_overviewController || !m_workspaceController || !m_overviewAdaptor) {
        return;
    }
    OverviewController::Sources sources;
    sources.snapping = m_snapEngine.get();
    sources.tiling = m_autotileEngine.get();
    sources.scrolling = m_scrollEngine.get();
    m_overviewController = std::make_unique<OverviewController>(
        m_workspaceController.get(), m_virtualDesktopManager.get(), m_screenManager.get(), m_layoutManager.get(),
        m_settings.get(), m_windowRegistry.get(), sources);
    m_overviewController->setWindowScreenResolver([this](const QString& windowId) {
        return trackedWindowScreen(windowId);
    });
    m_overviewController->setWindowStickyPredicate([this](const QString& windowId) {
        return isTrackedWindowSticky(windowId);
    });
    m_overviewController->setActivityManager(m_activityManager.get());
    m_overviewController->setScrollEngine(qobject_cast<PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get()));
    // A pan landed off screen is a strip-snapshot change the engine does not
    // announce; mark the blob dirty so the debounced save picks it up.
    m_overviewController->setStripDirtyMarker([this]() {
        if (m_windowTrackingAdaptor) {
            if (auto* service = m_windowTrackingAdaptor->service()) {
                service->markDirty(PhosphorPlacement::WindowTrackingService::DirtyScrollStrips);
            }
        }
    });
    // The named declarations live in Settings; the overview rewrites them
    // there and the ordinary workspacesNamedEntriesChanged path re-applies.
    m_overviewController->setNamedEntriesAccess({[this]() {
                                                     return m_settings ? m_settings->workspacesNamedEntries()
                                                                       : QVariantList();
                                                 },
                                                 [this](const QVariantList& entries) {
                                                     if (m_settings) {
                                                         m_settings->setWorkspacesNamedEntries(entries);
                                                     }
                                                 }});
    // The overview's window move rides the same cross-mode handoff as the
    // directional verbs, plus the drop intent.
    connect(m_workspaceController.get(), &WorkspaceController::windowWorkspaceMoveWithIntentRequested,
            m_overviewController.get(),
            [this](const QString& windowId, const QString& targetScreenId, int targetDesktop,
                   const QString& targetDesktopId, const PhosphorEngine::HandoffIntent& intent) {
                if (m_windowTrackingAdaptor) {
                    m_windowTrackingAdaptor->moveWindowToWorkspaceWithIntent(windowId, targetScreenId, targetDesktop,
                                                                             targetDesktopId, intent);
                }
            });

    // Rebuild triggers. Every one is a coalesced 0 ms request that the
    // controller drops while closed, so this wiring costs nothing until the
    // effect opens the gate.
    OverviewController* const overview = m_overviewController.get();
    const auto rebuild = [overview]() {
        overview->scheduleRebuild();
    };
    connect(m_workspaceController.get(), &WorkspaceController::workspaceMapPublished, overview, rebuild);
    for (PhosphorEngine::PlacementEngineBase* engine :
         {m_scrollEngine.get(), m_autotileEngine.get(), m_snapEngine.get()}) {
        if (!engine) {
            continue;
        }
        connect(engine, &PhosphorEngine::PlacementEngineBase::placementChanged, overview, rebuild);
        connect(engine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged, overview, rebuild);
        connect(engine, &PhosphorEngine::PlacementEngineBase::windowsReleased, overview, rebuild);
    }
    if (m_scrollingAdaptor) {
        connect(m_scrollingAdaptor, &ScrollingAdaptor::stripChanged, overview, rebuild);
    }
    if (m_windowRegistry) {
        connect(m_windowRegistry.get(), &PhosphorEngine::WindowRegistry::windowAppeared, overview, rebuild);
        connect(m_windowRegistry.get(), &PhosphorEngine::WindowRegistry::windowDisappeared, overview, rebuild);
        connect(m_windowRegistry.get(), &PhosphorEngine::WindowRegistry::metadataChanged, overview, rebuild);
    }
    if (m_virtualDesktopManager) {
        connect(m_virtualDesktopManager.get(), &PhosphorWorkspaces::VirtualDesktopManager::screenDesktopChanged,
                overview, rebuild);
    }
    m_overviewAdaptor->setController(overview);
    // The toggle chord is owned by the daemon's ShortcutManager (never by the
    // plugin) and relayed to the effect as toggleOverviewRequested. Hangs off
    // the controller so a teardown severs it with everything else.
    if (m_shortcutManager) {
        connect(m_shortcutManager.get(), &ShortcutManager::overviewToggleRequested, overview, [this]() {
            m_overviewAdaptor->requestToggle();
        });
    }
    qCDebug(lcDaemon) << "workspace overview wired";
}

void Daemon::teardownOverview()
{
    if (!m_overviewController) {
        return;
    }
    // Detaching closes an open overview (the adaptor emits
    // closeOverviewRequested) BEFORE the controller the adaptor would
    // otherwise still answer replays from is destroyed.
    if (m_overviewAdaptor) {
        m_overviewAdaptor->setController(nullptr);
    }
    m_overviewController.reset();
}

} // namespace PlasmaZones
