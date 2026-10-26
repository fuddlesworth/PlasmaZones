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
#include <PhosphorEngine/DesktopRemap.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>
#include "core/interfaces/shaderregistry.h"
#include "config/settingsconfigstore.h"
#include <PhosphorZones/ZoneDetector.h>
#include <QProcess>
#include <QPointer>
#include <QSet>
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
                // The daemon's ScreenManager runs on the live QtPhysicalScreenSource,
                // so a tracked screen always carries a real QScreen — qscreen
                // is non-null here.
                m_overlayService->handleScreenAdded(screen.qscreen);
                // Recalculate zone geometries for all effective screen IDs on this physical screen.
                // No VS cache restore is needed: ScreenManager keeps m_virtualConfigs across a
                // disconnect, and Settings pushes updates through refreshVirtualConfigs().
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
                            GeometryUtils::effectiveScreenGeometry(m_screenManager.get(), screenLayout, sid),
                            PhosphorZones::GeometryUtils::screenScale(m_screenManager.get(), sid));
                    }
                }
                // Re-admit the output to the tiling engines now (F765): its
                // returning windows are classified against its mode, and the
                // effect's seed only pins its desktop (applyScreenDesktopSeed).
                updateEngineScreens();
                updateLayoutFilter();
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

                // Cancel any drag-insert preview on the output, park its
                // windows, then reap every engine's states on it (see
                // retireOutputPlacements).
                retireOutputPlacements(removedScreenId);

                // The removed output's strip-preview settle timers, including
                // every virtual sub-screen of it. Without this they are only
                // reaped in stop(), so a session that hot-plugs monitors
                // accumulates one dead timer per screen id ever seen and each
                // armed one would fire a card for an output that is gone.
                reapScrollingOsdSettleTimers(removedScreenId);

                // The removed output's cached tiled counts, every virtual
                // sub-screen included. The erase keeps a same-id replug from
                // swallowing its first placementChanged re-resolve (the map is
                // otherwise only pruned on VS reconfigure and in stop()).
                for (auto it = m_lastTiledCountByScreen.begin(); it != m_lastTiledCountByScreen.end();) {
                    it = PhosphorIdentity::VirtualScreenId::samePhysical(it.key(), removedScreenId)
                        ? m_lastTiledCountByScreen.erase(it)
                        : std::next(it);
                }
                // The two per-screen tab paint-override maps for the removed
                // output (virtual sub-screens included): the OverlayService's
                // drop-indicator map and the TilingAdaptor's tab map that the
                // KWin effect replays. The departing-screen loop that
                // ordinarily clears both is keyed on the engine's set, which
                // the prune above already shrank, so nothing else ever sweeps
                // them and a same-id replug would replay stale overrides.
                const auto removedPhysical = [&removedScreenId](const QString& screenId) {
                    return PhosphorIdentity::VirtualScreenId::samePhysical(screenId, removedScreenId);
                };
                if (m_overlayService) {
                    m_overlayService->clearScrollDropIndicatorOverridesWhere(removedPhysical);
                }
                if (m_tilingAdaptor) {
                    m_tilingAdaptor->clearScrollTabPaintOverridesWhere(removedPhysical);
                }

                // Invalidate cached EDID serial so a different monitor on this connector is detected
                PhosphorScreens::ScreenIdentity::invalidateEdidCache(removedName);

                // A layout's allowedScreens is left as the user set it: an
                // absent screen's id in it matches no screen while it is gone,
                // and the monitor coming back finds its restriction in place,
                // as its assignments and virtual-screen config do. Stripping
                // it here persisted the loss, and a layout restricted to that
                // monitor alone became one shown on every screen.

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
                // A rotation or a resolution change moves the screen-shape
                // context fields (orientation, width, height, aspect ratio)
                // with no rule-set revision bump. The registry's cached
                // resolvers self-heal through their key, but the geometry pass
                // above only recomputes zones: it never diffs the assignment,
                // so a shape-keyed layout rule would leave the engine mode, the
                // published active layout and the snapped windows on the old
                // verdict. Re-drive both, as a colour-scheme flip does.
                scheduleReconcileActiveAssignments();
                scheduleContextOverlayRefresh();
            });

    // Connect to available geometry changes (panels added/removed/resized)
    // This is reactive - the sensor windows automatically track panel changes
    // Uses debouncing to coalesce rapid changes into a single update
    connect(m_screenManager.get(), &PhosphorScreens::ScreenManager::availableGeometryChanged, this, [this] {
        m_geometryUpdatePending = true;
        m_geometryUpdateTimer.start();
    });

    // A scale change moves every zone of a layout whose fixed geometry counts
    // device pixels, at an unchanged logical rect, and nothing else. It fires
    // when an output's real scale differs from the one known before (at
    // startup that is a fractional scale, once the output's sensor maps and
    // the coarse integer one gives way to it), so a session with no such
    // layout has nothing to redo.
    //
    // A device-pixel layout shown on the output takes the debounced geometry
    // pass: the recalculation it requests carries the new scale, and the
    // overlays follow, as do snapped windows when the setting that keeps them
    // in their zones across a resolution change is on. One shown nowhere was
    // last computed against the primary screen for the previews (the
    // all-layouts recalculation in initLayoutAndSettingsWiring), so a change
    // of the primary's scale recalculates it the same way.
    connect(m_screenManager.get(), &PhosphorScreens::ScreenManager::logicalScaleChanged, this,
            [this](const PhosphorScreens::PhysicalScreen& screen) {
                if (!m_layoutManager || !m_screenManager) {
                    return;
                }
                // The screen width and height context fields count device
                // pixels, so a scale change at an unchanged logical size moves
                // them, and with them any size-keyed rule verdict. Re-driven
                // ahead of the device-pixel-layout gate below, which concerns
                // zone geometry only.
                scheduleReconcileActiveAssignments();
                scheduleContextOverlayRefresh();
                QList<PhosphorZones::Layout*> devicePixelLayouts;
                for (PhosphorZones::Layout* layout : m_layoutManager->layouts()) {
                    if (layout->fixedGeometryInDevicePixels()) {
                        devicePixelLayouts.append(layout);
                    }
                }
                if (devicePixelLayouts.isEmpty()) {
                    return;
                }
                // The connector name stands in for a screen with no identity,
                // as the screen adaptor's own handler of this signal reads it.
                const QString physId = screen.identifier.isEmpty() ? screen.name : screen.identifier;
                const QString activity = currentActivity();
                QSet<PhosphorZones::Layout*> shown;
                bool shownOnOutput = false;
                for (const QString& screenId : m_screenManager->effectiveScreenIds()) {
                    PhosphorZones::Layout* const layout =
                        m_layoutManager->layoutForScreen(screenId, currentDesktopForScreen(screenId), activity);
                    if (!layout) {
                        continue;
                    }
                    shown.insert(layout);
                    if (layout->fixedGeometryInDevicePixels()
                        && PhosphorIdentity::VirtualScreenId::samePhysical(screenId, physId)) {
                        shownOnOutput = true;
                    }
                }
                if (shownOnOutput) {
                    m_geometryUpdatePending = true;
                    m_geometryUpdateTimer.start();
                }
                QScreen* const primary = Utils::primaryScreen();
                if (!primary || PhosphorScreens::ScreenIdentity::identifierFor(primary) != physId) {
                    return;
                }
                const qreal primaryScale =
                    PhosphorZones::GeometryUtils::screenScale(m_screenManager.get(), physId, primary);
                for (PhosphorZones::Layout* layout : std::as_const(devicePixelLayouts)) {
                    if (!shown.contains(layout)) {
                        PhosphorZones::LayoutComputeService::recalculateSync(
                            layout, GeometryUtils::effectiveScreenGeometry(m_screenManager.get(), layout, primary),
                            primaryScale);
                    }
                }
            });

    // Don't pre-create overlay windows at startup. On Wayland with the layer-shell
    // QPA plugin this can cause visibility issues. Create on-demand in show() instead,
    // which also avoids the overlay flashing during login.
    qCInfo(lcDaemon) << "Overlay service: ready," << m_screenManager->screens().count()
                     << "screens available (windows created on-demand)";
}

