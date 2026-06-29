// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"
#include "helpers.h"
#include "../overlayservice.h"
#include "../unifiedlayoutcontroller.h"
#include "../shortcutmanager.h"
#include "../../config/settingsconfigstore.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/LayoutComputeService.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorContext/ContextResolver.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include "../../core/geometryutils.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../dbus/layoutadaptor.h"
#include "../../dbus/settingsadaptor.h"
#include "../../dbus/shaderadaptor.h"
#include "../../dbus/compositorbridgeadaptor.h"
#include "../../dbus/controladaptor.h"
#include "../../dbus/overlayadaptor.h"
#include "../../dbus/zonedetectionadaptor.h"
#include "../../dbus/snapadaptor.h"
#include "../../dbus/windowtrackingadaptor.h"
#include "../../dbus/windowdragadaptor.h"
#include "../../dbus/autotileadaptor.h"
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>
#include "../../core/shaderregistry.h"
#include "../../config/settingsconfigstore.h"
#include <PhosphorZones/ZoneDetector.h>
#include <QProcess>
#include <QPointer>
#include "../../config/settings.h"
#include <QGuiApplication>
#include <QScreen>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorIdentity/WindowId.h>
#include <algorithm>

namespace PlasmaZones {

void Daemon::connectScreenSignals()
{
    // Start screen manager
    m_screenManager->start();

    // Warn about identical monitors producing duplicate screen IDs — both at
    // startup for the already-connected set and on every subsequent hotplug.
    // The startup call covers the common case; wiring it to screenAdded
    // catches users plugging a second identical monitor mid-session, where
    // the disambiguation "/CONNECTOR" suffix kicks in for the first time.
    Utils::warnDuplicateScreenIds();
    connect(m_screenManager.get(), &PhosphorScreens::ScreenManager::screenAdded, this,
            [](const PhosphorScreens::PhysicalScreen&) {
                Utils::warnDuplicateScreenIds();
            });

    // Settings → PhosphorScreens::ScreenManager refresh flows exclusively through the
    // IConfigStore contract: SettingsConfigStore forwards
    // Settings::virtualScreenConfigsChanged to IConfigStore::changed, which
    // PhosphorScreens::ScreenManager subscribes to in its start() (already called above) and
    // which also seeds the initial cache via loadAll(). A parallel direct
    // Settings observer here would double every refresh — left intentionally
    // absent.

    // React to PhosphorScreens::ScreenManager VS cache changes (driven by the IConfigStore
    // OR by direct test calls). Delegates to onVirtualScreensReconfigured
    // which migrates window assignments, refreshes autotile, resnaps
    // windows, and schedules downstream geometry updates. NOTE: this
    // handler no longer writes back to Settings — Settings is now the
    // source, not the sink.
    connect(m_screenManager.get(), &PhosphorScreens::ScreenManager::virtualScreensChanged, this,
            &Daemon::onVirtualScreensReconfigured);
    connect(m_screenManager.get(), &PhosphorScreens::ScreenManager::virtualScreenRegionsChanged, this,
            &Daemon::onVirtualScreenRegionsChanged);

    // Identifier-drift propagation: ScreenManager just re-keyed its own
    // in-memory VS cache; the persistent store (Settings) must follow or
    // the next reload would re-insert the orphaned entry under the old id.
    // Fires on same-model hotplug where disambiguation flips
    // bare ↔ "/CONNECTOR"-suffixed form. Gated on m_settings so tests that
    // construct a manager without Settings don't crash.
    connect(m_screenManager.get(), &PhosphorScreens::ScreenManager::screenIdentifierChanged, this,
            [this](const QString& oldId, const QString& newId) {
                if (!m_settings) {
                    return;
                }
                // Block the store's changed() fan-out while we rename: the
                // ScreenManager has ALREADY re-keyed its in-memory cache in
                // propagateIdentifierDrift, so letting renameVirtualScreenConfig's
                // virtualScreenConfigsChanged emission ride the direct connect
                // through SettingsConfigStore → IConfigStore::changed →
                // onConfigStoreChanged → refreshVirtualConfigs would fire a
                // full loadAll() round-trip whose diff is already empty. Block
                // the relay for the duration of the rename only; unblock after
                // save() so any genuine later writers still propagate.
                {
                    QSignalBlocker blocker(m_virtualScreenStore.get());
                    m_settings->renameVirtualScreenConfig(oldId, newId);
                }
                m_settings->save();
            });

    // Connect screen manager signals
    connect(m_screenManager.get(), &PhosphorScreens::ScreenManager::screenAdded, this,
            [this](const PhosphorScreens::PhysicalScreen& screen) {
                // Invalidate cached EDID serial so a fresh sysfs read happens for this connector
                // (handles the case where EDID wasn't available during very early startup)
                PhosphorScreens::ScreenIdentity::invalidateEdidCache(screen.name);
                // The daemon's ScreenManager runs on the live QtScreenProvider,
                // so a tracked screen always carries a real QScreen — qscreen
                // is non-null here.
                m_overlayService->handleScreenAdded(screen.qscreen);
                // Recalculate zone geometries for all effective screen IDs on this physical screen.
                // Note: VS cache restoration on screen re-add is no longer needed —
                // PhosphorScreens::ScreenManager::onScreenRemoved no longer wipes m_virtualConfigs, so
                // the entry survives a disconnect and is reused as-is when the screen
                // comes back. Settings is the source of truth and pushes updates via
                // refreshVirtualConfigs() in response to its own change signal.
                const QString physId = screen.identifier;
                const QStringList vsIds = m_screenManager->virtualScreenIdsFor(physId);
                const QString activity = currentActivity();
                for (const QString& sid : vsIds) {
                    // Per-output virtual desktops (#648): each screen its own desktop.
                    const int desktop = currentDesktopForScreen(sid);
                    PhosphorZones::Layout* screenLayout = m_layoutManager->layoutForScreen(sid, desktop, activity);
                    if (screenLayout) {
                        PhosphorZones::LayoutComputeService::recalculateSync(
                            screenLayout,
                            GeometryUtils::effectiveScreenGeometry(m_screenManager.get(), screenLayout, sid));
                    }
                }
            });

    connect(m_screenManager.get(), &PhosphorScreens::ScreenManager::screenRemoved, this,
            [this](const PhosphorScreens::PhysicalScreen& screen) {
                // Suppress OSD shows for ~1 s after any screen removal — see m_screensSettlingUntil.
                m_screensSettlingUntil = std::chrono::steady_clock::now() + std::chrono::seconds(1);

                m_overlayService->handleScreenRemoved(screen.qscreen);

                // Capture screen ID BEFORE invalidating cache (screenIdentifier reads cached EDID)
                const QString removedName = screen.name;
                const QString removedScreenId = screen.identifier;

                // Drop the removed output's per-output virtual-desktop entries (#648)
                // so the maps don't retain stale desktops across monitor hot-plug.
                // The autotile engine self-prunes via updateAutotileScreens; the VDM
                // and layout registry are physical-id keyed (the effect reports
                // physical output ids), matching removedScreenId. The overlay service
                // delegates to the layout registry, so clearing it there suffices.
                if (m_virtualDesktopManager) {
                    m_virtualDesktopManager->removeScreenDesktop(removedScreenId);
                }
                if (m_layoutManager) {
                    m_layoutManager->clearCurrentVirtualDesktopForScreen(removedScreenId);
                }

                // Invalidate cached EDID serial so a different monitor on this connector is detected
                PhosphorScreens::ScreenIdentity::invalidateEdidCache(removedName);

                // Clean stale entries from layout visibility restrictions
                // Check both screen ID (new) and connector name (legacy)
                for (PhosphorZones::Layout* layout : m_layoutManager->layouts()) {
                    QStringList allowed = layout->allowedScreens();
                    if (allowed.isEmpty())
                        continue;
                    bool changed = false;
                    changed |= (allowed.removeAll(removedScreenId) > 0);
                    changed |= (allowed.removeAll(removedName) > 0);
                    if (changed) {
                        layout->setAllowedScreens(allowed);
                    }
                }
            });

    connect(m_screenManager.get(), &PhosphorScreens::ScreenManager::screenGeometryChanged, this, [this] {
        m_geometryUpdatePending = true;
        m_geometryUpdateTimer.start();
    });

    // Connect to available geometry changes (panels added/removed/resized)
    // This is reactive - the sensor windows automatically track panel changes
    // Uses debouncing to coalesce rapid changes into a single update
    connect(m_screenManager.get(), &PhosphorScreens::ScreenManager::availableGeometryChanged, this, [this] {
        m_geometryUpdatePending = true;
        m_geometryUpdateTimer.start();
    });

    // Don't pre-create overlay windows at startup. On Wayland with the layer-shell
    // QPA plugin this can cause visibility issues. Create on-demand in show() instead,
    // which also avoids the overlay flashing during login.
    qCInfo(lcDaemon) << "Overlay service: ready," << m_screenManager->screens().count()
                     << "screens available (windows created on-demand)";
}

void Daemon::connectDesktopActivity()
{
    // Initialize and start virtual desktop manager
    m_virtualDesktopManager->init();
    m_virtualDesktopManager->start();

    // Virtual desktop changes are handled on TWO distinct paths (#648):
    //
    // 1) currentDesktopChanged — KWin's GLOBAL current desktop. Under Plasma 6.7
    //    "switch desktops independently for each screen", this flips merely on
    //    cursor movement between monitors on different desktops, so reacting to it
    //    with OSD / autotile recompute is the bug. Here it ONLY keeps the global
    //    desktop caches coherent with the active screen's desktop, for the
    //    currentVirtualDesktop() consumers (and the per-screen-map fallback). These
    //    are cheap value-sets that already fired on cursor-follow before the fix,
    //    so this is not a regression. NO OSD, NO autotile recompute here.
    connect(m_virtualDesktopManager.get(), &PhosphorWorkspaces::VirtualDesktopManager::currentDesktopChanged, this,
            [this](int desktop) {
                m_layoutManager->setCurrentVirtualDesktop(desktop);
                if (m_unifiedLayoutController) {
                    m_unifiedLayoutController->setCurrentVirtualDesktop(desktop);
                }
            });

    // 2) screenDesktopChanged — the KWin effect's PER-OUTPUT desktopChanged report
    //    (via VirtualDesktopManager). The authoritative per-screen switch; drives
    //    all context + OSD work scoped to the ONE screen that switched. The effect
    //    does NOT report this on cursor movement, so the per-desktop context thrash
    //    and the spurious all-screens OSD of #648 are gone. In single-desktop mode
    //    the effect fans this out to every screen, so behaviour is unchanged.
    connect(m_virtualDesktopManager.get(), &PhosphorWorkspaces::VirtualDesktopManager::screenDesktopChanged, this,
            [this](const QString& screenId, int desktop) {
                // [SEQ A] Cancel any active drag-insert preview before the engine's
                // desktop changes, else cancel/commit would hit the wrong TilingState.
                if (m_autotileEngine && m_autotileEngine->hasDragInsertPreview()) {
                    m_autotileEngine->cancelDragInsertPreview();
                }
                // [SEQ B] Pin screens where all autotiled windows are sticky BEFORE
                // changing the desktop context, so currentKeyForScreen() still
                // resolves existing TilingStates ("virtualdesktopsonlyonprimary").
                if (m_autotileEngine && m_windowTrackingAdaptor) {
                    auto* service = m_windowTrackingAdaptor->service();
                    m_autotileEngine->updateStickyScreenPins([service](const QString& windowId) {
                        return service->isWindowSticky(windowId);
                    });
                }
                // [SEQ C] Set THIS screen's engine desktop context (pure per-screen
                // swap, no state migration) BEFORE updateAutotileScreens() so the
                // engine resolves TilingStates under the new (screen, desktop) key.
                if (m_autotileEngine) {
                    m_autotileEngine->setCurrentDesktopForScreen(screenId, desktop);
                }
                // [SEQ D] Per-screen layout/overlay resolution context. The
                // overlay service delegates to the layout registry for per-output
                // desktop resolution, so this one push drives both (#648).
                m_layoutManager->setCurrentVirtualDesktopForScreen(screenId, desktop);
                // [SEQ E] Per-desktop assignments may differ — recompute autotile
                // screens, re-sync mode/filter, then refresh overlay geometry.
                updateAutotileScreens();
                syncModeFromAssignments();
                if (m_overlayService->isVisible()) {
                    m_overlayService->updateGeometries();
                }
                // OSD on the ONE screen that switched (#648).
                showDesktopSwitchOsdForScreen(screenId, currentActivity());
                // A context switch lays out its own (different) windows, so
                // refresh the active-assignment snapshot without applying —
                // otherwise the next rule edit would diff the new-context
                // assignment against the old and falsely re-resnap this screen.
                diffActiveAssignments();
            });

    // Prune stale PhosphorTiles::TilingState entries and disabled-desktop numbers when desktops are removed
    connect(m_virtualDesktopManager.get(), &PhosphorWorkspaces::VirtualDesktopManager::desktopCountChanged, this,
            [this](int newCount) {
                // Prune stale disabled-desktop entries (desktop numbers > newCount no longer exist).
                // NOTE: KDE Plasma renumbers desktops when one in the middle is removed (e.g.
                // removing desktop 2 of 4 shifts 3→2 and 4→3). We only prune out-of-range
                // entries here; mid-range renumbering would require tracking which desktop was
                // removed (not available from desktopCountChanged). A future improvement could
                // use KDE's desktop UUIDs instead of 1-based numbers.
                if (m_settings) {
                    // Prune both per-mode lists — a stale entry in either side leaks
                    // gates on now-deleted desktops just as effectively.
                    bool changed = false;
                    for (const auto mode : PhosphorZones::allModes()) {
                        QStringList disabled = m_settings->disabledDesktops(mode);
                        if (pruneDisabledDesktopEntries(disabled, newCount)) {
                            m_settings->setDisabledDesktops(mode, disabled);
                            changed = true;
                        }
                    }
                    if (changed) {
                        m_settings->save();
                    }
                }

                if (m_autotileEngine) {
                    // Desktop numbers are 1-based. Any state with desktop > newCount
                    // is stale. desktopsWithActiveState() returns the set of desktops
                    // currently holding tiling state — we filter that for anything
                    // past the new count and prune, avoiding the arbitrary upper
                    // bound of the old newCount+20 sweep.
                    const QSet<int> active = m_autotileEngine->desktopsWithActiveState();
                    for (int d : active) {
                        if (d > newCount) {
                            m_autotileEngine->pruneStatesForDesktop(d);
                        }
                    }
                }
                // Prune fallback assignment maps
                pruneContextMapsForDesktop(newCount);
            });

    // Set initial virtual desktop on components that maintain their own copy
    // (WindowDragAdaptor reads from PhosphorZones::LayoutRegistry directly via resolveLayoutForScreen())
    const int initialDesktop = currentDesktop();
    m_overlayService->setCurrentVirtualDesktop(initialDesktop);
    m_layoutManager->setCurrentVirtualDesktop(initialDesktop);
    if (m_autotileEngine) {
        m_autotileEngine->setCurrentDesktop(initialDesktop);
    }

    // Initialize and start activity manager
    // Connect to PhosphorWorkspaces::VirtualDesktopManager for desktop+activity coordinate lookup
    m_activityManager->init();
    if (PhosphorWorkspaces::ActivityManager::isAvailable()) {
        m_activityManager->start();

        // Prune stale PhosphorTiles::TilingState entries and disabled-activity IDs when activities are added/removed
        connect(m_activityManager.get(), &PhosphorWorkspaces::ActivityManager::activitiesChanged, this, [this]() {
            if (!m_activityManager) {
                return;
            }
            const QStringList activities = m_activityManager->activities();
            const QSet<QString> validSet(activities.begin(), activities.end());

            // Prune both per-mode disabled-activity lists.
            if (m_settings) {
                bool changed = false;
                for (const auto mode : PhosphorZones::allModes()) {
                    QStringList disabled = m_settings->disabledActivities(mode);
                    if (pruneDisabledActivityEntries(disabled, validSet)) {
                        m_settings->setDisabledActivities(mode, disabled);
                        changed = true;
                    }
                }
                if (changed) {
                    m_settings->save();
                }
            }

            if (m_autotileEngine) {
                m_autotileEngine->pruneStatesForActivities(activities);
            }
            pruneContextMapsForActivities(validSet);
        });

        // Set initial activity on components that maintain their own copy
        const QString initialActivity = m_activityManager->currentActivity();
        m_overlayService->setCurrentActivity(initialActivity);
        m_layoutManager->setCurrentActivity(initialActivity);
        if (m_autotileEngine) {
            m_autotileEngine->setCurrentActivity(initialActivity);
        }

        // Connect activity changes: update all components
        connect(m_activityManager.get(), &PhosphorWorkspaces::ActivityManager::currentActivityChanged, this,
                [this](const QString& activityId) {
                    m_overlayService->setCurrentActivity(activityId);
                    m_layoutManager->setCurrentActivity(activityId);
                    if (m_unifiedLayoutController) {
                        m_unifiedLayoutController->setCurrentActivity(activityId);
                    }
                    // Activity switch invalidates the TilingStateKey context — cancel
                    // any active drag-insert preview before the engine's activity changes.
                    if (m_autotileEngine && m_autotileEngine->hasDragInsertPreview()) {
                        m_autotileEngine->cancelDragInsertPreview();
                    }
                    // Pin sticky screens before changing activity context
                    if (m_autotileEngine && m_windowTrackingAdaptor) {
                        auto* service = m_windowTrackingAdaptor->service();
                        m_autotileEngine->updateStickyScreenPins([service](const QString& windowId) {
                            return service->isWindowSticky(windowId);
                        });
                    }
                    // Set engine's activity context BEFORE updateAutotileScreens()
                    if (m_autotileEngine) {
                        m_autotileEngine->setCurrentActivity(activityId);
                    }
                    // Per-activity assignments may differ — recompute autotile screens
                    updateAutotileScreens();
                    // Sync mode, layout filter, and controller state from per-activity assignments.
                    syncModeFromAssignments();
                    if (m_overlayService->isVisible()) {
                        m_overlayService->updateGeometries();
                    }

                    showDesktopSwitchOsd(activityId);
                    // Refresh the active-assignment snapshot for the new activity
                    // context (no apply — the switch handles its own windows) so a
                    // later rule edit doesn't falsely re-resnap. Mirrors the
                    // per-screen desktop-switch handler above.
                    diffActiveAssignments();
                });
    }
}

void Daemon::connectShortcutSignals()
{
    // NOTE: registerShortcuts() is called by Daemon::start() before this method.
    // Do NOT call it again here — it would hit the m_registrationInProgress guard
    // and log a spurious warning.

    // Connect shortcut signals
    // Screen detection: On X11, QCursor::pos() works; on Wayland, background daemons
    // get stale cursor data. resolveShortcutScreenId() handles both by falling back to
    // the screen reported by the KWin effect's windowActivated D-Bus call.
    connect(m_shortcutManager.get(), &ShortcutManager::openSettingsRequested, this, []() {
        // Launch in its own systemd scope so stopping the daemon service
        // doesn't kill the settings app (they'd share a cgroup otherwise).
        if (!QProcess::startDetached(
                QStringLiteral("systemd-run"),
                {QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("plasmazones-settings")})) {
            // Fallback if systemd-run is unavailable
            if (!QProcess::startDetached(QStringLiteral("plasmazones-settings"), {})) {
                qCWarning(lcDaemon) << "Failed to launch plasmazones-settings";
            }
        }
    });
    connect(m_shortcutManager.get(), &ShortcutManager::openEditorRequested, this, [this]() {
        // Screen-targeted (edits a screen's layout) — resolve cursor-first.
        // See layoutPickerRequested below for the rationale.
        QString screenId = resolveCursorScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
        if (screenId.isEmpty() && m_unifiedLayoutController) {
            screenId = m_unifiedLayoutController->currentScreenName();
        }
        if (!screenId.isEmpty()) {
            // Pass the effective screen ID directly — the editor handles both
            // physical and virtual screen IDs (VS-aware since v2.9).
            m_layoutAdaptor->openEditorForScreen(screenId);
        } else {
            m_layoutAdaptor->openEditor();
        }
    });
    // Quick layout shortcuts (Meta+Alt+1-9). Quick slots are per mode: in
    // snapping mode the slot holds a zone-layout UUID, in autotile mode an
    // autotile algorithm ID. Resolve the cursor screen's current mode, look up
    // that mode's slot, and apply the explicitly-bound layout — NOT the Nth
    // layout in priority order.
    connect(m_shortcutManager.get(), &ShortcutManager::quickLayoutRequested, this, [this](int number) {
        if (!m_unifiedLayoutController || !m_layoutManager) {
            return;
        }
        // Screen-targeted (applies a layout to a screen) — resolve
        // cursor-first. See layoutPickerRequested below for the rationale.
        const QString screenId = resolveCursorScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
        if (screenId.isEmpty()) {
            qCDebug(lcDaemon) << "QuickLayout shortcut: no screen info";
            return;
        }
        const PhosphorZones::AssignmentEntry::Mode mode = currentModeFor(screenId);
        const QString slotId = m_layoutManager->quickLayoutSlots(mode).value(number);
        if (slotId.isEmpty()) {
            // Explicitly-unbound slot for this mode — a deliberate no-op,
            // never a fallback to priority order.
            qCDebug(lcDaemon) << "QuickLayout shortcut: slot" << number << "unset for mode" << mode;
            return;
        }
        m_unifiedLayoutController->setCurrentScreenName(screenId);
        if (isScreenLockedForLayoutChange(screenId)) {
            return;
        }
        // Filter the controller's layout list to the SAME mode we resolved the
        // slot from, so the bound slot ID (manual UUID in snapping, autotile
        // algorithm in autotile) is always in scope for applyLayoutById. Deriving
        // the filter from `mode` here — rather than re-resolving the screen's mode
        // via updateLayoutFilterForScreen, which reads the assignment cascade —
        // keeps the slot lookup and the filter on one source of truth
        // (currentModeFor). The two can otherwise disagree when the live engine is
        // autotile-active via a per-screen override the cascade doesn't carry,
        // which would leave applyLayoutById unable to find the autotile slot.
        // applyLayoutById routes through applyEntry, which handles both manual
        // assignment and autotile algorithm switching.
        const bool autotile = (mode == PhosphorZones::AssignmentEntry::Autotile);
        m_unifiedLayoutController->setLayoutFilter(!autotile, autotile);
        if (!m_unifiedLayoutController->applyLayoutById(slotId)) {
            return;
        }
        resnapIfManualMode();
    });

    // Cycle layout shortcuts (Meta+[/])
    connect(m_shortcutManager.get(), &ShortcutManager::previousLayoutRequested, this, [this]() {
        if (m_cycleLayoutDebounce.isValid() && m_cycleLayoutDebounce.elapsed() < kShortcutDebounceMs) {
            return;
        }
        m_cycleLayoutDebounce.restart();
        // Screen-targeted (cycles a screen's layout) — resolve cursor-first.
        // See layoutPickerRequested below for the rationale.
        const QString screenId = resolveCursorScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
        if (screenId.isEmpty()) {
            qCDebug(lcDaemon) << "PreviousLayout shortcut: no screen info";
            return;
        }
        handleCycleLayout(screenId, false);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::nextLayoutRequested, this, [this]() {
        if (m_cycleLayoutDebounce.isValid() && m_cycleLayoutDebounce.elapsed() < kShortcutDebounceMs) {
            return;
        }
        m_cycleLayoutDebounce.restart();
        // Screen-targeted (cycles a screen's layout) — resolve cursor-first.
        // See layoutPickerRequested below for the rationale.
        const QString screenId = resolveCursorScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
        if (screenId.isEmpty()) {
            qCDebug(lcDaemon) << "NextLayout shortcut: no screen info";
            return;
        }
        handleCycleLayout(screenId, true);
    });

    // ═══════════════════════════════════════════════════════════════════════════
    // Keyboard Navigation Shortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    // Navigation shortcuts — single code path per operation (handleXxx)
    connect(m_shortcutManager.get(), &ShortcutManager::moveWindowRequested, this, [this](NavigationDirection d) {
        handleMove(d);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::focusZoneRequested, this, [this](NavigationDirection d) {
        handleFocus(d);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::pushToEmptyZoneRequested, this, [this]() {
        handlePush();
    });
    connect(m_shortcutManager.get(), &ShortcutManager::restoreWindowSizeRequested, this, [this]() {
        handleRestore();
    });
    connect(m_shortcutManager.get(), &ShortcutManager::toggleWindowFloatRequested, this, [this]() {
        handleFloat();
    });
    connect(m_shortcutManager.get(), &ShortcutManager::swapWindowRequested, this, [this](NavigationDirection d) {
        handleSwap(d);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::rotateWindowsRequested, this, [this](bool cw) {
        handleRotate(cw);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::swapVirtualScreenRequested, this, [this](NavigationDirection d) {
        handleSwapVirtualScreen(d);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::rotateVirtualScreensRequested, this, [this](bool cw) {
        handleRotateVirtualScreens(cw);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::snapToZoneRequested, this, [this](int n) {
        handleSnap(n);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::cycleWindowsInZoneRequested, this, [this](bool fwd) {
        handleCycle(fwd);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::resnapToNewLayoutRequested, this, [this]() {
        handleResnap();
    });
    connect(m_shortcutManager.get(), &ShortcutManager::snapAllWindowsRequested, this, [this]() {
        handleSnapAll();
    });

    // PhosphorZones::Layout picker shortcut (interactive layout browser + resnap)
    // Capture screen name at open time so it's still valid after the picker closes.
    //
    // Escape handling: KWin's wlr-layer-shell does not deliver keyboard
    // events to the picker's QQuickWindow on this Qt/KDE combination
    // (verified via Keys.onPressed diagnostic — fires zero times for the
    // duration of the picker). The QML Shortcut path is therefore unable
    // to react to Escape. We register Escape via KGlobalAccel using the
    // SAME id as the drag-cancel shortcut (`kCancelOverlayId`) so that:
    //   1. KGlobalAccel doesn't see two distinct actions competing for
    //      Escape — it only routes to one action per key, and the second
    //      ad-hoc registration would otherwise be silently no-op'd.
    //   2. cancelSnap() — the kCancelOverlayId callback — already
    //      dismisses whichever overlay is visible; the picker-takes-
    //      precedence ordering lives there.
    connect(m_shortcutManager.get(), &ShortcutManager::layoutPickerRequested, this, [this]() {
        if (!m_unifiedLayoutController) {
            return;
        }
        // The layout picker is screen-targeted — it picks the layout for a
        // screen — not window-targeted. Resolve cursor-first: the user's
        // intent is "the screen I am looking at". resolveShortcutScreenId
        // (focused-window-first) misroutes the picker to the wrong virtual
        // screen when the cursor rests on a different VS than the focused
        // window (e.g. just dropped a window on vs:1 while the focused
        // window is still on vs:0).
        const QString screenId = resolveCursorScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
        if (screenId.isEmpty()) {
            qCDebug(lcDaemon) << "LayoutPicker shortcut: no screen info";
            return;
        }
        m_unifiedLayoutController->setCurrentScreenName(screenId);
        updateLayoutFilterForScreen(screenId);
        m_overlayService->showLayoutPicker(screenId);
        if (m_windowDragAdaptor) {
            m_windowDragAdaptor->ensureCancelOverlayShortcutRegistered();
            // Picker navigation accels — registered while picker is
            // shown, dropped on dismiss. The unified PassiveOverlayShell
            // is kbd-None so the QML Shortcuts in LayoutPickerContent
            // can't fire; routing via KGlobalAccel is the replacement.
            // Lambdas capture the long-lived OverlayService — outlives
            // the registration window.
            auto* svc = m_overlayService.get();
            m_windowDragAdaptor->ensureLayoutPickerNavShortcutsRegistered(
                [svc](int dx, int dy) {
                    svc->pickerMoveSelection(dx, dy);
                },
                [svc] {
                    svc->pickerConfirmSelection();
                });
        }
    });
    // Snap-assist Escape: the unified PassiveOverlayShell is kbd-None
    // (the legacy SnapAssistOverlay's kbd-Exclusive QML Shortcut for
    // Escape no longer exists), so we register the global Escape
    // accelerator on every snap-assist show — cancelSnap() routes
    // Escape to hideSnapAssist() via the existing
    // isSnapAssistVisible() branch in WindowDragAdaptor::cancelSnap.
    // The matching unregister fires on snapAssistDismissed via
    // WindowDragAdaptor::onSnapAssistDismissed.
    connect(m_overlayService.get(), &IOverlayService::snapAssistShown, this,
            [this](const QString&, const PhosphorProtocol::EmptyZoneList&,
                   const PhosphorProtocol::SnapAssistCandidateList&) {
                if (m_windowDragAdaptor) {
                    m_windowDragAdaptor->ensureCancelOverlayShortcutRegistered();
                }
            });
    connect(m_overlayService.get(), &OverlayService::layoutPickerDismissed, this, [this]() {
        // Only release the Escape grab if no drag is currently active —
        // otherwise the in-progress drag's own cancel-overlay shortcut
        // would be torn down underneath it. WindowDragAdaptor's drag-end
        // path will release on its own when appropriate.
        if (m_windowDragAdaptor && !m_windowDragAdaptor->isDragActive()) {
            m_windowDragAdaptor->releaseCancelOverlayShortcut();
        }
        if (m_windowDragAdaptor) {
            m_windowDragAdaptor->releaseLayoutPickerNavShortcuts();
        }
    });
    connect(m_overlayService.get(), &OverlayService::layoutPickerSelected, this, [this](const QString& layoutId) {
        if (!m_unifiedLayoutController) {
            return;
        }
        // Check if screen is locked for its current mode. Route through
        // the resolver's `handleFor(screenId)` — it composes the live
        // (mode, desktop, activity) tuple via the bound IModeProvider /
        // IWorkspaceState adapters, so this site stops re-stitching the
        // 3-step cascade the resolver was introduced to collapse.
        QString screenId = m_unifiedLayoutController->currentScreenName();
        if (!screenId.isEmpty() && m_contextResolver) {
            if (m_contextResolver->isLocked(m_contextResolver->handleFor(screenId))) {
                showLockedPreviewOsd(screenId);
                return;
            }
        }
        // Screen name was already set when the picker opened.
        if (!m_unifiedLayoutController->applyLayoutById(layoutId)) {
            return;
        }
        resnapIfManualMode();
    });

    // Toggle layout lock shortcut — locks/unlocks current screen at screen-level for current mode
    connect(m_shortcutManager.get(), &ShortcutManager::toggleLayoutLockRequested, this, [this]() {
        // Screen-targeted (locks a screen's layout) — resolve cursor-first.
        // See layoutPickerRequested above for the rationale.
        const QString screenId = resolveCursorScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
        if (screenId.isEmpty() || !m_settings || !m_contextResolver) {
            return;
        }
        // Read the live mode through the resolver's frozen snapshot so this
        // site stops re-stitching (modeForScreen + Utils::contextLockKey)
        // — the resolver already composes the same Mode-typed lock key
        // internally via DaemonSettingsGateAdapter. We only need the
        // wire-encoded `key` here for the existing settings.setScreenLocked
        // mutation path, so the cast-and-compose stays — but the mode it
        // derives from is the resolver's authoritative value.
        const auto handle = m_contextResolver->handleFor(screenId);
        const int mode = static_cast<int>(handle.mode);
        QString key = Utils::contextLockKey(mode, screenId);
        // Lock at screen-level (desktop=0, activity="") so it applies to all desktops/activities
        // and matches the KCM's screen-level lock button
        bool wasLocked = m_settings->isScreenLocked(key);
        // Block settingsChanged during mutation — the signal triggers a
        // D-Bus relay, and external consumers (settings app) read from disk.
        // If the signal fires before save(), they read stale data.
        {
            QSignalBlocker blocker(m_settings.get());
            m_settings->setScreenLocked(key, !wasLocked);
        }
        m_settings->save();
        // Now that the file is written, notify external consumers
        if (m_settingsAdaptor) {
            Q_EMIT m_settingsAdaptor->settingsChanged();
        }

        if (wasLocked) {
            PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
            if (layout) {
                showLayoutOsd(layout, screenId);
            }
        } else {
            showLockedPreviewOsd(screenId);
        }
        qCInfo(lcDaemon) << "Toggle layout lock:" << (wasLocked ? "unlocked" : "locked") << "screen=" << screenId
                         << "mode=" << mode;
    });
}

void Daemon::pruneContextMapsForDesktop(int maxDesktop)
{
    auto it = m_lastAutotileOrders.begin();
    while (it != m_lastAutotileOrders.end()) {
        if (it.key().desktop > maxDesktop) {
            it = m_lastAutotileOrders.erase(it);
        } else {
            ++it;
        }
    }
}

void Daemon::pruneContextMapsForActivities(const QSet<QString>& validActivities)
{
    auto it = m_lastAutotileOrders.begin();
    while (it != m_lastAutotileOrders.end()) {
        if (!it.key().activity.isEmpty() && !validActivities.contains(it.key().activity)) {
            it = m_lastAutotileOrders.erase(it);
        } else {
            ++it;
        }
    }
}

bool Daemon::isScreenLockedForLayoutChange(const QString& screenId)
{
    // Route through the resolver — it composes the live (mode, desktop,
    // activity) tuple from the bound IModeProvider/IWorkspaceState, so
    // this site stops re-stitching the cascade Pass 1's sister sites
    // (`layoutPickerSelected`, `toggleLayoutLockRequested`) already
    // migrated. Null-guard the resolver for the same shutdown-window
    // reason as the other lock-check sites.
    if (!m_contextResolver) {
        return false;
    }
    if (m_contextResolver->isLocked(m_contextResolver->handleFor(screenId))) {
        showLockedPreviewOsd(screenId);
        return true;
    }
    return false;
}

void Daemon::handleCycleLayout(const QString& screenId, bool forward)
{
    if (!m_unifiedLayoutController) {
        return;
    }
    m_unifiedLayoutController->setCurrentScreenName(screenId);
    if (isScreenLockedForLayoutChange(screenId)) {
        return;
    }
    updateLayoutFilterForScreen(screenId);
    if (forward) {
        m_unifiedLayoutController->cycleNext();
    } else {
        m_unifiedLayoutController->cyclePrevious();
    }
    resnapIfManualMode();
}

void Daemon::migrateStartupScreenAssignments()
{
    if (!m_windowTrackingAdaptor || !m_screenManager) {
        return;
    }
    const auto vsConfigs = m_settings->virtualScreenConfigs();

    // "To virtual": for every physical screen Settings still subdivides, push
    // any bare-physId or stale VS assignments onto the current VS id set.
    QSet<QString> physWithSubdivisions;
    for (auto it = vsConfigs.constBegin(); it != vsConfigs.constEnd(); ++it) {
        if (it.value().hasSubdivisions()) {
            physWithSubdivisions.insert(it.key());
            QStringList vsIds = m_screenManager->virtualScreenIdsFor(it.key());
            m_windowTrackingAdaptor->service()->migrateScreenAssignmentsToVirtual(it.key(), vsIds,
                                                                                  m_screenManager.get());
        }
    }

    // "From virtual": symmetric pass for the daemon-was-down case. If the user
    // removed a screen's subdivisions while the daemon was offline, WTS state
    // on disk still holds "physId/vs:N" for windows on that screen; the live-
    // removal handler (onVirtualScreensReconfigured) never ran. Without this
    // pass those windows would survive into a fresh session under orphan
    // virtual ids and resnap/autotile/restore would mis-resolve them.
    //
    // The daemon supplies the policy (which physIds Settings still subdivides);
    // WTS owns the state-shape question (which physIds carry stale VS ids
    // anywhere in its bookkeeping). Coupling that to the daemon would force
    // every future addition of a screen-id-bearing state store to mirror an
    // orphan-scan update here — exactly the bug class this fix is closing.
    const QSet<QString> orphanPhysIds =
        m_windowTrackingAdaptor->service()->physicalScreensWithStaleVirtualAssignments(physWithSubdivisions);
    for (const QString& physId : orphanPhysIds) {
        m_windowTrackingAdaptor->service()->migrateScreenAssignmentsFromVirtual(physId);
    }
}

void Daemon::pruneAutotileOrdersForRemovedScreens(const QString& physicalScreenId)
{
    const QStringList currentVsIds =
        m_screenManager ? m_screenManager->virtualScreenIdsFor(physicalScreenId) : QStringList();
    QSet<QString> keepIds(currentVsIds.begin(), currentVsIds.end());
    // Also keep the physical ID itself (in case VS config was removed entirely)
    keepIds.insert(physicalScreenId);

    for (auto it = m_lastAutotileOrders.begin(); it != m_lastAutotileOrders.end();) {
        if (PhosphorIdentity::VirtualScreenId::extractPhysicalId(it.key().screenId) == physicalScreenId
            && !keepIds.contains(it.key().screenId)) {
            it = m_lastAutotileOrders.erase(it);
        } else {
            ++it;
        }
    }

    // The per-screen tiled-count cache (the placementChanged re-resolve gate) is
    // keyed by the same screen ids, so prune it on the same boundary to keep it
    // from accumulating dead entries across virtual-screen reconfigures.
    for (auto it = m_lastTiledCountByScreen.begin(); it != m_lastTiledCountByScreen.end();) {
        if (PhosphorIdentity::VirtualScreenId::extractPhysicalId(it.key()) == physicalScreenId
            && !keepIds.contains(it.key())) {
            it = m_lastTiledCountByScreen.erase(it);
        } else {
            ++it;
        }
    }
}

void Daemon::pruneAutotileOrdersForWindow(const QString& instanceId)
{
    if (instanceId.isEmpty() || m_lastAutotileOrders.isEmpty()) {
        return;
    }
    for (auto it = m_lastAutotileOrders.begin(); it != m_lastAutotileOrders.end();) {
        QStringList& order = it.value();
        const int before = order.size();
        order.erase(std::remove_if(order.begin(), order.end(),
                                   [&instanceId](const QString& wid) {
                                       return PhosphorIdentity::WindowId::extractInstanceId(wid) == instanceId;
                                   }),
                    order.end());
        if (order.isEmpty()) {
            it = m_lastAutotileOrders.erase(it);
        } else {
            if (order.size() != before) {
                qCDebug(lcDaemon) << "Pruned closed window" << instanceId
                                  << "from saved autotile order for screen=" << it.key().screenId
                                  << "desktop=" << it.key().desktop;
            }
            ++it;
        }
    }
}

void Daemon::onVirtualScreensReconfigured(const QString& physicalScreenId)
{
    // m_screenManager / m_layoutManager / m_virtualDesktopManager are
    // unique_ptrs constructed in the Daemon ctor's initializer list and
    // never nulled — by the time this signal handler fires they are valid.
    // m_windowTrackingAdaptor is constructed later in init() and may be null
    // if the signal somehow fires before construction (e.g. early Settings
    // load); guard those uses individually.
    const PhosphorScreens::VirtualScreenConfig config = m_screenManager->virtualScreenConfig(physicalScreenId);

    // Recalculate zone geometries inline for the affected screens FIRST so
    // that any PhosphorTiles::TilingState created by the upcoming updateAutotileScreens
    // call (and the resnap below) reads fresh zone bounds. The screenAdded
    // handler does the same inline recalc for newly-added physical screens.
    const QString activity = currentActivity();
    const QStringList affectedScreenIds = config.hasSubdivisions()
        ? m_screenManager->virtualScreenIdsFor(physicalScreenId)
        : QStringList{physicalScreenId};
    for (const QString& sid : affectedScreenIds) {
        // Per-output virtual desktops (#648): each screen its own desktop.
        const int desktop = currentDesktopForScreen(sid);
        PhosphorZones::Layout* screenLayout = m_layoutManager->layoutForScreen(sid, desktop, activity);
        if (screenLayout) {
            PhosphorZones::LayoutComputeService::recalculateSync(
                screenLayout, GeometryUtils::effectiveScreenGeometry(m_screenManager.get(), screenLayout, sid));
        }
    }

    // Clear stale resnap buffer — screen IDs have changed.
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->service()->clearResnapBuffer();
    }

    // Migrate window screen assignments symmetrically across the
    // subdivision/no-subdivision boundary:
    //   • subdivisions present → move physId (or stale VS ids from a prior
    //     config) onto the new VS id set;
    //   • subdivisions removed → collapse any lingering "physId/vs:N"
    //     assignments back to the bare physId so resnap, autotile, and
    //     pending-restore lookups don't dangle on orphan screen ids.
    // Migration is a no-op for windows already on a valid screen id in the
    // new config — those keep their stored screen.
    if (m_windowTrackingAdaptor) {
        if (config.hasSubdivisions()) {
            const QStringList vsIds = m_screenManager->virtualScreenIdsFor(physicalScreenId);
            m_windowTrackingAdaptor->service()->migrateScreenAssignmentsToVirtual(physicalScreenId, vsIds,
                                                                                  m_screenManager.get());
        } else {
            m_windowTrackingAdaptor->service()->migrateScreenAssignmentsFromVirtual(physicalScreenId);
        }
    }

    // Prune stale autotile order entries for old virtual screen IDs.
    pruneAutotileOrdersForRemovedScreens(physicalScreenId);

    // Re-derive the autotile screen set so the engine picks up new virtual
    // screen IDs (or drops removed ones) and creates/destroys TilingStates
    // accordingly. Without this, re-splitting a physical screen whose
    // assignments already exist on the VS IDs leaves the engine unaware of
    // the screens — onScreenGeometryChanged early-returns and no tiling
    // happens until something else (e.g. an assignment change) fires
    // layoutAssigned → updateAutotileScreens.
    updateAutotileScreens();

    // Resnap windows on this physical screen and any of its virtual children
    // to their stored zones. Uses calculateResnapFromCurrentAssignments which
    // is NOT gated on keepWindowsInZonesOnResolutionChange — VS reconfiguration
    // is user-initiated, not a passive resolution change. The physId filter is
    // VS-aware via Utils::belongsToPhysicalScreen. The vs_reconfigure-tagged
    // variant suppresses the kwin-effect snap-assist continuation so users
    // don't get a thumbnail picker popping up after every VS swap/rotate.
    if (m_snapAdaptor) {
        m_snapAdaptor->resnapForVirtualScreenReconfigure(physicalScreenId);
    }

    // Trigger debounced geometry recalculation for the rest of the system
    // (overlays, panel requery, autotile retile). Reuse the same debounced
    // path as physical screen geometry changes.
    if (m_screenManager ? m_screenManager->physicalScreenFor(physicalScreenId).isValid()
                        : (PhosphorScreens::ScreenIdentity::findByIdOrName(physicalScreenId) != nullptr)) {
        m_geometryUpdatePending = true;
        m_geometryUpdateTimer.start();
    }
}

void Daemon::onVirtualScreenRegionsChanged(const QString& physicalScreenId)
{
    // VS ID set is unchanged (swap/rotate/boundary resize). The heavy topology
    // work in onVirtualScreensReconfigured is all no-ops for this case, so we
    // short-circuit to just the steps that actually matter:
    //   1. Recompute zone geometries for each affected VS layout.
    //   2. Kick the snap-mode resnap (tagged vs_reconfigure → no snap-assist).
    // The autotile retile is handled by AutotileEngine's own
    // virtualScreenRegionsChanged handler — we deliberately do NOT call
    // updateAutotileScreens() here, because that would force a second retile
    // pass on top of the engine's own, producing the visible "move then
    // retile" double-movement reported on VS swap/rotate.

    const QString activity = currentActivity();
    const QStringList affectedScreenIds = m_screenManager->virtualScreenIdsFor(physicalScreenId);
    for (const QString& sid : affectedScreenIds) {
        // Per-output virtual desktops (#648): each screen its own desktop.
        const int desktop = currentDesktopForScreen(sid);
        PhosphorZones::Layout* screenLayout = m_layoutManager->layoutForScreen(sid, desktop, activity);
        if (screenLayout) {
            PhosphorZones::LayoutComputeService::recalculateSync(
                screenLayout, GeometryUtils::effectiveScreenGeometry(m_screenManager.get(), screenLayout, sid));
        }
    }

    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->service()->clearResnapBuffer();
    }
    if (m_snapAdaptor) {
        m_snapAdaptor->resnapForVirtualScreenReconfigure(physicalScreenId);
    }
}

} // namespace PlasmaZones
