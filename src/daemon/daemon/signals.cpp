// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/daemon.h"
#include "helpers.h"
#include "config/settings.h"
#include "core/platform/logging.h"
#include "core/types/constants.h"
#include "daemon/controllers/unifiedlayoutcontroller.h"
#include "daemon/overlayservice.h"
#include "dbus/layoutadaptor/layoutadaptor.h"
#include "dbus/settingsadaptor/settingsadaptor.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorRules/ExclusionRules.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include <PhosphorZones/LayoutRegistry.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QRect>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <QVector>

#include <algorithm>
#include <memory>
#include <utility>

using PlacementEngineBase = PhosphorEngine::PlacementEngineBase;

namespace PlasmaZones {

void Daemon::initializeUnifiedController()
{
    // Initialize unified layout controller (manual layouts only).
    // Registry injected explicitly (not reached via engine->algorithmRegistry()):
    // keeps DI contract visible at the call site and lets unit tests stub the
    // engine without losing algorithm enumeration.
    m_unifiedLayoutController =
        std::make_unique<UnifiedLayoutController>(m_layoutManager.get(), m_settings.get(), m_screenManager.get(),
                                                  m_algorithmRegistry.get(), m_autotileEngine.get(), this);

    // Share the daemon's bundle-owned autotile source with the controller
    // so its internal layout cache's autotile half is populated from the
    // bundle's preview cache instead of rebuilt from scratch per call.
    m_unifiedLayoutController->setAutotileLayoutSource(m_autotileLayoutSource);

    // Set initial desktop/activity context for visibility-filtered cycling
    const int desktopNow = currentDesktop();
    m_layoutManager->setCurrentVirtualDesktop(desktopNow);
    m_unifiedLayoutController->setCurrentVirtualDesktop(desktopNow);
    if (m_activityManager && PhosphorWorkspaces::ActivityManager::isAvailable()) {
        m_layoutManager->setCurrentActivity(m_activityManager->currentActivity());
        m_unifiedLayoutController->setCurrentActivity(m_activityManager->currentActivity());
    }
}

void Daemon::connectLayoutSignals()
{
    // ═══════════════════════════════════════════════════════════════════════════
    // Mode-based layout filtering
    // ═══════════════════════════════════════════════════════════════════════════

    // Derive initial per-screen autotile state from assignments
    updateAutotileScreens();

    // Set initial layout filter
    updateLayoutFilter();

    // Pre-warm the per-screen unified passive overlay shell. The shell
    // hosts every kbd-None overlay slot (OSD, snap-assist, …) so a
    // single warm-up at daemon start covers QML compile + first-paint
    // pipeline build for all of them — subsequent per-content shows are
    // animator-only operations on already-mapped slot Items inside the
    // already-warmed shell wl_surface. Deferred so daemon init completes
    // first.
    if (m_overlayService) {
        QTimer::singleShot(0, this, [this]() {
            m_overlayService->warmUpNotifications();
            // Post-shell-migration every kbd-None overlay (OSD,
            // snap-assist, layout picker, zone-selector, main overlay)
            // lives as an Item slot inside the per-screen passive shell.
            // warmUpNotifications builds the shell wl_surface +
            // QQuickWindow + QML compile + first-paint pipeline once per
            // screen, which covers QML compile cost for every slot. Each
            // slot's content body (SnapAssistContent / LayoutPickerContent
            // / ZoneSelectorContent) is async-loaded on its first show
            // via the slot's `loaded` toggle, so per-content classes only
            // pay their construction cost when the user actually summons
            // them rather than at daemon start.
        });
    }

    // Note: autotileEnabledChanged, snappingEnabledChanged, and perScreenAutotileSettingsChanged
    // are handled by the consolidated settingsChanged handler (lives in
    // init_services.cpp since the daemon file split — single-pass processing).
    // Individual autotile signals only fire from runtime setters, where settingsChanged also
    // fires and handles the transitions — no separate handlers needed.

    // Re-derive autotile screens and layout filter when assignments change
    // (e.g., from D-Bus, layout picker, unified controller).
    // Also sync the unified controller's cycling index when the assignment
    // affects the current desktop — needed for D-Bus/KCM batch operations.
    // Do NOT touch setActiveLayout here — applyEntry and the drop-time
    // cross-layout switch in WindowDragAdaptor (drop.cpp) handle active
    // layout themselves, and calling it here with QSignalBlocker steals
    // the activeLayoutChanged transition, leaving the resnap buffer
    // empty. Desktop switches sync active layout via syncModeFromAssignments().
    // connectLayoutSignals()/connectOverlaySignals() re-run on every start(),
    // but these senders are NOT torn down in stop(), so without dropping the
    // prior connections a stop()/start() cycle stacks duplicates and every
    // handler below runs twice. Drop exactly the handles WE installed last
    // time: a (sender, signal, receiver) disconnect would also delete other
    // call sites' handlers on the same signals (initLayoutAndSettingsWiring()
    // connects layoutAssigned from the ctor, and that never re-runs), and
    // Qt::UniqueConnection does not apply to lambda/functor connections.
    // start() calls this one FIRST (lifecycle.cpp), so the clear lives here
    // and connectOverlaySignals() only appends.
    //
    // Scope note: this list covers the four connections below.
    // initializeAutotile()'s eight shortcut lambdas get the same treatment
    // from their own list, because start() calls it BEFORE this function
    // clears here, so a shared list would drop them right after install.
    //
    // m_layoutManager is constructed in the Daemon ctor and never reset, so it
    // is non-null on every path that reaches here; the guards elsewhere in
    // this file cover members that stop() DOES reset.
    for (const QMetaObject::Connection& c : std::as_const(m_restartScopedConnections)) {
        disconnect(c);
    }
    m_restartScopedConnections.clear();
    m_restartScopedConnections << connect(
        m_layoutManager.get(), &PhosphorZones::LayoutRegistry::layoutAssigned, this,
        [this](const QString& screenId, int virtualDesktop, PhosphorZones::Layout* /*layout*/) {
            updateAutotileScreens();
            updateLayoutFilter();

            // Sync unified controller cycling index when assignment affects current desktop.
            const int curDesktop = currentDesktopForScreen(screenId);
            if (virtualDesktop != 0 && virtualDesktop != curDesktop) {
                return;
            }
            if (!m_unifiedLayoutController || !m_layoutManager) {
                return;
            }
            const QString focusedScreenId = m_unifiedLayoutController->currentScreenName();
            if (focusedScreenId.isEmpty() || focusedScreenId != screenId) {
                return;
            }
            const QString assignmentId =
                m_layoutManager->assignmentIdForScreen(focusedScreenId, curDesktop, currentActivity());
            m_unifiedLayoutController->syncFromExternalState(assignmentId);
        });

    // Connect unified layout controller signals for OSD display
    connect(m_unifiedLayoutController.get(), &UnifiedLayoutController::layoutApplied, this,
            [this](PhosphorZones::Layout* layout) {
                if (!layout) {
                    return;
                }
                // Dismiss snap assist — it's stale once the layout changes
                if (m_overlayService && m_overlayService->isSnapAssistVisible()) {
                    m_overlayService->hideSnapAssist();
                }
                // Suppress during startup. Mirrors the algorithmChanged gate above.
                // `loadState()` from finalizeStartup() synchronously emits
                // `layoutApplied` per screen as layouts get assigned, and
                // finalizeStartup() is the authoritative startup-OSD path
                // (it calls `showOsdForAllScreens`). Letting this handler also
                // fire double-queues a show on every screen: the first OSD
                // begins its fly-in shader leg, the second show immediately
                // cancels that AV via `cancelTrackingFor` (whose
                // teardownShaderLeg parks the shader pieces)
                // and re-enters via the reuse path. The user sees the slide
                // animation get nuked mid-flight and visually perceives "show
                // doesn't animate", because only the hide leg (no duplicate)
                // plays cleanly.
                if (!m_running) {
                    return;
                }
                // Defer OSD display (same rationale as autotileApplied — first-time
                // QML compilation of the passive shell blocks the event loop
                // ~100-300ms). Capture layout ID (not raw pointer) to avoid
                // use-after-free if the layout is ever replaced between now and
                // next event loop pass.
                if (m_settings && m_settings->showOsdOnLayoutSwitch()) {
                    QUuid layoutId = layout->id();
                    QString screenId = m_unifiedLayoutController->currentScreenName();
                    showLayoutOsdDeferred(layoutId, screenId);
                }
            });

    connect(m_unifiedLayoutController.get(), &UnifiedLayoutController::autotileApplied, this,
            [this](const QString& algorithmName, int windowCount) {
                Q_UNUSED(windowCount)
                // Dismiss snap assist — autotile mode replaces snapping entirely
                if (m_overlayService && m_overlayService->isSnapAssistVisible()) {
                    m_overlayService->hideSnapAssist();
                }
                // Suppress during startup. Mirrors the algorithmChanged and
                // layoutApplied gates above. `loadState()` from finalizeStartup()
                // synchronously emits `autotileApplied` per screen as layouts get
                // assigned for autotile-mode entries, and finalizeStartup() is the
                // authoritative startup-OSD path (it calls `showOsdForAllScreens`).
                // Without this gate, autotile users would hit the same first-OSD
                // double-queue that the layoutApplied gate fixes for snap-mode
                // users: the first OSD begins its fly-in shader leg, the second
                // show cancels that AV via cancelTrackingFor's shader-leg park,
                // and the slide-in animation gets nuked mid-flight.
                if (!m_running) {
                    return;
                }
                // Defer OSD display so QML window creation (first-time ~100-300ms for
                // NotificationOverlay.qml compilation + scene graph) doesn't block the
                // daemon event loop while the effect is sending windowOpened D-Bus
                // calls. Without this, first toggle to autotile has perceptible lag
                // because the daemon can't process incoming tiling requests until the
                // OSD handler returns.
                if (m_settings && m_settings->showOsdOnLayoutSwitch() && m_autotileEngine && m_overlayService) {
                    QString algorithmId = m_autotileEngine->algorithmId();
                    QString screenId = m_unifiedLayoutController->currentScreenName();
                    showAlgorithmOsdDeferred(algorithmId, algorithmName, screenId);
                }
            });

    // Live cheatsheet refilter on mode switches. Mode authority is
    // ScreenModeRouter + AssignmentEntry::Mode, and the router is a plain
    // query class with no change signal — a mode switch normally lands as an
    // applied layout (the toggle-autotile handler routes through
    // applyLayoutById), so these two are the mode-change edge the sheet can
    // observe. The one exception, the bare suppressed-default autotile entry
    // written directly, calls refreshCheatsheetIfVisible itself at its write
    // site. refreshCheatsheetIfVisible re-resolves the mode for the
    // sheet's BOUND screen, so an apply on another screen is a harmless
    // no-op re-push. These live HERE, not in connectShortcutSignals(),
    // because the controller is created only in initializeUnifiedController,
    // which runs between the two; the connections die with the per-start
    // controller, so no tracked handle is needed.
    connect(m_unifiedLayoutController.get(), &UnifiedLayoutController::layoutApplied, this,
            [this](PhosphorZones::Layout*) {
                refreshCheatsheetIfVisible();
            });
    connect(m_unifiedLayoutController.get(), &UnifiedLayoutController::autotileApplied, this,
            [this](const QString&, int) {
                refreshCheatsheetIfVisible();
            });

    // Record manual layout only when user explicitly selects one via zone selector
    // or unified layout controller — NOT on every internal layout change.

    // No handler for OverlayService::manualLayoutSelected: the zone-selector
    // slot is input-transparent by design (ZoneSelectorContent's
    // `interactive: false`) and the QML hover path that used to emit this is
    // gone. Cross-layout commits happen at drop time inside WindowDragAdaptor
    // (drop.cpp), which calls assignLayout + setActiveLayout directly when
    // the user releases the drag on a zone in a different layout. Routing
    // through a hover-driven handler would resnap mid-interaction, which is
    // what produced the "layouts changing when holding alt" bug.
}

void Daemon::connectOverlaySignals()
{
    // No autotileLayoutSelected handler: the zone-selector slot is input-
    // transparent by design (see ZoneSelectorContent's `interactive: false`),
    // so QML never emits a hover-driven selection. Switching the autotile
    // algorithm via the zone-selector popup is gone — users have keyboard
    // shortcuts (NextLayout / QuickLayoutN) and the explicit Layout Picker
    // (Meta+Alt+Space by default) for that.

    // Connect Snap Assist selection: fetch authoritative zone geometry from service (same as
    // keyboard navigation) to avoid overlay coordinate drift/overlap bugs, then forward to effect
    // Same restart-duplication guard as connectLayoutSignals (see there).
    m_restartScopedConnections << connect(
        m_overlayService.get(), &IOverlayService::snapAssistWindowSelected, this,
        [this](const QString& windowId, const QString& zoneId, const QString& geometryJson, const QString& screenId) {
            // Resolve screen ID; fall back to primary screen
            QString effectiveScreenId = screenId;
            if (effectiveScreenId.isEmpty()) {
                // Prefer effective screen IDs (virtual-screen-aware) over physical screen
                const QStringList effectiveIds =
                    (m_screenManager ? m_screenManager->effectiveScreenIds() : QStringList());
                if (!effectiveIds.isEmpty()) {
                    effectiveScreenId = effectiveIds.first();
                }
            }
            // Snap assist is a manual-mode concept; ignore if the TARGET screen uses autotile.
            if (isAutotileScreen(effectiveScreenId)) {
                return;
            }
            // If the window is being pulled from a sibling virtual screen that uses
            // autotile, notify the engine so it removes the window from the source
            // screen's tiling tree and retiles the remaining windows.
            if (m_autotileEngine && m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()) {
                const QString sourceScreen = m_windowTrackingAdaptor->service()->screenForWindow(windowId);
                if (!sourceScreen.isEmpty() && sourceScreen != effectiveScreenId && isAutotileScreen(sourceScreen)) {
                    m_autotileEngine->windowClosed(windowId);
                    // Clear autotile-floated marker immediately — windowClosed removes
                    // the window from the engine but doesn't clear the WTS flag.
                    m_autotileEngine->clearModeSpecificFloatMarker(windowId);
                }
            }
            QRect geometry;
            if (!effectiveScreenId.isEmpty() && m_windowTrackingAdaptor) {
                geometry = m_windowTrackingAdaptor->zoneGeometryRect(zoneId, effectiveScreenId);
            }
            if (!geometry.isValid() && !geometryJson.isEmpty()) {
                QJsonObject obj = QJsonDocument::fromJson(geometryJson.toUtf8()).object();
                geometry = QRect(obj[QLatin1String("x")].toInt(), obj[QLatin1String("y")].toInt(),
                                 obj[QLatin1String("width")].toInt(), obj[QLatin1String("height")].toInt());
            }
            if (m_windowTrackingAdaptor) {
                m_windowTrackingAdaptor->requestMoveSpecificWindowToZone(windowId, zoneId, geometry);
            }
        });

    // Connect navigation feedback signal to show OSD (manual mode: from WindowTrackingAdaptor via KWin effect)
    m_restartScopedConnections << connect(
        m_windowTrackingAdaptor, &WindowTrackingAdaptor::navigationFeedback, this,
        [this](bool success, const QString& action, const QString& reason, const QString& sourceZoneId,
               const QString& targetZoneId, const QString& screenId) {
            // Suppress resnap OSD when triggered by a mode/layout change
            // (layout switch OSD already provides feedback)
            if (m_suppressResnapOsd > 0 && (action == QLatin1String("resnap") || action == QLatin1String("retile"))) {
                m_suppressResnapOsd = std::max(0, m_suppressResnapOsd - 1);
                return;
            }
            if (m_settings && m_settings->showNavigationOsd()) {
                if (m_overlayService) {
                    m_overlayService->showNavigationOsd(success, action, reason, sourceZoneId, targetZoneId, screenId);
                }
            }
        });

    // Note: AutotileEngine::navigationFeedbackRequested is relayed through
    // WindowTrackingAdaptor::navigationFeedback via setEngines(), so both engines
    // share the same OSD path through the handler above.
    //
    // KWin effect reports navigation feedback via reportNavigationFeedback D-Bus method,
    // which also emits the Qt navigationFeedback signal.

    // Dismiss snap assist when any window zone assignment changes (navigation, snap, unsnap,
    // float toggle, resnap, etc.). Snap assist is only relevant until the user performs another
    // window operation. During snap assist continuation, the window stays visible (keyboard
    // grab released) so this handler fires and destroys it; showContinuationIfNeeded() then
    // creates a fresh one. For non-continuation selections, this provides the final cleanup.
    m_restartScopedConnections << connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::windowZoneChanged, this,
                                          [this](const QString& /*windowId*/, const QString& /*zoneId*/) {
                                              if (m_overlayService && m_overlayService->isSnapAssistVisible()) {
                                                  m_overlayService->hideSnapAssist();
                                              }
                                          });

