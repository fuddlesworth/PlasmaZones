// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/daemon.h"
#include "helpers.h"

#include <QGuiApplication>
#include <QFutureWatcher>
#include <QPointer>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QScreen>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusError>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPluginLoader>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <array>

#include <PhosphorServiceIdle/IdleService.h>
#include <PhosphorAnimation/CurveLoader.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfileLoader.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/PhosphorCurve.h>
#include <PhosphorAnimation/QtQuickClockManager.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include "daemon/overlayservice.h"
#include "daemon/controllers/unifiedlayoutcontroller.h"
#include "daemon/controllers/shortcutmanager.h"
#include "daemon/controllers/enginefactory.h"
#include "daemon/controllers/contextresolverwiring.h"
#include "daemon/rendering/surfaceshaderitem.h"
#include "daemon/rendering/zoneentryscaffold.h"
#include "daemon/rendering/zoneshadernoderhi.h"

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorZones/LayoutRegistry.h>
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
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorContext/ContextResolver.h>
#include <PhosphorScreens/DBusScreenAdaptor.h>
#include <PhosphorScreens/Swapper.h>
#include <PhosphorScreens/PlasmaPanelSource.h>
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
#include "core/types/baselinecleanup.h"
#include "core/types/constants.h"
#include "core/resolve/crosssurfaceresolver.h"
#include "core/resolve/animationbootstrap.h"
#include "core/resolve/screenmoderouter.h"
#include "core/utils/geometryutils.h"
#include "core/utils/utils.h"
#include "core/platform/logging.h"
#include "core/interfaces/shaderregistry.h"
#include "common/screenidresolver.h"
#include "common/layoutbundlebuilder.h"
#include "phosphor_i18n.h"
#include "dbus/layoutadaptor/layoutadaptor.h"
#include "dbus/settingsadaptor/settingsadaptor.h"
#include "dbus/overlayadaptor.h"
#include "dbus/zonedetectionadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "dbus/autotileadaptor/autotileadaptor.h"
#include "dbus/snapadaptor/snapadaptor.h"
#include "dbus/shaderadaptor.h"
#include "dbus/compositorbridgeadaptor.h"
#include "dbus/controladaptor.h"
#include "dbus/ruleadaptor.h"

