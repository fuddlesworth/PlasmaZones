// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/daemon.h"
#include "helpers.h"

#include <QGuiApplication>
#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrent>
#include <QScreen>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QSet>

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/PhosphorCurve.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include "daemon/overlayservice.h"
#include "daemon/controllers/unifiedlayoutcontroller.h"
#include "daemon/controllers/shortcutmanager.h"

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/ScrollingTemplateStore.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/ZonesLayoutSource.h>
#include <PhosphorZones/LayoutComputeService.h>
#include <PhosphorZones/ZoneDetector.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/AutotileLayoutSourceFactory.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include <PhosphorContext/ContextResolver.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorRules/ExclusionRules.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleStore.h>

#include "config/configbackends.h"
#include "config/configdefaults.h"
#include "config/settingsconfigstore.h"
#include "config/settings.h"
#include "core/types/constants.h"
#include "core/resolve/screenmoderouter.h"
#include "core/utils/geometryutils.h"
#include "core/utils/utils.h"
#include "core/platform/logging.h"
#include "core/interfaces/shaderregistry.h"
#include "dbus/layoutadaptor/layoutadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "dbus/snapadaptor/snapadaptor.h"

namespace PlasmaZones {

void Daemon::initLayoutAndSettingsWiring()
{
    // Every sender wired below (m_settings, m_layoutManager, the value-member
    // timers) survives stop(), and init() can re-run — drop the handles we
    // installed last time so a stop() -> init() -> start() cycle cannot stack
    // duplicate handlers (double mode-transition + double gap resnap per
    // save). Exact handles, not (sender, signal, receiver) sweeps: other call
    // sites (connectLayoutSignals, overlayservice) own handlers on the same
    // signals.
    for (const QMetaObject::Connection& c : std::as_const(m_layoutSettingsWiringConnections)) {
        disconnect(c);
    }
    m_layoutSettingsWiringConnections.clear();
    // Wire the level-1 (global) cascade tier as two pass-through
    // providers — snap default layout id and autotile default algorithm
    // id — symmetric in shape and each gated on its own enabled flag.
    // The library decides precedence (snap > autotile when both are
    // non-empty); the daemon does not arbitrate mode here. When
    // snappingEnabled is false the snap provider returns empty, so
    // the cascade naturally resolves autotile defaults for unassigned
    // contexts (fixes #368 without baking engine specifics into the
    // composition root).
    //
    // Lifetime: m_settings is declared AFTER m_layoutManager in
    // daemon.h, so reverse-order member destruction tears m_settings
    // down FIRST. The lambdas capture `this` and dereference m_settings,
    // so any cascade query during member-destruction would UAF without
    // the explicit teardown in stop() (which clears EVERY provider
    // installed in this function — see lifecycle.cpp, and add the
    // matching clear there when installing another one)
    // before any unique_ptr member runs its destructor, plus the null
    // checks below as a belt-and-suspenders guard against future
    // refactors that reset m_settings explicitly. NOTE: snap with
    // defaultLayoutId="" silently falls through to the autotile branch
    // — see test_layoutmanager_assignment.cpp
    // testLevel1Default_snapEnabledEmptyId_autotileEnabled_autotileWins
    // for the pinned behaviour.
    m_layoutManager->setDefaultLayoutIdProvider([this]() -> QString {
        if (!m_settings || !m_settings->snappingEnabled()) {
            return QString();
        }
        const QString id = m_settings->defaultLayoutId();
        if (id.isEmpty()) {
            return QString();
        }
        // The reserved no-layout word is a valid configured default ("no
        // default at all" — the library card's Clear Default) and is not a
        // UUID, so it must pass through BEFORE the dead-id check below, which
        // would degrade it to empty and let the level-1 cascade fall through
        // to the autotile default — the exact silent-default creep the
        // sentinel exists to stop. The registry synthesizes a Snapping entry
        // carrying the word, and its existing opt-out arms answer "no layout".
        if (id == PhosphorZones::NoSnappingLayout) {
            return id;
        }
        // Refuse an id whose layout no longer exists. Deleting a layout does not
        // clear this setting, so the level-1 default would otherwise keep handing
        // out a dead UUID: every unassigned screen resolves to it, which means the
        // daemon publishes it as that screen's active layout and an
        // `ActiveLayout Equals <deleted uuid>` rule keeps matching a layout the
        // user removed. Falling through to empty puts those screens in the
        // no-resolvable-layout state the cascade already handles.
        //
        // This duplicates the same dead-id check inside LayoutRegistry::defaultLayout(),
        // and has to: that one FALLS BACK (to the active layout, then the first
        // registered one) where this provider must return empty, because the
        // provider's answer is the published ActiveLayout value.
        //
        // Known asymmetry, accepted: layoutForScreen() still routes through
        // defaultLayout()'s fallback, so after the configured default is deleted
        // a screen keeps live zones from the first registered layout while
        // ActiveLayout publishes empty for it. Zones stay usable, and no rule
        // matches a layout the user deleted.
        //
        // Cost: one QUuid parse plus an O(layouts) layoutById scan per cascade
        // miss, which the drag path hits per resolve. The empty-id early return
        // above keeps the common unconfigured case off this path entirely.
        const auto uuid = Utils::parseUuid(id);
        if (!uuid || !m_layoutManager->layoutById(*uuid)) {
            return QString();
        }
        return id;
    });
    m_layoutManager->setDefaultAutotileAlgorithmProvider([this]() {
        if (!m_settings || !m_settings->autotileEnabled()) {
            return QString();
        }
        return m_settings->defaultAutotileAlgorithm();
    });
    // Tiled-window-count provider — lets a SetTilingAlgorithm rule match on
    // Field::TiledWindowCount (e.g. switch algorithm once a second window
    // opens). Reads the engine's live per-screen state (non-creating); nullopt
    // when the screen is not actively tiling so a count predicate stays inert
    // there. The screen's current-context state aligns with the (desktop,
    // activity) the algorithm is resolved for, so the desktop/activity args are
    // not needed to disambiguate.
    m_layoutManager->setTiledWindowCountProvider(
        [this](const QString& screenId, int, const QString&) -> std::optional<int> {
            // const overloads throughout, as intent: a non-creating lookup
            // that returns nullptr when the screen has no tiling state, so a
            // TiledWindowCount predicate stays inert there instead of matching
            // on a reported 0. (Both overloads are non-creating today — see
            // AutotileEngine::stateForScreen in autotileengine/facade.cpp — so
            // the as_const is a statement of intent, not the thing preventing
            // a create-on-read.) Both tiling-family engines are consulted so a
            // TiledWindowCount rule works on scrolling screens too.
            // Pick by LIVE claim, not fixed order: a lingering state on an
            // engine the screen has since left would shadow the owning
            // engine's count with a stale one.
            if (m_autotileEngine && m_autotileEngine->isActiveOnScreen(screenId)) {
                if (const PhosphorEngine::IPlacementState* state =
                        std::as_const(*m_autotileEngine).stateForScreen(screenId)) {
                    return state->tiledWindowCount();
                }
            }
            if (m_scrollEngine && m_scrollEngine->isActiveOnScreen(screenId)) {
                if (const PhosphorEngine::IPlacementState* state =
                        std::as_const(*m_scrollEngine).stateForScreen(screenId)) {
                    return state->tiledWindowCount();
                }
            }
            return std::nullopt;
        });
    // Orientation provider — derives "portrait" / "landscape" from the screen's
    // geometry so a Field::ScreenOrientation rule can drive any context slot on a
    // rotated monitor. Returns nullopt for an unknown / invalid geometry (the
    // predicate then stays inert). A square screen is treated as landscape.
    m_layoutManager->setScreenOrientationProvider([this](const QString& screenId) -> std::optional<QString> {
        if (!m_screenManager) {
            return std::nullopt;
        }
        const QRect geom = m_screenManager->screenGeometry(screenId);
        if (!geom.isValid()) {
            return std::nullopt;
        }
        return geom.height() > geom.width() ? QStringLiteral("portrait") : QStringLiteral("landscape");
    });
    // Colour-scheme provider — session-wide "light" / "dark" from the live
    // application palette, so a Field::ColorScheme rule can drive any context
    // slot (a darker overlay at night, a scheme-specific layout). Reads live
    // per call, so there is no daemon-side cache to go stale; the registry's
    // cached resolvers fold the token into their keys, and the
    // systemColorSchemeChanged wiring (init_engines.cpp) re-drives applied
    // state on a flip.
    m_layoutManager->setColorSchemeProvider([]() -> std::optional<QString> {
        const QString token = Settings::systemColorSchemeToken();
        return token.isEmpty() ? std::nullopt : std::optional<QString>(token);
    });
    // Per-screen current-desktop provider (#648): the registry (and the
    // overlay service through it) resolves per-output desktops straight from
    // the VirtualDesktopManager — ONE authority, replacing the push-updated
    // mirror the daemon used to maintain from the screenDesktopChanged
    // handler. nullopt (no VDM, unknown screen) falls back to the registry's
    // global desktop, matching the old empty-mirror behavior.
    m_layoutManager->setCurrentVirtualDesktopProvider([this](const QString& screenId) -> std::optional<int> {
        if (!m_virtualDesktopManager) {
            return std::nullopt;
        }
        const int desktop = m_virtualDesktopManager->currentDesktopForScreen(screenId);
        return desktop >= 1 ? std::optional<int>(desktop) : std::nullopt;
    });
    // Snapping-preferred provider — separate from defaultLayoutIdProvider
    // because the user can have snapping enabled WITHOUT a global default
    // snap layout id (per-screen assignments cover everything). Without
    // this signal the cascade would fall through to autotile when both
    // (snappingEnabled && defaultLayoutId == "") and (autotileEnabled &&
    // defaultAutotileAlgorithm != ""), surfacing "Tiling: Binary Split"
    // OSD content to a user who never enabled autotile globally.
    m_layoutManager->setSnappingPreferredProvider([this]() {
        return m_settings && m_settings->snappingEnabled();
    });
    // Global "suppress default layout assignment" gate. When on, the level-1
    // default synthesis above is short-circuited so an unassigned context gets
    // no active layout (no engine activates) until the user assigns one — the
    // same effective state as having no default providers configured. The
    // per-context DefaultLayoutAssignment rule overrides this either way.
    m_layoutManager->setDefaultAssignmentSuppressedProvider([this]() {
        return m_settings && m_settings->suppressDefaultLayoutAssignment();
    });
    // Native scrolling-template store: created in the ctor (before the
    // layout-source bundle build so the template provider registers) and
    // wired into the registry HERE so the assignment/resolver choke points
    // validate template ids and resolve template objects. The
    // templatesChanged → engine-recompute connection is restart-scoped and
    // lives in signals.cpp.
    m_layoutManager->setScrollingTemplateStore(m_scrollingTemplateStore.get());
    // Default-template provider: the setting-backed fallback for a Scrolling
    // context whose cascade entry names no template (parity with snapping's
    // default layout).
    m_layoutManager->setDefaultScrollingTemplateProvider([this]() {
        return m_settings ? m_settings->defaultScrollingTemplate() : QString();
    });
    // Wire the compute service to the layout manager so tracked layouts
    // are evicted on removal (bounds m_trackedLayouts over time).
    m_layoutComputeService->setLayoutManager(m_layoutManager.get());

    // Seed the curated default picker visibility on a fresh install (no-op when
    // a layout-settings.json / autotile-overrides.json already exists), before
    // loadLayouts() so the seeded hidden state merges onto each layout.
    m_layoutManager->seedDefaultLayoutSettingsIfFresh(ConfigDefaults::defaultLayoutVisibilitySettings());

    // Load layouts (defaultLayout() reads settings internally)
    m_layoutManager->loadLayouts();
    m_layoutManager->loadAssignments();

    // Recalculate zone geometries for ALL layouts so that fixed-mode zones
    // have correct normalized coordinates for preview rendering (KCM, OSD, selector).
    if (QScreen* primary = Utils::primaryScreen()) {
        for (PhosphorZones::Layout* layout : m_layoutManager->layouts()) {
            PhosphorZones::LayoutComputeService::recalculateSync(
                layout, GeometryUtils::effectiveScreenGeometry(m_screenManager.get(), layout, primary));
        }
    }

    // Configure overlay service with settings, layout manager, and default
    // layout. ShaderRegistry is wired via the ctor, so every overlay path
    // that needs it sees a non-null registry from the first call onward.
    m_overlayService->setSettings(m_settings.get());
    m_overlayService->setLayoutManager(m_layoutManager.get());
    m_overlayService->setAlgorithmRegistry(m_algorithmRegistry.get());
    m_overlayService->setAutotileLayoutSource(m_autotileLayoutSource);
    if (auto* defLayout = m_layoutManager->defaultLayout()) {
        m_overlayService->setLayout(defLayout);
        m_zoneDetector->setLayout(defLayout);
        qCInfo(lcDaemon) << "Overlay configured layout=" << defLayout->name() << "zones=" << defLayout->zoneCount();
    } else {
        qCWarning(lcDaemon) << "No default layout available for overlay";
    }

    // Connect layout changes to zone detector and overlay service
    // activeLayoutChanged fires when the global active layout changes; layoutAssigned
    // fires for per-screen assignments. We handle both but avoid redundant recalculations.
    m_layoutSettingsWiringConnections.append(
        connect(m_layoutManager.get(), &PhosphorZones::LayoutRegistry::activeLayoutChanged, this,
                [this](PhosphorZones::Layout* layout) {
                    if (layout) {
                        // Recalculate zone geometries asynchronously using primary screen geometry.
                        // Active layout is global; recalculating per-screen overwrites each
                        // iteration (last-wins bug). The overlay computes per-screen geometry
                        // on the fly via GeometryUtils::getZoneGeometryWithGaps(m_screenManager.get(), ).
                        QScreen* primary = Utils::primaryScreen();
                        if (primary) {
                            QString screenId = PhosphorScreens::ScreenIdentity::identifierFor(primary);
                            m_layoutComputeService->requestRecalculate(
                                layout, screenId,
                                GeometryUtils::effectiveScreenGeometry(m_screenManager.get(), layout, primary));
                        }
                    }
                    m_zoneDetector->setLayout(layout);
                    m_overlayService->updateLayout(layout);
                }));

    // Connect per-screen layout assignments
    // Only update if this is a DIFFERENT layout than the active one
    // (to avoid double-processing when both signals fire for the same layout)
    m_layoutSettingsWiringConnections.append(
        connect(m_layoutManager.get(), &PhosphorZones::LayoutRegistry::layoutAssigned, this,
                [this](const QString& screenId, int /*virtualDesktop*/, PhosphorZones::Layout* layout) {
                    if (!layout) {
                        return;
                    }
                    // Skip if this layout is already the active layout
                    // (activeLayoutChanged handler already processed it for all screens)
                    if (layout == m_layoutManager->activeLayout()) {
                        return;
                    }
                    // This is a screen-specific layout different from the active one
                    // Only recalculate for the specific screen.
                    //
                    // Through the screenId overload, like every other
                    // recalculate site. screenByName matches the CONNECTOR name,
                    // while an assignment key is the screen IDENTIFIER — an
                    // EDID-derived string on any monitor that exposes one, or a
                    // "physId/vs:N" child. The lookup therefore missed and this
                    // whole block was dead on those setups. The overload resolves
                    // virtual sub-screens too, which screenByName never could.
                    const QRectF geom = GeometryUtils::effectiveScreenGeometry(m_screenManager.get(), layout, screenId);
                    if (geom.isValid()) {
                        m_layoutComputeService->requestRecalculate(layout, screenId, geom);
                    }
                    // Note: We don't change zone detector or overlay here since
                    // they work with the active layout, not per-screen layouts
                }));

    // Connect settings changes to overlay service and autotile engine.
    // This is the SINGLE comprehensive handler for batch config reloads (Settings::load()).
    // Individual autotile signals are NOT emitted from load() — all autotile state
    // transitions are handled here to avoid redundant retile passes.
    m_prevSnappingEnabled = m_settings->snappingEnabled();
    m_prevAutotileEnabled = m_settings->autotileEnabled();
    m_prevScrollingEnabled = m_settings->scrollingEnabled();
    m_previewNotifyTimer.setSingleShot(true);
    m_previewNotifyTimer.setInterval(100);
    m_layoutSettingsWiringConnections.append(connect(&m_previewNotifyTimer, &QTimer::timeout, this, [this]() {
        if (m_algorithmRegistry && m_algorithmRegistry->previewParams() != m_preRetilePreviewParams
            && m_layoutAdaptor) {
            m_layoutAdaptor->notifyLayoutListChanged();
        }
    }));

    // m_settings is ctor-owned and SURVIVES a stop() -> init() cycle, and
    // stop()'s per-sender sweep deliberately excludes it — so re-wiring here
    // would stack a second settingsChanged handler and run the whole
    // mode-transition block twice per save (double resnap, double OSD arm,
    // second pass against already-cleared pending restores). The duplicate is
    // prevented by the EXACT-handle drop at the top of this function, which
    // this connection registers itself with. Deliberately NOT a
    // (sender, signal, receiver) sweep, per the policy stated there: other
    // owners (the overlay service, ShortcutManager, UnifiedLayoutController)
    // hold their own handlers on this same sender, and severing by sender and
    // signal is one receiver argument away from taking theirs with it.
    m_layoutSettingsWiringConnections.append(connect(m_settings.get(), &Settings::settingsChanged, this, [this]() {
        m_overlayService->updateSettings(m_settings.get());

        // Detect state transitions before syncing
        const bool snappingNow = m_settings->snappingEnabled();
        const bool autotileNow = m_settings->autotileEnabled();
        const bool scrollingNow = m_settings->scrollingEnabled();
        const bool snappingToggled = snappingNow != m_prevSnappingEnabled;
        const bool autotileToggled = autotileNow != m_prevAutotileEnabled;
        const bool scrollingToggled = scrollingNow != m_prevScrollingEnabled;
        m_prevSnappingEnabled = snappingNow;
        m_prevAutotileEnabled = autotileNow;
        m_prevScrollingEnabled = scrollingNow;

        // Sync config immediately so the engine never reads stale values.
        // Only retile + preview notification are debounced (100ms timer).
        m_preRetilePreviewParams =
            m_algorithmRegistry ? m_algorithmRegistry->previewParams() : PhosphorTiles::AlgorithmPreviewParams{};
        if (m_autotileEngine) {
            m_autotileEngine->refreshConfigFromSettings();
        }
        if (m_scrollEngine) {
            m_scrollEngine->refreshConfigFromSettings();
        }
        m_previewNotifyTimer.start();

        // Capture autotile window order BEFORE any mode switch destroys PhosphorTiles::TilingState.
        // Saved for deterministic re-seeding when autotile is re-enabled.
        // MERGE (not replace): the map is shared with the scrolling engine
        // and with other contexts' saved orders — a wholesale assign here
        // would discard every scrolling column order and every other
        // context's entries (same rationale as the mode-toggle path in
        // autotile_init.cpp).
        if (autotileToggled && !autotileNow) {
            const QHash<TilingStateKey, QStringList> captured = captureAutotileOrders();
            for (auto it = captured.constBegin(); it != captured.constEnd(); ++it) {
                m_lastEngineOrders.insert(it.key(), it.value());
            }
            // Feature gate toggled off: release the engine's screens.
            handleAutotileDisabled();
        }

        // Handle activation of autotile mode.
        // Fires when either:
        //   (a) Snapping toggled OFF while autotile is already enabled, OR
        //   (b) Autotile toggled ON (regardless of snapping state)
        // Both paths need per-screen autotile assignments created.
        // handleSnappingToAutotile() skips screens already on an autotile
        // assignment, so mixed-mode setups (screen A snapping, screen B
        // autotile) correctly flip screen A without clobbering screen B's
        // per-screen algorithm customization.
        const bool enteringAutotile =
            (snappingToggled && !snappingNow && autotileNow) || (autotileToggled && autotileNow && !snappingNow);
        if (enteringAutotile) {
            handleSnappingToAutotile();
        }

        // Re-derive autotile screens and apply per-screen overrides.
        // windowsReleased clears floating state for released windows.
        updateEngineScreens();
        updateLayoutFilter();

        // Resnap after a tiling-family engine is disabled: restore windows to
        // their pre-tiling zone positions. PhosphorZones::Zone assignments are
        // preserved while a screen is autotile OR scrolling (onLayoutChanged
        // skips non-snap screens) so resnap uses original snap assignments.
        // The scrolling arm is the master-switch twin: its released windows
        // fell through updateEngineScreens' derive pass to snapping in the
        // same recompute above.
        if (((autotileToggled && !autotileNow) || (scrollingToggled && !scrollingNow)) && m_windowTrackingAdaptor
            && m_snapAdaptor && m_snapEngine) {
            // Pre-arm OSD suppression for the resnap signal(s) about to fire (the
            // feedback returns asynchronously, so arm before emitting).
            armResnapOsdSuppression(1); // resnapCurrentAssignments()
            m_snapAdaptor->resnapCurrentAssignments();
            // Batched float-restore: one resnap signal per autotile-disabled
            // toggle instead of per-window D-Bus chatter. Downcast mirrors
            // signals.cpp's resnap-batching path; a non-snap concrete engine
            // would simply skip the batch (no behaviour regression vs the
            // pre-batch shape, which used per-window D-Bus calls).
            if (auto* concreteSnap = qobject_cast<PhosphorSnapEngine::SnapEngine*>(m_snapEngine.get())) {
                // updateEngineScreens() above fired windowsReleased synchronously
                // and its tail drain already emitted the snap-FLOAT half; what
                // remains in m_pendingSnapFloatRestores is the branch-b snap-zone
                // restores for windows that were floated in autotile. Append them
                // to this batch — mirroring the mode-toggle path — so they land in
                // the same signal as the pre-tile geometry restores rather than
                // lingering to corrupt the next toggle's preClaimedZoneIds. The
                // exclusion set keeps buildAutotileRestoreEntries from also
                // emitting a pre-tile rect for a window that has a zone restore
                // here (its own floating guard covers the float half).
                QSet<QString> restoredWindows;
                for (const ZoneAssignmentEntry& e : m_pendingSnapFloatRestores) {
                    restoredWindows.insert(e.windowId);
                }
                // Scope to the CURRENT activity: m_lastEngineOrders can hold
                // orders captured on other activities, and this caller is
                // desktop-unscoped (per-screen desktops differ, so no single
                // desktop filter applies) — the builder's per-window guards
                // cover the rest.
                QVector<ZoneAssignmentEntry> entries =
                    buildAutotileRestoreEntries(restoredWindows, -1, currentActivity());
                entries.append(m_pendingSnapFloatRestores);
                m_pendingSnapFloatRestores.clear();
                if (!entries.isEmpty()) {
                    armResnapOsdSuppression(1); // the batched emit drives a second resnap feedback
                    concreteSnap->emitBatchedResnap(entries);
                }
            }
        }

        // Re-resolve the active layout from assignments for the current context.
        // Resnap/retile/OSD is triggered separately by applyAssignmentChanges()
        // after the KCM's batch save completes — NOT here in the settings handler.
        syncModeFromAssignments();

        // Refresh the active-assignment snapshot without applying, the same way
        // the desktop / activity switch handlers do. A settings save can move a
        // screen's RESOLVED assignment while mutating no assignment at all: the
        // cascade falls through to the global default providers, so editing the
        // default layout (or toggling snapping / autotile) re-resolves every
        // screen that has no stored entry of its own. Without this the snapshot
        // goes stale, which both makes a later rule edit falsely re-resnap those
        // screens and leaves subscribers to activeLayoutForScreenChanged (the
        // KWin effect's ActiveLayout rule matching) pinned to the old layout.
        //
        // During init()'s D-Bus retry loop this can run before m_layoutAdaptor
        // exists: the snapshot advances and the publish is dropped. Harmless —
        // the adaptor's mirror is seeded by the priming diffActiveAssignments()
        // in initEngines once the adaptor is up.
        diffActiveAssignments();
    }));

    // Resnap currently-snapped windows when a snapping gap/padding setting
    // changes (global or per-screen) so the new spacing is visible immediately
    // instead of requiring a manual re-snap of each window (discussion #661).
    // The signals below are re-emitted by Settings::load() only when the value
    // actually changed, so this never fires on unrelated saves (colours,
    // shortcuts). Autotile windows are already retiled by the settingsChanged
    // handler above; this covers manually-snapped windows. Debounced so a batch
    // of per-side gap edits in one save collapses into a single resnap pass.
    // Watchdog that floors the resnap-OSD suppression counter if some primed
    // feedback never arrives (a resnap that produced zero moves emits none).
    // Re-armed by armResnapOsdSuppression on every arm.
    m_suppressResnapOsdWatchdog.setSingleShot(true);
    m_suppressResnapOsdWatchdog.setInterval(2000);
    m_layoutSettingsWiringConnections.append(connect(&m_suppressResnapOsdWatchdog, &QTimer::timeout, this, [this]() {
        m_suppressResnapOsd = 0;
    }));

    m_gapResnapTimer.setSingleShot(true);
    m_gapResnapTimer.setInterval(100);
    m_layoutSettingsWiringConnections.append(connect(&m_gapResnapTimer, &QTimer::timeout, this, [this]() {
        if (!m_snapAdaptor) {
            return;
        }
        armResnapOsdSuppression(1); // settings-driven reflow, not user navigation
        m_snapAdaptor->resnapCurrentAssignments();
    }));
    const auto scheduleGapResnap = [this]() {
        m_gapResnapTimer.start();
    };
    m_layoutSettingsWiringConnections.append(
        connect(m_settings.get(), &Settings::innerGapChanged, this, scheduleGapResnap));
    m_layoutSettingsWiringConnections.append(
        connect(m_settings.get(), &Settings::outerGapChanged, this, scheduleGapResnap));
    m_layoutSettingsWiringConnections.append(
        connect(m_settings.get(), &Settings::usePerSideOuterGapChanged, this, scheduleGapResnap));
    m_layoutSettingsWiringConnections.append(
        connect(m_settings.get(), &Settings::outerGapTopChanged, this, scheduleGapResnap));
    m_layoutSettingsWiringConnections.append(
        connect(m_settings.get(), &Settings::outerGapBottomChanged, this, scheduleGapResnap));
    m_layoutSettingsWiringConnections.append(
        connect(m_settings.get(), &Settings::outerGapLeftChanged, this, scheduleGapResnap));
    m_layoutSettingsWiringConnections.append(
        connect(m_settings.get(), &Settings::outerGapRightChanged, this, scheduleGapResnap));
    m_layoutSettingsWiringConnections.append(
        connect(m_settings.get(), &Settings::perScreenSnappingSettingsChanged, this, scheduleGapResnap));
}

} // namespace PlasmaZones