    // Mid-drag close cleanup: WindowDragAdaptor holds transient drag state
    // (m_draggedWindowId, m_pendingSnapDragWindowId, m_snapAssistPendingWindowId,
    // overlay refs) that must be torn down if the dragged window closes before
    // endDrag fires. The canonical close path runs through WTA::windowClosed
    // (called by the kwin-effect's notifyWindowClosed); we fan that out
    // in-process here instead of re-exposing a D-Bus surface no external
    // caller was wiring up.
    if (m_windowDragAdaptor) {
        // PMF slot, so Qt::UniqueConnection is available here (unlike the
        // lambda connections above) and keeps a restart from fanning each
        // close out twice.
        connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::windowClosedNotification, m_windowDragAdaptor,
                &WindowDragAdaptor::handleWindowClosed, Qt::UniqueConnection);
    }
}

void Daemon::finalizeStartup()
{
    // Restore autotile state from previous session (window order, algorithm, split ratio)
    // Defers actual retiling until windows are announced by KWin effect
    if (m_autotileEngine) {
        m_autotileEngine->loadState();
    }

    // Now that AutotileEngine::loadState has restored autotile placement records,
    // re-run the exclusion-rule prune so any loaded WindowPlacement records for apps
    // an Exclude rule covers are dropped from the unified store. The init-prologue
    // priming call (daemon.cpp's setExcludeRuleSet/setRules/prune sequence, run
    // synchronously before the rulesChanged subscription wires) already pruned what
    // was loaded then; this re-run covers records that landed during the later
    // autotile load. Patterns derive from the unified Rule store via
    // PhosphorRules::ExclusionRules; the WTA prune removeIf's the placement store.
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->pruneExcludedPendingRestores(
            PhosphorRules::ExclusionRules::applicationExcludePatternsFrom(m_excludeRuleSet));
    }

    // Signal that daemon is fully initialized and ready for queries
    if (m_layoutAdaptor) {
        Q_EMIT m_layoutAdaptor->daemonReady();
    }

    // Explicitly broadcast settingsChanged so the KWin effect re-fetches all
    // settings (including activation triggers) after daemonReady.  The effect's
    // initial async getSetting() calls can race with daemon construction and
    // return stale/empty values; this second broadcast guarantees a clean load.
    if (m_settingsAdaptor)
        Q_EMIT m_settingsAdaptor->settingsChanged();

    // Show the layout OSD on ALL screens so the user sees what's assigned everywhere.
    // The layoutApplied signal only fires for the focused screen; this covers the rest.
    // Gated on the desktop-switch OSD toggle (not the layout-switch toggle): startup is
    // a passive context change like a desktop switch, not an explicit user-driven layout
    // change, so users who disable desktop-switch OSDs expect quiet startup too.
    //
    // The welcome OSD reads the (screen, desktop, activity) cascade via
    // LayoutRegistry::assignmentIdForScreen. At finalizeStartup time the
    // activity may not have arrived from KActivities yet: currentActivity()
    // returns "", the cascade misses the user's stored entries (keyed on the
    // real activity UUID) and falls through to the level-1 default, which can
    // legitimately resolve to autotile. The OSD then says "Tiling: Binary
    // Split" while every configured screen is actually in snapping mode.
    //
    // So defer the show until BOTH the activity is known and we have seen at
    // least one currentDesktopChanged from VirtualDesktopManager (or its
    // initial sync read has populated). If activity is already non-empty when
    // finalizeStartup runs (the KActivities synchronous-init path), fire
    // immediately, otherwise queue on the first activityChanged. A short
    // timeout fallback keeps the OSD from being suppressed forever where
    // KActivities is unavailable, still emitting the empty-activity OSD that
    // activity-less environments already got.
    if (m_settings && m_settings->showOsdOnDesktopSwitch()) {
        const QString activity = currentActivity();
        // activityReady: either we already have a non-empty activity, or
        // KActivities is unavailable on this system (no point waiting).
        const bool activityReady = !activity.isEmpty() || !PhosphorWorkspaces::ActivityManager::isAvailable();
        if (activityReady) {
            showOsdForAllScreens(activity);
        } else if (m_activityManager) {
            // Defer the welcome OSD until the activity arrives from
            // KActivities, otherwise the cascade walks with empty
            // activity and misses the user's stored entries (which are
            // keyed on the actual activity UUID), silently surfacing
            // the level-1 fallback (e.g. autotile-bsp) on a system
            // configured for snapping. Two arms:
            //
            //   (a) signal — currentActivityChanged fires non-empty;
            //   (b) fallback timer — KActivities never reports.
            //
            // `fired` is the canonical "already shown" flag.
            // QMetaObject::Connection's `operator bool()` reports
            // "the connection was successfully made," NOT "still
            // active," so checking `*conn` after `disconnect(*conn)`
            // cannot tell "still queued" from "already fired and
            // disconnected." A shared_ptr<bool> captured by both arms
            // lets each compare-and-set first. The 5000ms timeout is
            // sized for KActivities cold-boot autostart at 2-3s on
            // slower systems, where 2s produced false fallbacks.
            auto conn = std::make_shared<QMetaObject::Connection>();
            auto fired = std::make_shared<bool>(false);
            *conn = connect(m_activityManager.get(), &PhosphorWorkspaces::ActivityManager::currentActivityChanged, this,
                            [this, conn, fired](const QString& a) {
                                if (a.isEmpty() || *fired) {
                                    return;
                                }
                                *fired = true;
                                QObject::disconnect(*conn);
                                showOsdForAllScreens(a);
                            });
            QTimer::singleShot(5000, this, [this, conn, fired]() {
                if (*fired) {
                    return;
                }
                *fired = true;
                QObject::disconnect(*conn);
                // Only fall back to firing the OSD if the activity has now
                // become available. If we land here while currentActivity()
                // is still empty AND KActivities is supposed to be
                // available, firing would replay the very wrong-cascade
                // OSD (autotile-bsp on a snapping-mode user) the deferral
                // was added to avoid — just delayed by 5 s instead of
                // immediate. Suppressing the welcome OSD entirely is the
                // lesser harm: the user gets no welcome banner, but never
                // sees content keyed to the wrong activity.
                const QString activity = currentActivity();
                if (activity.isEmpty() && PhosphorWorkspaces::ActivityManager::isAvailable()) {
                    return;
                }
                showOsdForAllScreens(activity);
            });
        }
    }

    // Prime the active-assignment snapshot now that screens and initial layouts
    // are applied, so the first rule edit diffs against the live assignments
    // (not an empty baseline, which would resnap every screen once).
    diffActiveAssignments();
}