void Daemon::connectDesktopActivity()
{
    // init() BEFORE start(), DELIBERATELY the opposite of the order
    // Workspaces::sharedManager uses. init()'s first refresh takes the
    // BLOCKING session-bus path only while m_running is still false, and the
    // daemon needs the current desktop to be known SYNCHRONOUSLY by the time
    // this function returns: the compositor announces its whole window set
    // (windowsOpenedBatch) as soon as the bridge comes up, and every engine
    // keys the context it adopts those windows into on
    // currentDesktopForScreen(). With start() first the refresh is issued
    // asynchronously, the batch wins the race, and the daemon adopts the
    // session onto desktop 1 — then the effect's per-output report flips the
    // context to the real desktop and the user is looking at an empty strip
    // ("No windows on the strip yet") until something forces a re-announce.
    // The shell orders these the other way because a 1 s stall there is on the
    // GUI thread; here it is on the daemon's start path, before any window has
    // been placed, and correctness outranks it.
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
                // The RELEASE half runs in [SEQ C½], after the context moves.
                applyStickyScreenPins(m_windowTrackingAdaptor, m_autotileEngine.get(), m_scrollEngine.get(),
                                      PhosphorEngine::StickyPinPhase::Acquire);
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
                // [SEQ C½] Release the pins of screens that no longer hold an
                // all-sticky window set, now that the context above has moved:
                // a pinned key does not follow the switch, so the pinned state
                // is still reachable, but the migration now lands on the
                // desktop entered (the point of running it after [SEQ C]).
                // Ahead of updateEngineScreens() so displaced-window releases
                // are done before the managed set is recomputed and announced.
                applyStickyScreenPins(m_windowTrackingAdaptor, m_autotileEngine.get(), m_scrollEngine.get(),
                                      PhosphorEngine::StickyPinPhase::Release);
                // [SEQ D] Per-screen layout/overlay resolution context needs no
                // push anymore: the layout registry (and the overlay service
                // through it) resolves per-output desktops via the injected
                // provider reading the VirtualDesktopManager, which this
                // handler's own signal already updated — one authority, no
                // mirror to lag (#648).
                // [SEQ E] Per-desktop assignments may differ — recompute autotile
                // screens, re-sync mode/filter, then refresh overlay geometry.
                updateEngineScreens();
                // [SEQ E¼] The desktop just entered gets its share of every window
                // spanning it (after the pins and [SEQ E]; see the helper), per EFFECTIVE id.
                reconcileMembershipsForScreens(m_tilingAdaptor, m_screenManager->virtualScreenIdsFor(screenId));
                // [SEQ E½] Re-announce the managed set UNCONDITIONALLY for this
                // report. The effect's staleness gate rejects any announce whose
                // per-screen desktop stamps disagree with what it last reported,
                // and its contract is "rejection converges: the daemon
                // re-announces for the desktop the effect has since reported".
                // updateEngineScreens only announces when the managed set
                // CHANGES, so on a global switch (the effect fans out one report
                // per output) the first report's announce carries the other
                // outputs' not-yet-updated desktops, gets rejected, and the
                // later reports change nothing — the promised follow-up never
                // came and the effect kept the old desktop's managed set. That
                // stale set is how focus-follows-mouse kept running on an
                // unassigned desktop (discussion #1028 follow-up). The adaptor
                // coalesces per event-loop turn and stamps desktops at emit
                // time, so the burst's last announce carries a fully consistent
                // map and is accepted.
                if (m_tilingAdaptor) {
                    m_tilingAdaptor->notifyEngineScreensChanged(/*isDesktopSwitch=*/true);
                }
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

    // The desktop an output shows as it comes (back): a seed, not a switch.
    connect(m_virtualDesktopManager.get(), &PhosphorWorkspaces::VirtualDesktopManager::screenDesktopSeeded, this,
            &Daemon::applyScreenDesktopSeed);

    // KWin's desktop list renumbered (a desktop removed, inserted or moved): one
    // old-to-new map for every number-keyed store. Ahead of desktopCountChanged
    // for a removal or a move; a growing count goes out first (F1022). The only
    // one of the desktop manager's two renumber signals connected
    // (desktopRemovedAt is its single-removal subset). desktop_renumber.cpp.
    connect(m_virtualDesktopManager.get(), &PhosphorWorkspaces::VirtualDesktopManager::desktopsRenumbered, this,
            &Daemon::onDesktopsRenumbered);
    // The records pushed before the change are the ones the renumber moves; a
    // push after this already carries the new numbering (WindowRegistry::remapDesktops).
    connect(m_virtualDesktopManager.get(), &PhosphorWorkspaces::VirtualDesktopManager::desktopListChanging, this,
            [this]() {
                if (m_windowRegistry) {
                    m_windowRegistry->beginDesktopRenumber();
                }
            });

    // Prune state past the count when desktops are removed (desktop_renumber.cpp).
    connect(m_virtualDesktopManager.get(), &PhosphorWorkspaces::VirtualDesktopManager::desktopCountChanged, this,
            &Daemon::onDesktopCountChanged);

    // Remember the desktop list for the next start (reconcileDesktopsAtStartup).
    connect(m_virtualDesktopManager.get(), &PhosphorWorkspaces::VirtualDesktopManager::desktopsChanged, this, [this]() {
        const QStringList ids = m_virtualDesktopManager->desktopIds();
        if (m_windowTrackingAdaptor && !ids.isEmpty()) {
            // A late first list that matches the last session's is followed by
            // no desktopsRenumbered, the signal that would end the restored-only
            // hold (onDesktopsRenumbered, F1045), so it ends here. The same two
            // lists the desktop manager compares, read before this replaces one.
            if (m_lateStartupRenumberUpTo
                && PhosphorEngine::DesktopRemap::fromIds(m_windowTrackingAdaptor->knownDesktopIds(), ids)
                       .isIdentity()) {
                m_lateStartupRenumberUpTo.reset();
                m_lateStartupRules.clear();
            }
            m_windowTrackingAdaptor->setKnownDesktopIds(ids);
        }
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

        // Prune the rules and engine state of removed activities (desktop_renumber.cpp).
        connect(m_activityManager.get(), &PhosphorWorkspaces::ActivityManager::activitiesChanged, this,
                &Daemon::onActivitiesChanged);

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
                    // The first activity known after a late start is no
                    // switch: the startup welcome OSD waits for exactly this
                    // change (finalizeStartup) and is shown for it below, in
                    // place of the switch OSD. Keyed on the welcome's own
                    // pending flag, not on the registry holding no activity:
                    // an empty report later on leaves the registry empty too,
                    // and the next real switch would then lose its OSD.
                    const bool welcome = m_welcomeOsdAwaitsActivity && !activityId.isEmpty();
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
                    // Pin sticky screens before changing activity context. The
                    // release half runs after the switch below: activity is
                    // the other half of the same context key, so an early
                    // release migrates onto the activity being left exactly
                    // as it would onto the desktop being left.
                    applyStickyScreenPins(m_windowTrackingAdaptor, m_autotileEngine.get(), m_scrollEngine.get(),
                                          PhosphorEngine::StickyPinPhase::Acquire);
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
                    // Release half, now that the activity context has moved,
                    // so a migration targets the activity being entered. Same
                    // placement as the desktop path's [SEQ C½].
                    applyStickyScreenPins(m_windowTrackingAdaptor, m_autotileEngine.get(), m_scrollEngine.get(),
                                          PhosphorEngine::StickyPinPhase::Release);
                    // Per-activity assignments may differ — recompute autotile screens
                    updateEngineScreens();
                    // Activity moves every screen's context at once; same ordering as [SEQ E¼].
                    reconcileMembershipsForScreens(m_tilingAdaptor, m_screenManager->effectiveScreenIds());
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

                    if (welcome) {
                        m_welcomeOsdAwaitsActivity = false;
                        showOsdForAllScreens(activityId);
                    } else {
                        showDesktopSwitchOsd(activityId);
                    }
                    // Refresh the active-assignment snapshot for the new activity
                    // context (no apply — the switch handles its own windows) so a
                    // later rule edit doesn't falsely re-resnap. Mirrors the
                    // per-screen desktop-switch handler above.
                    diffActiveAssignments();
                });
    }

    // What changed while the daemon was down, now that the handlers above are connected.
    reconcileDesktopsAtStartup();
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
        // <= 1, not == 0: the synthetic None row is store-independent and
        // keeps the count at >= 1 for every family (the same reason the
        // Templates arm asks the store directly), so a manual/autotile
        // vocabulary that is actually empty counts exactly 1 — the lone None
        // card, which cycling among is a silent no-op. The picker gate in
        // shortcuts_wiring.cpp uses the same threshold; the two must agree.
        if (templateStoreEmpty || visibleCount <= 1) {
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

    // The two per-screen tab paint-override maps sit on the same boundary,
    // the OverlayService's drop-indicator one and the TilingAdaptor's tab one
    // the KWin effect replays: the departing-screen loop that ordinarily
    // clears them cannot sweep a screen the engine no longer names, so
    // without this a dropped vs:N id keeps its overrides (and would replay
    // stale paint on a same-id return).
    const auto droppedSubScreen = [&](const QString& screenId) {
        return PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId) == physicalScreenId
            && !keepIds.contains(screenId);
    };
    if (m_overlayService) {
        m_overlayService->clearScrollDropIndicatorOverridesWhere(droppedSubScreen);
    }
    if (m_tilingAdaptor) {
        m_tilingAdaptor->clearScrollTabPaintOverridesWhere(droppedSubScreen);
    }

    // The strip-preview settle timers are keyed by the same screen ids and
    // belong on this boundary too. The screenRemoved reap cannot cover them:
    // it matches on samePhysical, and here the physical output SURVIVES, so a
    // physical match would reap the sub-screens that are still live. Without
    // this a dropped vs:N leaves its QTimer parented to the daemon until
    // shutdown, and an armed one goes on to fire for a screen that no longer
    // exists.
    reapScrollingOsdSettleTimersWhere(droppedSubScreen);
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
                screenLayout, GeometryUtils::effectiveScreenGeometry(m_screenManager.get(), screenLayout, sid),
                PhosphorZones::GeometryUtils::screenScale(m_screenManager.get(), sid));
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
                screenLayout, GeometryUtils::effectiveScreenGeometry(m_screenManager.get(), screenLayout, sid),
                PhosphorZones::GeometryUtils::screenScale(m_screenManager.get(), sid));
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
