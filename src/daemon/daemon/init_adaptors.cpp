// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/daemon.h"
#include "helpers.h"

#include <QGuiApplication>
#include <QDBusConnection>

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
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
#include "dbus/tilingadaptor/tilingadaptor.h"
// Needed as COMPLETE types, not just forward declarations: the re-entry
// preamble below deletes them, and deleting through an incomplete type calls
// neither the destructor nor a class-specific operator delete.
#include "dbus/autotileadaptor/autotileadaptor.h"
#include "dbus/scrollingadaptor/scrollingadaptor.h"
#include "dbus/snapadaptor/snapadaptor.h"
#include "dbus/shaderadaptor.h"
#include "dbus/compositorbridgeadaptor.h"
#include "dbus/controladaptor.h"
#include "dbus/ruleadaptor.h"

namespace PlasmaZones {

void Daemon::initCoreAdaptors()
{
    // stop() -> init() re-entry: every adaptor below survives stop() (it only
    // detaches / clears their borrows), so construct-over would leak one full
    // adaptor set as QObject children per cycle, each still holding its
    // init-time connections and D-Bus object registration. Delete the whole
    // previous set first, borrowers before borrowed: ControlAdaptor (borrows
    // snap/layout/WTA), then the engine adaptors (borrow the WTA), then the
    // core adaptors in reverse creation order.
    //
    // Manual delete of Qt-parented QObjects is a deliberate carve-out from
    // the never-manual-delete rule: init() runs before exec(), so no queued
    // D-Bus dispatch or event can land mid-delete, and deleteLater() would
    // NOT be processed before the new set registers over the same D-Bus
    // paths.
    delete m_controlAdaptor;
    m_controlAdaptor = nullptr;
    delete m_snapAdaptor;
    m_snapAdaptor = nullptr;
    delete m_autotileAdaptor;
    m_autotileAdaptor = nullptr;
    // TilingAdaptor and ScrollingAdaptor belong to this set too: both are
    // QDBusAbstractAdaptor children of the D-Bus-registered Daemon object and
    // both are unconditionally re-newed in initEnginesAndWiring, which
    // documents these members as null on a re-cycle. Omitting them leaked the
    // old pair AND left a second adaptor registered for an interface that
    // already had one on the same object, which Qt refuses — taking down the
    // whole Tiling/Scrolling wire surface after a daemon restart.
    delete m_tilingAdaptor;
    m_tilingAdaptor = nullptr;
    delete m_scrollingAdaptor;
    m_scrollingAdaptor = nullptr;
    delete m_windowDragAdaptor;
    m_windowDragAdaptor = nullptr;
    delete m_windowTrackingAdaptor;
    m_windowTrackingAdaptor = nullptr;
    delete m_zoneDetectionAdaptor;
    m_zoneDetectionAdaptor = nullptr;
    delete m_overlayAdaptor;
    m_overlayAdaptor = nullptr;
    delete m_compositorBridge;
    m_compositorBridge = nullptr;
    delete m_ruleAdaptor;
    m_ruleAdaptor = nullptr;
    delete m_shaderAdaptor;
    m_shaderAdaptor = nullptr;
    delete m_settingsAdaptor;
    m_settingsAdaptor = nullptr;
    delete m_layoutAdaptor;
    m_layoutAdaptor = nullptr;
    // DBusScreenAdaptor is in this set for the same reason as the Tiling and
    // Scrolling pair above: it is a QDBusAbstractAdaptor child of the
    // D-Bus-registered Daemon and is re-newed unconditionally below, so
    // omitting it leaked the old one and left a second adaptor registered for
    // an interface that already had one on this object.
    delete m_screenAdaptor;
    m_screenAdaptor = nullptr;

    // Initialize domain-specific D-Bus adaptors
    // Each adaptor has its own D-Bus interface
    // D-Bus adaptors use raw new; Qt parent-child manages their lifetime.
    m_layoutAdaptor =
        new LayoutAdaptor(m_layoutManager.get(), m_virtualDesktopManager.get(), m_screenManager.get(), this);
    m_layoutAdaptor->setActivityManager(m_activityManager.get());
    m_layoutAdaptor->setSettings(m_settings.get());
    m_layoutAdaptor->setAlgorithmRegistry(m_algorithmRegistry.get());
    m_layoutAdaptor->setLayoutSource(m_layoutSources.composite());
    // Thread the bundle-owned autotile source through the adaptor's
    // buildUnifiedLayoutList path so its preview cache survives across
    // D-Bus calls. The full composite above drives the
    // getLayoutPreview* methods; this separate pointer targets only the
    // autotile enumeration slot — see LayoutAdaptor::setAutotileLayoutSource.
    m_layoutAdaptor->setAutotileLayoutSource(m_autotileLayoutSource);
    // Invalidate D-Bus getActiveLayout() cache when the default layout changes in settings
    connect(m_settings.get(), &Settings::defaultLayoutIdChanged, m_layoutAdaptor, &LayoutAdaptor::invalidateCache);
    m_settingsAdaptor = new SettingsAdaptor(m_settings.get(), m_shaderRegistry.get(), &m_profileRegistry, this);

    // Shader adaptor - shader discovery, compilation lifecycle, file monitoring.
    // Held as a member so stop() can detach() it before the unique_ptr member
    // that owns m_shaderRegistry runs its destructor.
    m_shaderAdaptor = new ShaderAdaptor(m_shaderRegistry.get(), this);

    // Rule adaptor - the unified org.plasmazones.Rules surface.
    // Held as a member so stop() can detach() it before the unique_ptr that
    // owns m_ruleStore runs its destructor.
    m_ruleAdaptor = new RuleAdaptor(m_ruleStore.get(), this);

    // Compositor bridge adaptor - compositor-agnostic window control protocol.
    // Held as a member so the support report and the registration watchdog
    // can query its state. Ownership stays with `this` via QObject parent.
    m_compositorBridge = new CompositorBridgeAdaptor(this);

    // Session-idle detection for Decorations.Performance.PauseWhenIdle. The KWin
    // effect cannot do this itself: idleness arrives over ext-idle-notify-v1, a
    // Wayland CLIENT protocol that the compositor the effect lives in SERVES rather
    // than consumes. The daemon is already a client, so it watches and pushes the
    // resolved boolean over D-Bus.
    //
    // Worth the plumbing because it is the only thing that recovers idle GPU power:
    // an animated decoration pack repaints every window carrying it on every vsync,
    // and it is the existence of per-frame work — not how cheap that work is — that
    // keeps the card pinned in its top performance state.
    //
    // Constructed AFTER m_settingsAdaptor (it emits through it) and AFTER
    // m_compositorBridge (it subscribes to bridgeRegistered to push the current idle
    // state to a freshly (re)started effect, since the signal is edge-triggered).
    setupIdleService();

    // Overlay adaptor - overlay visibility and highlighting
    m_overlayAdaptor = new OverlayAdaptor(m_overlayService.get(), m_zoneDetector.get(), m_layoutManager.get(),
                                          m_screenManager.get(), m_settings.get(), this);

    // PhosphorZones::Zone detection adaptor - zone detection queries
    m_zoneDetectionAdaptor = new ZoneDetectionAdaptor(m_zoneDetector.get(), m_layoutManager.get(),
                                                      m_screenManager.get(), m_settings.get(), this);

    // Window tracking adaptor - window-zone assignments
    m_windowTrackingAdaptor =
        new WindowTrackingAdaptor(m_layoutManager.get(), m_zoneDetector.get(), m_screenManager.get(), m_settings.get(),
                                  m_virtualDesktopManager.get(), m_activityManager.get(), this);
    m_windowTrackingAdaptor->setZoneDetectionAdaptor(m_zoneDetectionAdaptor);
    m_windowTrackingAdaptor->setWindowRegistry(m_windowRegistry.get());
    // Full rule set for per-window RestorePosition evaluation (overrides the
    // per-engine *RestoreFloatedWindowsOnLogin settings for matched windows).
    m_windowTrackingAdaptor->setRuleStore(m_ruleStore.get());

    // Drop closed windows from m_lastEngineOrders so a manual→autotile toggle
    // doesn't replay a ghost id into the TilingState (recalculateLayout would
    // then tile N+1 windows for N actual windows).
    connect(m_windowRegistry.get(), &PhosphorEngine::WindowRegistry::windowDisappeared, this,
            [this](const QString& instanceId) {
                pruneEngineOrdersForWindow(instanceId);
            });

    // Reapply window geometries after each geometry batch (processPendingGeometryUpdates).
    // When the delayed panel requery completes it emits availableGeometryChanged, which triggers
    // the same debounce → processPendingGeometryUpdates → reapply path; no separate delay needed.
    m_reapplyGeometriesTimer.setSingleShot(true);
    connect(&m_reapplyGeometriesTimer, &QTimer::timeout, m_windowTrackingAdaptor,
            &WindowTrackingAdaptor::requestReapplyWindowGeometries);

    // DBusScreenAdaptor::setVirtualScreenConfig writes to Settings (the source
    // of truth) via the IConfigStore — the daemon's single SettingsConfigStore
    // instance, shared with m_screenManager (as its Config::configStore) and
    // m_virtualScreenSwapper. One store per process, one change-signal
    // channel, no parallel Settings observer.
    m_screenAdaptor = new PhosphorScreens::DBusScreenAdaptor(m_screenManager.get(), m_virtualScreenStore.get(), this);

    // Window drag adaptor - handles drag events from KWin script
    // All drag logic (modifiers, zones, snapping) handled here
    m_windowDragAdaptor = new WindowDragAdaptor(m_overlayService.get(), m_zoneDetector.get(), m_layoutManager.get(),
                                                m_screenManager.get(), m_settings.get(), m_windowTrackingAdaptor, this);

    // PhosphorZones::Zone selector methods are called directly from WindowDragAdaptor; QDBusAbstractAdaptor
    // signals are for D-Bus, not Qt connections.

    // Give the window drag adaptor access to the shortcut manager for
    // registering/unregistering the Escape cancel shortcut during drags.
    // Routed through the PhosphorShortcutsIntegration::IAdhocRegistrar interface so the underlying
    // Registry stays private to ShortcutManager.
    m_windowDragAdaptor->setShortcutRegistrar(m_shortcutManager.get());

    // When the compositor bridge re-registers (e.g. KWin reloaded the effect,
    // effect process restarted, or daemon itself restarted mid-drag), any drag
    // state the daemon is still holding is stale — the new effect instance has
    // no knowledge of the prior drag. Clear it eagerly so the next dragStarted
    // from the fresh effect lands on a clean slate instead of silently
    // colliding with a mismatched windowId in the next handler.
    connect(m_compositorBridge, &CompositorBridgeAdaptor::bridgeRegistered, m_windowDragAdaptor,
            [this](const QString& compositorName, const QString&, const QStringList&) {
                qCInfo(lcDaemon) << "Compositor bridge registered (" << compositorName
                                 << ") — clearing any stale drag state held by daemon";
                m_windowDragAdaptor->clearForCompositorReconnect();
            });

    // Registration watchdog: the KWin effect should register as a compositor
    // bridge within a few seconds of startup. If it never does, the daemon is
    // alive but has no window control — drags and shortcuts silently do
    // nothing. Stop the watchdog as soon as the bridge registers; otherwise
    // warnCompositorBridgeMissing() fires once on timeout. Connecting to the
    // adaptor (not m_windowDragAdaptor) so a re-registration also cancels it.
    m_bridgeWatchdogTimer.setSingleShot(true);
    connect(&m_bridgeWatchdogTimer, &QTimer::timeout, this, &Daemon::warnCompositorBridgeMissing);
    connect(m_compositorBridge, &CompositorBridgeAdaptor::bridgeRegistered, &m_bridgeWatchdogTimer,
            [this](const QString&, const QString&, const QStringList&) {
                m_bridgeWatchdogTimer.stop();
            });

    // Initialize scripted algorithm loader BEFORE engine construction so that
    // user-defined algorithms are registered in the daemon registry before
    // the engine resolves the configured algorithm ID.
    m_scriptedAlgorithmLoader = std::make_unique<PhosphorTiles::ScriptedAlgorithmLoader>(
        QString(ScriptedAlgorithmSubdir), m_algorithmRegistry.get());
    // When scripted algorithms change (hot-reload), notify layout list consumers
    connect(m_scriptedAlgorithmLoader.get(), &PhosphorTiles::ScriptedAlgorithmLoader::algorithmsChanged, this,
            [this]() {
                if (m_layoutAdaptor)
                    m_layoutAdaptor->notifyLayoutListChanged();
            });
    m_scriptedAlgorithmLoader->scanAndRegister();
}

} // namespace PlasmaZones