void Daemon::syncAutotileFloatState(const QString& windowId, bool floating, const QString& screenId)
{
    // Sync floating state to PhosphorPlacement::WindowTrackingService and propagate
    // to KWin effect's NavigationHandler::m_floatingWindows via D-Bus signal.
    // Also track autotile origin so mode transitions can distinguish
    // autotile-originated floats from manual snapping-mode floats.
    //
    // Null-guard the engines: this slot is connected to autotile-engine
    // signals, so a queued-connection delivery that lands after
    // m_autotileEngine.reset() (shutdown window) would otherwise deref
    // a freed pointer. Mirrors the windowsReleased connect block above.
    if (!m_autotileEngine || !m_snapEngine) {
        return;
    }
    if (m_windowTrackingAdaptor) {
        PhosphorPlacement::WindowTrackingService* wts = m_windowTrackingAdaptor->service();
        if (floating) {
            m_windowTrackingAdaptor->setWindowFloating(windowId, true);
            m_autotileEngine->markModeSpecificFloated(windowId);
            // Clear stale snap-mode pre-float state ONLY when the pre-float data
            // is for the SAME screen (autotile re-float on the same screen).
            // When the window crosses from a snap VS (e.g. vs:0) to an autotile VS
            // (e.g. vs:1), the pre-float data for vs:0 must be preserved — it's
            // needed to restore the snap zone when the window returns to vs:0.
            const QString preFloatScreen = wts->preFloatScreen(windowId);
            if (preFloatScreen.isEmpty() || preFloatScreen == screenId) {
                wts->clearPreFloatZoneForWindow(windowId);
            }
        } else {
            // The window's snap-mode float (if any) already lives in its placement
            // record's snap slot — captured at the mode-switch snapshot / save time —
            // so there is no parallel saved-float set to update here. The record is
            // the single source of truth and drives the float restore on return to
            // snapping (see windowsReleased).
            m_windowTrackingAdaptor->setWindowFloating(windowId, false);
            m_autotileEngine->clearModeSpecificFloatMarker(windowId);
        }
    }
    // NOTE: Do NOT call unsnapForFloat() here — it destroys the window's
    // zone assignment from manual mode, making it "not snapped" when
    // returning to manual mode. PhosphorZones::Zone assignments are preserved so
    // switching back to manual restores the snapped state.

    // Restore geometry: applyGeometryForFloat prefers pre-snap (the window's
    // original position before any zone snapping) over pre-autotile (autotile tiling).
    // Pre-autotile persists across float/unfloat cycles (the effect's exact-match
    // contains-guard in saveAndRecordPreAutotileGeometry prevents overwriting), so
    // every float restores to the same original position. The effect does NOT apply
    // geometry on float — the daemon is the single source.
    if (floating && m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->applyGeometryForFloat(windowId, screenId);
    }

    // Use "Floating" and "Tiled" labels for autotile (not "Snapped" for unfloat)
    if (m_settings && m_settings->showNavigationOsd() && m_overlayService) {
        QString reason = floating ? QStringLiteral("floated") : QStringLiteral("tiled");
        m_overlayService->showNavigationOsd(true, QStringLiteral("float"), reason, QString(), QString(), screenId);
    }
}

