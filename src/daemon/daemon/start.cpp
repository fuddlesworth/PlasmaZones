// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/daemon.h"
#include "helpers.h"
#include "daemon/overlayservice.h"
#include "daemon/controllers/unifiedlayoutcontroller.h"
#include "daemon/controllers/shortcutmanager.h"
#include "config/settingsconfigstore.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/LayoutComputeService.h>
#include <PhosphorZones/ScrollingTemplateStore.h> // count() (inline) for the cycle shortcut's empty-store gate
#include <PhosphorScreens/Manager.h>
#include <PhosphorContext/ContextResolver.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include "core/utils/geometryutils.h"
#include "core/platform/logging.h"
#include "core/utils/utils.h"
#include "dbus/layoutadaptor/layoutadaptor.h"
#include "dbus/settingsadaptor/settingsadaptor.h"
#include "dbus/shaderadaptor.h"
#include "dbus/compositorbridgeadaptor.h"
#include "dbus/controladaptor.h"
#include "dbus/overlayadaptor.h"
#include "dbus/zonedetectionadaptor.h"
#include "dbus/snapadaptor/snapadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "dbus/tilingadaptor/tilingadaptor.h"
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>
#include "core/interfaces/shaderregistry.h"
#include "config/settingsconfigstore.h"
#include <PhosphorZones/ZoneDetector.h>
#include <QProcess>
#include <QPointer>
#include "config/settings.h"
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
    // Ordering note, BY DESIGN: start() above already fired its initial
    // refreshVirtualConfigs pass (which emits virtualScreensChanged per
    // stored config) BEFORE this connect, so onVirtualScreensReconfigured
    // never runs for the startup configuration — the adaptors it touches do
    // not exist yet at start() time, and migrateStartupScreenAssignments
    // covers the assignment-migration half explicitly. Do not reorder the
    // connect above start().
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
                // Record the new screen's resolved assignment so a later
                // (unrelated) rule edit doesn't diff it as a change and
                // spuriously resnap it — the screen-add path lays it out here.
                diffActiveAssignments();
                // Topology changed, so every scroll park position derived
                // from the OUTPUT UNION is stale: a monitor attached below
                // raises the union bottom, and parks committed against the
                // old union would sit inside the new output. The debounced
                // geometry pass retiles every active scroll screen; the
                // sensor/geometry signals usually fire it anyway, but this
                // makes the retile unconditional rather than incidental.
                m_geometryUpdatePending = true;
                m_geometryUpdateTimer.start();
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
                // (The engines' own per-screen desktop maps are cleared inside
                // their pruneStatesForRemovedScreen calls below — this handler
                // does NOT call updateEngineScreens.) The VDM
                // and layout registry are physical-id keyed (the effect reports
                // physical output ids), matching removedScreenId. The overlay service
                // delegates to the layout registry, so clearing it there suffices.
                if (m_virtualDesktopManager) {
                    // The VDM is the ONE per-output desktop authority: the
                    // layout registry (and the overlay service through it)
                    // resolves per-screen desktops via the injected provider
                    // that reads this manager, so this removal covers them.
                    m_virtualDesktopManager->removeScreenDesktop(removedScreenId);
                }

                // A live drag-insert preview on the departing output (any
                // virtual sub-screen of it) must unwind BEFORE the prunes
                // tear its states down — the scroll engine's prune cancels
                // internally, the autotile prune does not, and the daemon
                // path must not depend on which engine holds the preview.
                if (m_windowDragAdaptor) {
                    m_windowDragAdaptor->cancelDragInsertPreviewsForScreen(removedScreenId);
                }

                // All three engines need the explicit whole-output reap:
                // snap's per-(screen,desktop,activity) stores are created
                // lazily on placement, and the two tiling engines'
                // updateEngineScreens sweep only reaps CURRENT-context
                // states, so sibling-context states (other desktops or
                // activities) of the removed output would leak and
                // resurface ghost tiles on replug. Each engine matches
                // every virtual sub-screen of the removed physical id.
                if (m_snapEngine) {
                    m_snapEngine->pruneStatesForRemovedScreen(removedScreenId);
                }
                if (m_autotileEngine) {
                    m_autotileEngine->pruneStatesForRemovedScreen(removedScreenId);
                }
                if (m_scrollEngine) {
                    m_scrollEngine->pruneStatesForRemovedScreen(removedScreenId);
                }

                // The removed output's strip-preview settle timers, including
                // every virtual sub-screen of it. Without this they are only
                // reaped in stop(), so a session that hot-plugs monitors
                // accumulates one dead timer per screen id ever seen and each
                // armed one would fire a card for an output that is gone.
                reapScrollingOsdSettleTimers(removedScreenId);

                // The removed output's cached tab-strip payloads and tiled
                // counts, every virtual sub-screen included. The engine-side
                // prune now emits "[]" per strip screen, which also removes
                // these; the direct erase covers payloads cached for screens
                // whose engine state never materialised, and the tiled-count
                // erase keeps a same-id replug from swallowing its first
                // placementChanged re-resolve (both maps are otherwise only
                // pruned on VS reconfigure and in stop()).
                for (auto it = m_lastScrollTabStripsJson.begin(); it != m_lastScrollTabStripsJson.end();) {
                    it = PhosphorIdentity::VirtualScreenId::samePhysical(it.key(), removedScreenId)
                        ? m_lastScrollTabStripsJson.erase(it)
                        : std::next(it);
                }
                for (auto it = m_lastTiledCountByScreen.begin(); it != m_lastTiledCountByScreen.end();) {
                    it = PhosphorIdentity::VirtualScreenId::samePhysical(it.key(), removedScreenId)
                        ? m_lastTiledCountByScreen.erase(it)
                        : std::next(it);
                }
                // The OverlayService's strip model and paint-override maps for
                // the removed output (virtual sub-screens included): the
                // departing-screen loop keys on the engine's set, which the
                // prune above already shrank, so nothing else ever sweeps
                // them and a same-id replug would replay stale overrides.
                if (m_overlayService) {
                    m_overlayService->clearScrollTabStateWhere([&removedScreenId](const QString& screenId) {
                        return PhosphorIdentity::VirtualScreenId::samePhysical(screenId, removedScreenId);
                    });
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
                // Drop the removed screen from the assignment snapshot so it
                // doesn't linger as a stale entry (kept consistent with the
                // add / context-switch / apply refresh points), and from the
                // announce ledger — a replugged monitor must not carry a
                // stale "already saw template X" verdict across the unplug.
                diffActiveAssignments();
                // Keyed by EFFECTIVE id like the two maps above, while this
                // handler carries the PHYSICAL one — match on the physical
                // prefix so a subdivided output's vs:N entries go too.
                for (auto it = m_lastAnnouncedTemplateByScreen.begin(); it != m_lastAnnouncedTemplateByScreen.end();) {
                    it = PhosphorIdentity::VirtualScreenId::samePhysical(it.key(), removedScreenId)
                        ? m_lastAnnouncedTemplateByScreen.erase(it)
                        : std::next(it);
                }
                // Same union-park staleness rule as the screenAdded tail: a
                // removed bottom monitor lowers the output union, so every
                // scroll park must re-derive against the new topology.
                m_geometryUpdatePending = true;
                m_geometryUpdateTimer.start();
            });

    connect(m_screenManager.get(), &PhosphorScreens::ScreenManager::screenGeometryChanged, this,
            [this](const PhosphorScreens::PhysicalScreen& screen) {
                // Same cancel-before-context-change rule as the screen-removed,
                // desktop-switch and activity handlers: a rotation or a
                // resolution change reshapes the strip this output's live
                // preview was detached into, so the view slides under a
                // stationary cursor and the drag's detach-once invariant no
                // longer holds. Scoped to the output that changed, keyed on
                // its identifier the way the removal path is, so a rotation of
                // monitor A leaves monitor B's preview alone.
                if (m_windowDragAdaptor) {
                    m_windowDragAdaptor->cancelDragInsertPreviewsForScreen(screen.identifier);
                }
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
                // desktop changes, else cancel/commit would hit the wrong state.
                // Scoped to the ONE output that switched (this signal is
                // per-output): a desktop flip on monitor A must not snap
                // monitor B's live preview back mid-drag. The activity twin
                // below stays unconditional — an activity switch is global.
                if (m_windowDragAdaptor) {
                    m_windowDragAdaptor->cancelDragInsertPreviewsForScreen(screenId);
                }
                // [SEQ B] Pin screens where all autotiled windows are sticky BEFORE
                // changing the desktop context, so currentKeyForScreen() still
                // resolves existing TilingStates ("virtualdesktopsonlyonprimary").
                if (m_windowTrackingAdaptor) {
                    if (auto* service = m_windowTrackingAdaptor->service()) {
                        const auto sticky = [service](const QString& windowId) {
                            return service->isWindowSticky(windowId);
                        };
                        if (m_autotileEngine) {
                            m_autotileEngine->updateStickyScreenPins(sticky);
                        }
                        if (m_scrollEngine) {
                            m_scrollEngine->updateStickyScreenPins(sticky);
                        }
                    }
                }
                // [SEQ C] Set THIS screen's engine desktop context (pure per-screen
                // swap, no state migration) BEFORE updateEngineScreens() so the
                // engine resolves TilingStates under the new (screen, desktop) key.
                if (m_autotileEngine) {
                    m_autotileEngine->setCurrentDesktopForScreen(screenId, desktop);
                }
                // Feed the SAME per-output desktop into the snap engine so its
                // per-(screen,desktop,activity) key tracker resolves the right store
                // (symmetric with autotile; per-monitor keying is the #724 fix).
                if (m_snapEngine) {
                    m_snapEngine->setCurrentDesktopForScreen(screenId, desktop);
                }
                if (m_scrollEngine) {
                    m_scrollEngine->setCurrentDesktopForScreen(screenId, desktop);
                }
                // [SEQ D] Per-screen layout/overlay resolution context needs no
                // push anymore: the layout registry (and the overlay service
                // through it) resolves per-output desktops via the injected
                // provider reading the VirtualDesktopManager, which this
                // handler's own signal already updated — one authority, no
                // mirror to lag (#648).
                // [SEQ E] Per-desktop assignments may differ — recompute autotile
                // screens, re-sync mode/filter, then refresh overlay geometry.
                updateEngineScreens();
                // A desktop whose assignment is snapping demotes the screen out
                // of tiling in the recompute above; nothing further on this path
                // consumes the preserved snap-ZONE half, so put the released
                // windows back on their recorded zones instead of leaving them
                // sitting at their tile rects.
                flushPendingSnapZoneRestores();
                syncModeFromAssignments();
                // Re-evaluate the context gates for the new (screen, desktop)
                // pair: this per-output path never goes through
                // setCurrentVirtualDesktop (whose global change gate is dead
                // under per-output desktops), so without an explicit call a
                // switch onto a context-disabled desktop left an
                // already-visible overlay or zone selector up on that screen.
                m_overlayService->hideDisabledAndRefresh();
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
                // use KDE's desktop UUIDs instead of 1-based numbers. The same limitation
                // applies to the per-screen CURRENT-desktop maps (VDM / layout registry /
                // engine context): they are corrected by the effect's next per-output
                // desktop report rather than re-derived here.
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

                // A live preview must unwind BEFORE the prunes: autotile's
                // cancel resolves its state via a create-if-missing lookup,
                // so a cancel arriving AFTER the prune would resurrect a
                // state for the deleted desktop (the two context-switch
                // handlers carry the same ordering).
                if (m_windowDragAdaptor) {
                    m_windowDragAdaptor->cancelDragInsertPreviews();
                }

                // Desktop numbers are 1-based. Any state with desktop > newCount is
                // stale. desktopsWithActiveState() returns the desktops currently
                // holding state — filter for anything past the new count and prune,
                // avoiding the arbitrary upper bound of the old newCount+20 sweep. All
                // THREE engines carry their own stores, so prune each.
                for (PhosphorEngine::PlacementEngineBase* engine :
                     {m_autotileEngine.get(), m_snapEngine.get(), m_scrollEngine.get()}) {
                    if (!engine) {
                        continue;
                    }
                    const QSet<int> active = engine->desktopsWithActiveState();
                    for (int d : active) {
                        if (d > newCount) {
                            engine->pruneStatesForDesktop(d);
                        }
                    }
                }
                // Prune fallback assignment maps
                pruneContextMapsForDesktop(newCount);

                // No diffActiveAssignments() here, deliberately. The published
                // active-layout map is keyed by screen and resolved against each
                // screen's own current desktop, and every screen whose desktop
                // number this removal actually moved is announced through
                // screenDesktopChanged instead: VirtualDesktopManager clamps the
                // out-of-range entries (clampScreenDesktopsToCount) BEFORE it
                // emits desktopCountChanged, so their per-screen handler has
                // already re-diffed by the time we run. A mid-list removal that
                // renumbers a still-in-range screen (desktop 3 becomes 2) leaves
                // the per-screen map holding the stale number until the effect
                // re-reports that output's desktop; a diff here would read the
                // same stale number and could not fix it. pruneContextMapsForDesktop
                // touches only m_lastEngineOrders, which no resolution reads.
            });

    // Set initial virtual desktop on components that maintain their own copy
    // (WindowDragAdaptor reads from PhosphorZones::LayoutRegistry directly via resolveLayoutForScreen())
    const int initialDesktop = currentDesktop();
    m_overlayService->setCurrentVirtualDesktop(initialDesktop);
    m_layoutManager->setCurrentVirtualDesktop(initialDesktop);
    if (m_autotileEngine) {
        m_autotileEngine->setCurrentDesktop(initialDesktop);
    }
    if (m_snapEngine) {
        m_snapEngine->setCurrentDesktop(initialDesktop);
    }
    if (m_scrollEngine) {
        m_scrollEngine->setCurrentDesktop(initialDesktop);
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

            // Same cancel-before-prune ordering as desktopCountChanged: an
            // after-the-fact cancel would re-create state for a removed
            // activity through autotile's create-if-missing lookup.
            if (m_windowDragAdaptor) {
                m_windowDragAdaptor->cancelDragInsertPreviews();
            }

            // All three engines carry their own per-(screen,desktop,activity)
            // stores, so prune removed activities from each.
            for (PhosphorEngine::PlacementEngineBase* engine :
                 {m_autotileEngine.get(), m_snapEngine.get(), m_scrollEngine.get()}) {
                if (engine) {
                    engine->pruneStatesForActivities(activities);
                }
            }
            pruneContextMapsForActivities(validSet);
        });

        // Set initial activity on components that maintain their own copy.
        // Layout registry FIRST: the overlay's setter runs a refresh pass
        // that resolves assignments through the registry, so updating the
        // overlay first would render one pass against the old activity.
        const QString initialActivity = m_activityManager->currentActivity();
        m_layoutManager->setCurrentActivity(initialActivity);
        m_overlayService->setCurrentActivity(initialActivity);
        if (m_autotileEngine) {
            m_autotileEngine->setCurrentActivity(initialActivity);
        }
        if (m_snapEngine) {
            m_snapEngine->setCurrentActivity(initialActivity);
        }
        if (m_scrollEngine) {
            m_scrollEngine->setCurrentActivity(initialActivity);
        }

        // Connect activity changes: update all components
        connect(m_activityManager.get(), &PhosphorWorkspaces::ActivityManager::currentActivityChanged, this,
                [this](const QString& activityId) {
                    // Registry before overlay — see the initial-activity note
                    // above (the overlay's refresh resolves via the registry).
                    m_layoutManager->setCurrentActivity(activityId);
                    m_overlayService->setCurrentActivity(activityId);
                    if (m_unifiedLayoutController) {
                        m_unifiedLayoutController->setCurrentActivity(activityId);
                    }
                    // Activity switch invalidates the placement-state context — cancel
                    // any active drag-insert preview before the engines' activity
                    // changes. Unconditional on purpose: an activity switch is
                    // global, unlike the per-output desktop switch above.
                    if (m_windowDragAdaptor) {
                        m_windowDragAdaptor->cancelDragInsertPreviews();
                    }
                    // Pin sticky screens before changing activity context
                    // (null-guarded service, matching every other daemon
                    // ->service() consumer).
                    if (m_windowTrackingAdaptor) {
                        if (auto* service = m_windowTrackingAdaptor->service()) {
                            const auto sticky = [service](const QString& windowId) {
                                return service->isWindowSticky(windowId);
                            };
                            if (m_autotileEngine) {
                                m_autotileEngine->updateStickyScreenPins(sticky);
                            }
                            if (m_scrollEngine) {
                                m_scrollEngine->updateStickyScreenPins(sticky);
                            }
                        }
                    }
                    // Set engine's activity context BEFORE updateEngineScreens()
                    if (m_autotileEngine) {
                        m_autotileEngine->setCurrentActivity(activityId);
                    }
                    if (m_snapEngine) {
                        m_snapEngine->setCurrentActivity(activityId);
                    }
                    if (m_scrollEngine) {
                        m_scrollEngine->setCurrentActivity(activityId);
                    }
                    // Per-activity assignments may differ — recompute autotile screens
                    updateEngineScreens();
                    // Same reason as the per-screen desktop switch above: this
                    // path has no consumer for the preserved snap-ZONE half, so
                    // windows released by an activity's snapping assignment need
                    // their zones restored here.
                    flushPendingSnapZoneRestores();
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

void Daemon::pruneContextMapsForDesktop(int maxDesktop)
{
    auto it = m_lastEngineOrders.begin();
    while (it != m_lastEngineOrders.end()) {
        if (it.key().desktop > maxDesktop) {
            it = m_lastEngineOrders.erase(it);
        } else {
            ++it;
        }
    }
}

void Daemon::pruneContextMapsForActivities(const QSet<QString>& validActivities)
{
    auto it = m_lastEngineOrders.begin();
    while (it != m_lastEngineOrders.end()) {
        if (!it.key().activity.isEmpty() && !validActivities.contains(it.key().activity)) {
            it = m_lastEngineOrders.erase(it);
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
    // Layout cycling is meaningless on a screen whose engine has no layout
    // concept (scrolling) — answer with feedback instead of applying a snap
    // layout there (the old one-way-door-out-of-scrolling policy).
    const LayoutSupport support = layoutSupportForScreen(screenId);
    if (support == LayoutSupport::None) {
        showLayoutsUnavailableOsd(screenId);
        return;
    }
    // Bind the screen, THEN push the LIVE capability, so applyEntry's template
    // branch routes on the engine that actually owns the screen it is about to
    // act on (same order as the picker and quick-slot handlers in
    // shortcuts_wiring.cpp). Pushing ahead of the None bail left the
    // controller describing a screen this handler then refused to act on.
    m_unifiedLayoutController->setCurrentScreenName(screenId);
    m_unifiedLayoutController->setCurrentLayoutSupport(support);
    if (isScreenLockedForLayoutChange(screenId)) {
        return;
    }
    updateLayoutFilterForScreen(screenId);
    // Same empty-vocabulary answer the picker gives (shortcuts_wiring.cpp):
    // an empty candidate list means cycling would silently do nothing, on
    // ANY screen. When the overlay's include resolution picked the template
    // family, the store is asked directly — the synthetic None row keeps
    // visibleLayoutCount >= 1 there even with zero templates, so the count
    // alone cannot detect an empty store. Gated on the overlay's OWN
    // resolution rather than on `support`: the include resolution ANDs the
    // live capability with isScrolling(assignmentId), so a Templates-capable
    // screen carrying a manual assignment resolves to the manual family and
    // its rows must still cycle. Same gate shape and same store-keyed OSD
    // split as the picker so the two shortcuts never disagree.
    if (m_overlayService) {
        const int visibleCount = m_overlayService->visibleLayoutCount(screenId);
        const bool templateStoreEmpty = m_overlayService->screenResolvesToTemplates(screenId)
            && (!m_scrollingTemplateStore || m_scrollingTemplateStore->count() == 0);
        if (templateStoreEmpty || visibleCount == 0) {
            if (templateStoreEmpty) {
                qCDebug(lcDaemon) << "Layout cycle: no templates in the store for screen" << screenId;
                if (navigationOsdAllowed(screenId)) {
                    m_overlayService->showNavigationOsd(false, QStringLiteral("layout"), QStringLiteral("no_templates"),
                                                        QString(), QString(), screenId);
                }
            } else {
                showLayoutsUnavailableOsd(screenId);
            }
            return;
        }
    }
    if (forward) {
        m_unifiedLayoutController->cycleNext();
    } else {
        m_unifiedLayoutController->cyclePrevious();
    }
    resnapIfManualMode();
}

void Daemon::migrateStartupScreenAssignments()
{
    // m_settings and service() are unguarded below by the same invariant the
    // rest of the file relies on: both are ctor-owned / adaptor-constructed
    // and never null while the adaptor exists (autotile.cpp documents it).
    if (!m_windowTrackingAdaptor || !m_screenManager || !m_windowTrackingAdaptor->service()) {
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

void Daemon::pruneEngineOrdersForRemovedScreens(const QString& physicalScreenId)
{
    const QStringList currentVsIds =
        m_screenManager ? m_screenManager->virtualScreenIdsFor(physicalScreenId) : QStringList();
    QSet<QString> keepIds(currentVsIds.begin(), currentVsIds.end());
    // Also keep the physical ID itself (in case VS config was removed entirely)
    keepIds.insert(physicalScreenId);

    for (auto it = m_lastEngineOrders.begin(); it != m_lastEngineOrders.end();) {
        if (PhosphorIdentity::VirtualScreenId::extractPhysicalId(it.key().screenId) == physicalScreenId
            && !keepIds.contains(it.key().screenId)) {
            it = m_lastEngineOrders.erase(it);
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

    // Same boundary, same reason, for the raw tab-strip payload cache: a dead
    // entry there is not merely a leak, it makes refreshScrollTabEnrichment
    // re-parse and re-push a departed screen on every title tick.
    for (auto it = m_lastScrollTabStripsJson.begin(); it != m_lastScrollTabStripsJson.end();) {
        if (PhosphorIdentity::VirtualScreenId::extractPhysicalId(it.key()) == physicalScreenId
            && !keepIds.contains(it.key())) {
            it = m_lastScrollTabStripsJson.erase(it);
        } else {
            ++it;
        }
    }

    // And the template-announce ledger, on the same boundary. Un-subdividing an
    // output is reachable with no unplug at all, so without this its vs:N
    // verdicts outlive the screens they were recorded for, and a later
    // re-subdivision inherits a "already announced template X" claim for a
    // context the user has not seen since.
    for (auto it = m_lastAnnouncedTemplateByScreen.begin(); it != m_lastAnnouncedTemplateByScreen.end();) {
        if (PhosphorIdentity::VirtualScreenId::extractPhysicalId(it.key()) == physicalScreenId
            && !keepIds.contains(it.key())) {
            it = m_lastAnnouncedTemplateByScreen.erase(it);
        } else {
            ++it;
        }
    }

    // The OverlayService's per-screen strip model and paint-override maps sit
    // on the same boundary: the departing-screen loop cannot sweep a screen
    // the engine no longer names, so without this a dropped vs:N id keeps its
    // overrides (and would replay stale paint on a same-id return).
    if (m_overlayService) {
        m_overlayService->clearScrollTabStateWhere([&](const QString& screenId) {
            return PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId) == physicalScreenId
                && !keepIds.contains(screenId);
        });
    }
}

void Daemon::pruneEngineOrdersForWindow(const QString& instanceId)
{
    if (instanceId.isEmpty() || m_lastEngineOrders.isEmpty()) {
        return;
    }
    for (auto it = m_lastEngineOrders.begin(); it != m_lastEngineOrders.end();) {
        QStringList& order = it.value();
        const int before = order.size();
        order.erase(std::remove_if(order.begin(), order.end(),
                                   [&instanceId](const QString& wid) {
                                       return PhosphorIdentity::WindowId::extractInstanceId(wid) == instanceId;
                                   }),
                    order.end());
        if (order.isEmpty()) {
            it = m_lastEngineOrders.erase(it);
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

    // Cancel any live drag-insert preview on this output BEFORE the screen id
    // set is re-derived, the same ordering rule the four sibling
    // context-change handlers document (screenRemoved, screenDesktopChanged,
    // the activity switch and the mode reassignment). Subdividing or
    // un-subdividing an output rewrites its screen ids, so a preview holding
    // "DP-1" while the topology moves everything to "DP-1/vs:0" is left with
    // captured keys nothing resolves, and neither its commit nor its cancel
    // can put the window anywhere. Scoped to the reconfigured output because
    // samePhysical covers the physical id and every virtual child of it.
    if (m_windowDragAdaptor) {
        m_windowDragAdaptor->cancelDragInsertPreviewsForScreen(physicalScreenId);
    }

    // Recalculate zone geometries inline for the affected screens FIRST so
    // that any PhosphorTiles::TilingState created by the upcoming updateEngineScreens
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
    pruneEngineOrdersForRemovedScreens(physicalScreenId);

    // Re-derive the autotile screen set so the engine picks up new virtual
    // screen IDs (or drops removed ones) and creates/destroys TilingStates
    // accordingly. Without this, re-splitting a physical screen whose
    // assignments already exist on the VS IDs leaves the engine unaware of
    // the screens — onScreenGeometryChanged early-returns and no tiling
    // happens until something else (e.g. an assignment change) fires
    // layoutAssigned → updateEngineScreens.
    updateEngineScreens();
    // Screens dropped by the new VS topology release their windows above. The
    // reconfigure resnap below works off stored zone assignments and does not
    // consume the preserved snap-ZONE half, so emit it here.
    flushPendingSnapZoneRestores();

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

    // Re-prime the active-assignment snapshot for the NEW virtual screen id
    // set — same rationale as the screenAdded / screenRemoved tails: a VS
    // reconfigure replaces the physical id with vs:N children (or back) in
    // effectiveScreenIds, and an un-primed id always diffs as changed, so
    // the next unrelated rule edit would spuriously resnap every new VS.
    // The refresh also republishes the active-layout map under the new id
    // keyspace, or the effect's ActiveLayout cache keeps matching on screen
    // ids that no longer exist.
    diffActiveAssignments();
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
    // updateEngineScreens() here, because that would force a second retile
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
    // The scroll engine subscribes to no ScreenManager signal of its own
    // (unlike autotile's virtualScreenRegionsChanged handler), so its
    // affected strips must be retiled here or their columns keep stale
    // widths/offsets until an unrelated retile. The retile relays out of
    // the STORED override map, and the per-context rule params and gaps that
    // feed it re-resolve on the push, not on the retile. Native templates
    // hold fractions, so the template half of the push does not depend on
    // geometry. updateScrollingScreens' per-pass push plus its identical-set
    // retile covers both needs in one call, keeping this handler's
    // single-retile property.
    if (m_scrollEngine && !m_scrollEngine->activeScreens().isEmpty()) {
        updateScrollingScreens(m_scrollEngine->activeScreens());
    }
}

} // namespace PlasmaZones