namespace PlasmaZones {

void Daemon::initLayoutAndSettingsWiring()
{
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
    // the explicit teardown in stop() (which clears both providers
    // before any unique_ptr member runs its destructor) plus the null
    // checks below as a belt-and-suspenders guard against future
    // refactors that reset m_settings explicitly. NOTE: snap with
    // defaultLayoutId="" silently falls through to the autotile branch
    // — see test_layoutmanager_assignment.cpp
    // testLevel1Default_snapEnabledEmptyId_autotileEnabled_autotileWins
    // for the pinned behaviour.
    m_layoutManager->setDefaultLayoutIdProvider([this]() {
        if (!m_settings || !m_settings->snappingEnabled()) {
            return QString();
        }
        return m_settings->defaultLayoutId();
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
            if (!m_autotileEngine) {
                return std::nullopt;
            }
            // const overload: a non-creating lookup that returns nullptr when the
            // screen has no tiling state. The non-const overload would lazily
            // CREATE an empty state, both polluting m_screenStates during a pure
            // resolution query and reporting 0 (not nullopt) for a non-tiling
            // screen, which would make a TiledWindowCount predicate match there
            // instead of staying inert.
            const PhosphorEngine::IPlacementState* state = std::as_const(*m_autotileEngine).stateForScreen(screenId);
            if (!state) {
                return std::nullopt;
            }
            return state->tiledWindowCount();
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
            });

    // Connect per-screen layout assignments
    // Only update if this is a DIFFERENT layout than the active one
    // (to avoid double-processing when both signals fire for the same layout)
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
                // Only recalculate for the specific screen
                const PhosphorScreens::PhysicalScreen screen = m_screenManager->screenByName(screenId);
                if (screen.isValid() && screen.qscreen) {
                    m_layoutComputeService->requestRecalculate(
                        layout, screenId,
                        GeometryUtils::effectiveScreenGeometry(m_screenManager.get(), layout, screen.qscreen));
                }
                // Note: We don't change zone detector or overlay here since
                // they work with the active layout, not per-screen layouts
            });

    // Connect settings changes to overlay service and autotile engine.
    // This is the SINGLE comprehensive handler for batch config reloads (Settings::load()).
    // Individual autotile signals are NOT emitted from load() — all autotile state
    // transitions are handled here to avoid redundant retile passes.
    m_prevSnappingEnabled = m_settings->snappingEnabled();
    m_prevAutotileEnabled = m_settings->autotileEnabled();
    m_previewNotifyTimer.setSingleShot(true);
    m_previewNotifyTimer.setInterval(100);
    connect(&m_previewNotifyTimer, &QTimer::timeout, this, [this]() {
        if (m_algorithmRegistry && m_algorithmRegistry->previewParams() != m_preRetilePreviewParams
            && m_layoutAdaptor) {
            m_layoutAdaptor->notifyLayoutListChanged();
        }
    });

    connect(m_settings.get(), &Settings::settingsChanged, this, [this]() {
        m_overlayService->updateSettings(m_settings.get());

        // Detect state transitions before syncing
        const bool snappingNow = m_settings->snappingEnabled();
        const bool autotileNow = m_settings->autotileEnabled();
        const bool snappingToggled = snappingNow != m_prevSnappingEnabled;
        const bool autotileToggled = autotileNow != m_prevAutotileEnabled;
        m_prevSnappingEnabled = snappingNow;
        m_prevAutotileEnabled = autotileNow;

        // Sync config immediately so the engine never reads stale values.
        // Only retile + preview notification are debounced (100ms timer).
        m_preRetilePreviewParams =
            m_algorithmRegistry ? m_algorithmRegistry->previewParams() : PhosphorTiles::AlgorithmPreviewParams{};
        if (m_autotileEngine) {
            m_autotileEngine->refreshConfigFromSettings();
        }
        m_previewNotifyTimer.start();

        // Capture autotile window order BEFORE any mode switch destroys PhosphorTiles::TilingState.
        // Saved for deterministic re-seeding when autotile is re-enabled.
        if (autotileToggled && !autotileNow) {
            m_lastAutotileOrders = captureAutotileOrders();
        }

        // Handle autotile feature gate toggle
        if (autotileToggled && !autotileNow) {
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
        updateAutotileScreens();
        updateLayoutFilter();

        // Resnap after autotile disabled: restore windows to their pre-autotile
        // zone positions. PhosphorZones::Zone assignments are preserved during autotile (onLayoutChanged
        // skips autotile screens) so resnap uses original snap assignments.
        if (autotileToggled && !autotileNow && m_windowTrackingAdaptor && m_snapAdaptor && m_snapEngine) {
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
                // updateAutotileScreens() above fired windowsReleased synchronously,
                // populating m_pendingSnapFloatRestores with the snap-float and
                // branch-b snap-zone restores for windows that were floated in
                // autotile. Those windows must be EXCLUDED from the pre-tile geometry
                // restore (they get a float/zone restore instead) and their entries
                // appended to this batch — mirroring the mode-toggle path. Dropping
                // them (the previous behaviour) lost the snap-float restore entirely
                // and left stale entries to corrupt the next toggle's preClaimedZoneIds.
                QSet<QString> restoredWindows;
                for (const ZoneAssignmentEntry& e : m_pendingSnapFloatRestores) {
                    restoredWindows.insert(e.windowId);
                }
                QVector<ZoneAssignmentEntry> entries = buildAutotileRestoreEntries(restoredWindows);
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
    });

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
    connect(&m_suppressResnapOsdWatchdog, &QTimer::timeout, this, [this]() {
        m_suppressResnapOsd = 0;
    });

    m_gapResnapTimer.setSingleShot(true);
    m_gapResnapTimer.setInterval(100);
    connect(&m_gapResnapTimer, &QTimer::timeout, this, [this]() {
        if (!m_snapAdaptor) {
            return;
        }
        armResnapOsdSuppression(1); // settings-driven reflow, not user navigation
        m_snapAdaptor->resnapCurrentAssignments();
    });
    const auto scheduleGapResnap = [this]() {
        m_gapResnapTimer.start();
    };
    connect(m_settings.get(), &Settings::innerGapChanged, this, scheduleGapResnap);
    connect(m_settings.get(), &Settings::outerGapChanged, this, scheduleGapResnap);
    connect(m_settings.get(), &Settings::usePerSideOuterGapChanged, this, scheduleGapResnap);
    connect(m_settings.get(), &Settings::outerGapTopChanged, this, scheduleGapResnap);
    connect(m_settings.get(), &Settings::outerGapBottomChanged, this, scheduleGapResnap);
    connect(m_settings.get(), &Settings::outerGapLeftChanged, this, scheduleGapResnap);
    connect(m_settings.get(), &Settings::outerGapRightChanged, this, scheduleGapResnap);
    connect(m_settings.get(), &Settings::perScreenSnappingSettingsChanged, this, scheduleGapResnap);
}

} // namespace PlasmaZones