void Daemon::syncAutotileFloatStatePassive(const QString& windowId, bool floating, const QString& screenId)
{
    // Passive state-sync: update WTS to match the engine's internal PhosphorTiles::TilingState,
    // but DO NOT call applyGeometryForFloat. This path is entered when the
    // engine inserts a window whose WTS floating state diverges from the
    // PhosphorTiles::TilingState (e.g. snap→autotile drag drops a window whose WTS still
    // says floating from a prior snap-mode Meta+F). The window already has a
    // valid position (the drop location); teleporting it via the stored
    // pre-tile rect would resize and jump it — discussion #271.
    //
    // Null-guard the engines for the same shutdown-window reason as
    // syncAutotileFloatState above.
    if (!m_autotileEngine || !m_snapEngine || !m_windowTrackingAdaptor) {
        return;
    }
    PhosphorPlacement::WindowTrackingService* wts = m_windowTrackingAdaptor->service();
    // Cross-engine handoff: this signal fires when autotile has just taken
    // ownership of a window that may still be tracked by snap. handoffRelease
    // is a no-op when the engine doesn't track the window, so the
    // isWindowTracked gate below is screen-agnostic (no same-screen-ID
    // comparison) — that closes the loophole where snap thought the window
    // was on the same screen ID as the autotile target (stale state
    // mid-mode-switch) and the previous screen-gated path skipped cleanup.
    // The tracked-check itself only keeps the handoff log meaningful.
    if (m_snapEngine && m_snapEngine->isWindowTracked(windowId)) {
        qCInfo(lcDaemon) << "Cross-engine handoff: releasing snap state for" << windowId << "(autotile screen"
                         << screenId << ")";
        m_snapEngine->handoffRelease(windowId);
    }
    if (floating) {
        m_windowTrackingAdaptor->setWindowFloating(windowId, true);
        m_autotileEngine->markModeSpecificFloated(windowId);
        // Mirror syncAutotileFloatState's cross-VS pre-float preservation so
        // zone-restore on return to the snap VS still works.
        const QString preFloatScreen = wts->preFloatScreen(windowId);
        if (preFloatScreen.isEmpty() || preFloatScreen == screenId) {
            wts->clearPreFloatZoneForWindow(windowId);
        }
    } else {
        // Snap-mode float persists in the placement record's snap slot (single
        // source of truth); nothing to save into a parallel set here.
        m_windowTrackingAdaptor->setWindowFloating(windowId, false);
        m_autotileEngine->clearModeSpecificFloatMarker(windowId);
    }
    // Deliberately no applyGeometryForFloat and no navigation OSD — this is
    // a silent sync of engine↔WTS state, not a user-visible float action.
}

void Daemon::syncAutotileBatchFloatState(const QStringList& windowIds, const QString& screenId)
{
    // Symmetric null-guard with syncAutotileFloatState — shutdown-window
    // queued connections shouldn't deref freed engine pointers.
    if (!m_autotileEngine || !m_windowTrackingAdaptor) {
        return;
    }
    PhosphorPlacement::WindowTrackingService* wts = m_windowTrackingAdaptor->service();
    for (const QString& windowId : windowIds) {
        // Update WTS state directly (not adaptor setWindowFloating: its extra
        // windowStateChanged/placement-capture side emissions are wrong for a
        // batch the effect already applied), but DO route the broadcast
        // bookkeeping through the relay chokepoint. Skipping it left
        // m_broadcastFloating at not-floating for a batch-floated window, so
        // the next genuine unfloat broadcast hit false==false in the gate and
        // was suppressed — stranding the effect's float cache exactly like
        // the silent flips this contract exists to prevent. The relay's
        // windowFloatingChanged emission is redundant for the effect (its
        // float-cache write no-ops, already floating from the
        // windowsTileRequested batch) and the rest of its handler is
        // convergent: a coalesced rule reconcile re-resolving to the same
        // state, idempotent snap-tracking clears, at most a raise of the
        // already-floated window. Overflow floats are rare, so the spare
        // signal is the price of a single-owner last-broadcast contract.
        wts->setWindowFloating(windowId, true);
        m_windowTrackingAdaptor->relayWindowFloatingChanged(windowId, true, screenId);
        m_autotileEngine->markModeSpecificFloated(windowId);
        // Same cross-VS preservation logic as the single-window handler
        const QString preFloatScreen = wts->preFloatScreen(windowId);
        if (preFloatScreen.isEmpty() || preFloatScreen == screenId) {
            wts->clearPreFloatZoneForWindow(windowId);
        }
    }
    if (m_settings && m_settings->showNavigationOsd() && m_overlayService && !windowIds.isEmpty()) {
        m_overlayService->showNavigationOsd(true, QStringLiteral("float"), QStringLiteral("overflow"), QString(),
                                            QString(), screenId);
    }
}

} // namespace PlasmaZones
