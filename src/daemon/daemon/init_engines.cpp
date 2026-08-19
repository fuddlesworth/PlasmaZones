// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// FILE-SIZE EXCEPTION (sanctioned): the engine-initialization phase of the
// Daemon composition root — every engine's construction, provider wiring and
// signal fan-out in the one place the ordering contract between them can be
// read top to bottom. Splitting by engine would scatter the cross-engine
// defer/reciprocity wiring this file exists to keep adjacent.

#include "daemon/daemon.h"
#include "helpers.h"
#include "stripzones.h"
#include "common/stripcardserialize.h"

#include <QGuiApplication>
#include <QFutureWatcher>
#include <QPointer>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QScreen>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPluginLoader>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
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
#include <PhosphorContext/ContextResolver.h>
#include <PhosphorScreens/DBusScreenAdaptor.h>
#include <PhosphorScreens/Swapper.h>
#include <PhosphorScreens/PlasmaPanelSource.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
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
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "dbus/autotileadaptor/autotileadaptor.h"
#include "dbus/tilingadaptor/tilingadaptor.h"
#include "dbus/scrollingadaptor/scrollingadaptor.h"
#include "dbus/snapadaptor/snapadaptor.h"
#include "dbus/shaderadaptor.h"
#include "dbus/compositorbridgeadaptor.h"
#include "dbus/controladaptor.h"
#include "dbus/ruleadaptor.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

void Daemon::initEnginesAndWiring()
{
    // Create both placement engines and the mode router via factory.
    // The factory returns concrete types; we grab raw pointers for adaptor
    // wiring before moving into the base-class unique_ptr members.
    auto engines = createEngines(m_layoutManager.get(), m_windowTrackingAdaptor->service(), m_screenManager.get(),
                                 m_algorithmRegistry.get(), m_zoneDetector.get(), m_settings.get(),
                                 m_virtualDesktopManager.get(), m_windowRegistry.get());
    auto* autotileEngine = engines.autotile.get();
    auto* snapEngine = engines.snap.get();
    auto* scrollEngine = engines.scroll.get();
    // Factory contract: createEngines always constructs all three engines
    // and the router. Fail loudly once here in every build; the code below
    // dereferences the raw pointers unconditionally, so a sometimes-null
    // contract would need guards on EVERY use, not just some.
    if (!autotileEngine || !snapEngine || !scrollEngine || !engines.router) {
        qFatal("Daemon::initEnginesAndWiring: createEngines violated its all-engines contract");
    }
    // Move the shared cross-surface resolver BEFORE the engines so it is
    // destroyed AFTER them (they borrow it). Declared earlier than the engines
    // in daemon.h for the same reason.
    m_crossSurfaceResolver = std::move(engines.crossSurfaceResolver);
    m_autotileEngine = std::move(engines.autotile);
    m_snapEngine = std::move(engines.snap);
    m_scrollEngine = std::move(engines.scroll);
    m_screenModeRouter = std::move(engines.router);

    // Per-context (window-rule) gap overrides. Snapping resolves these as
    // the highest-priority gap layer (GeometryUtils::getEffective*); the
    // tiling-family engines get them through these providers — without
    // them a context gap rule was silently ignored on tiled windows. Each
    // closure resolves the screen's CURRENT context here (the engine
    // library stays settings-agnostic) and adapts ContextGapOverride into
    // the PerScreenKeys-shaped map the resolver already consumes.
    //
    // Scrolling provider: resolves against the "scrolling" placement mode
    // so a `Mode Equals "scrolling"` gap rule applies to the strip and
    // stays inert elsewhere.
    scrollEngine->setContextGapProvider([this](const QString& screenId) -> QVariantMap {
        if (!m_layoutManager || screenId.isEmpty()) {
            return {};
        }
        return GeometryUtils::mergeConfigPerScreenGaps(
            GeometryUtils::contextGapOverrideMap(m_layoutManager->resolveContextGaps(
                screenId, currentDesktopForScreen(screenId), currentActivity(), QStringLiteral("scrolling"))),
            m_settings.get(), screenId);
    });

    // Snap-restore defer gate (ScrollEngine::windowOpened): bakes BOTH the
    // global snapping toggle and the recorded context's mode into one
    // closure, matching the predicate autotile's twin gate evaluates
    // through its own layout-manager reference. When snapping is disabled
    // SnapEngine::resolveWindowRestore never claims, so the gate must
    // answer false or the window would strand unmanaged.
    scrollEngine->setSnappingModeResolver([this](const QString& screenId, int desktop, const QString& activity) {
        return m_layoutManager && m_layoutManager->snappingPreferred()
            && m_layoutManager->modeForScreen(screenId, desktop, activity)
            == PhosphorZones::AssignmentEntry::Mode::Snapping;
    });

    // Own-mode resolver for the scroll engine's cross-screen reclaim
    // (claimCrossScreenReopen): answers whether the RECORDED context still
    // resolves to Scrolling mode, so a session window KWin dropped on the
    // wrong output is pulled back into its recorded strip. No global-toggle
    // term (unlike the snapping resolver above): a Scrolling-mode verdict
    // already implies a live scroll assignment for that context.
    scrollEngine->setScrollingModeResolver([this](const QString& screenId, int desktop, const QString& activity) {
        return m_layoutManager
            && m_layoutManager->modeForScreen(screenId, desktop, activity)
            == PhosphorZones::AssignmentEntry::Mode::Scrolling;
    });

    // Autotile-mode resolver for the scroll-side cross-screen defer gate,
    // the reciprocal of autotile's scrolling defer.
    //
    // LIVENESS is part of the question, not just mode. The deferring side
    // must ask exactly what the CLAIMING side will answer: autotile's claim
    // requires the recorded home in its LIVE screen set on top of the
    // record-context mode verdict, so a defer keyed on mode alone stands
    // down for a window autotile then declines — leaving it unmanaged.
    // The two can disagree during a per-desktop-mode context switch or
    // before a screen set is announced. Only the daemon sees both engines,
    // so the liveness term is baked in here rather than inside either
    // library.
    scrollEngine->setAutotileModeResolver(
        [this, autotileEngine](const QString& screenId, int desktop, const QString& activity) {
            return m_layoutManager
                && m_layoutManager->modeForScreen(screenId, desktop, activity)
                == PhosphorZones::AssignmentEntry::Mode::Autotile
                && autotileEngine->isActiveOnScreen(screenId);
        });

    // Scroll "zone numbers" for the navigation OSD: a strip window's zone
    // number is its 1-based VISIBLE tile slot — the same sequential
    // strip-order number the previews label and the Snap-to-Zone digits
    // drive through moveFocusedToPosition. A window with no visible tile
    // gets no entry, which covers off-screen columns, the hidden tabs of a
    // tabbed column, parked columns, and any tile whose work-area
    // intersection comes out empty; the OSD falls back to direction-only
    // copy for those. This keeps the "Zone %1" copy meaningful on scrolling
    // screens, which have no zone layout of their own, for the arms that
    // actually render zone copy — the focus, cycle and digit-success arms,
    // each of which hands the OSD a landed window id. The move and swap
    // arms pass an empty targetZoneId and render direction copy regardless
    // of what this provider returns.
    //
    // ONE visibleTiles() walk, and the number comes off the TILE rather than
    // the loop index: the previews, the digits and this list therefore
    // derive from the same walk and cannot disagree, without three call
    // sites independently re-deriving "index + 1" and having to be kept
    // honest by comment. StripZones::numberMapsForTiles is that one
    // derivation, shared with the strip preview card (daemon/osd.cpp).
    //
    // m_overlayService is constructed in the Daemon constructor and never
    // reset, so it is non-null for the daemon's whole lifetime. Stated once
    // here for this file: that is why this deref and the later ones in
    // initEnginesAndWiring carry no null guard.
    m_overlayService->setScrollZonesProvider([this](const QString& screenId) -> QVariantList {
        const auto* scroll = qobject_cast<const PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get());
        if (!scroll || !scroll->isActiveOnScreen(screenId)) {
            return {};
        }
        return StripZones::numberMapsForTiles(scroll->visibleTiles(screenId));
    });

    // Engine layout-capability resolver for the layout picker / drag popup:
    // routes through the router so the answer tracks the LIVE owning engine
    // (a disabled scrolling assignment downgrades to snapping and keeps its
    // layouts). Cleared alongside the scroll-zones provider in stop().
    m_overlayService->setLayoutSupportResolver([this](const QString& screenId) {
        // The LIVE capability as an int code (OverlayService::LayoutSupport*
        // constants): None empties the layout list, Placement keeps the
        // classic entries, Templates swaps in the native template cards and
        // drives the overlay's template-aware arms (activeLayoutIdForScreen,
        // isSnappingContextInactive).
        return static_cast<int>(layoutSupportForScreen(screenId));
    });

    // Drag-insert selector capability resolver: same router-based liveness
    // rule as the layout-support resolver above (a disabled scrolling
    // assignment downgrades and the popup reverts to zone layouts). Cleared
    // alongside it in stop().
    m_overlayService->setDragInsertSelectorResolver([this](const QString& screenId) {
        return dragInsertSelectorForScreen(screenId);
    });

    // Strip cards for the strip-mode selector popup, serialized at the seam
    // (OverlayService stays engine-header-free). Cleared alongside the
    // other providers in stop().
    m_overlayService->setStripCardsProvider(
        [this](const QString& screenId, const QString& excludeWindowId) -> QVariantList {
            const auto* scroll = qobject_cast<const PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get());
            if (!scroll || !scroll->isActiveOnScreen(screenId)) {
                return {};
            }
            return stripColumnsToVariantList(scroll->stripSnapshot(screenId, excludeWindowId));
        });
    // The popup mirrors the strip, so it needs the same axis the engine
    // resolved. Taken from stripAxisForScreen rather than re-derived from the
    // screen's aspect: two derivations can disagree on a near-square monitor,
    // and here that would draw a miniature the drop targets do not match.
    // Liveness-gated like the cards provider directly above, and for the same
    // reason: stripIsVertical is public on the overlay service, so a screen
    // this engine does not own must answer "no vertical strip" rather than the
    // axis the engine WOULD resolve from that screen's shape. Every screen the
    // popup draws a strip for is in this set (the mode push that adds it is
    // what turns the strip selector on), so the gate cannot suppress a real
    // answer. Cleared alongside the other providers in stop().
    m_overlayService->setStripAxisProvider([this](const QString& screenId) -> bool {
        const auto* scroll = qobject_cast<const PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get());
        return scroll && scroll->isActiveOnScreen(screenId) && scroll->stripAxisForScreen(screenId).isVertical();
    });
    // The layout picker's per-screen row builds template cards too, and a
    // card drawn the other way depicts a shape that screen will never show —
    // same provider shape, same liveness gate, same stop() clear.
    if (m_unifiedLayoutController) {
        m_unifiedLayoutController->setStripAxisProvider([this](const QString& screenId) -> bool {
            const auto* scroll = qobject_cast<const PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get());
            return scroll && scroll->isActiveOnScreen(screenId) && scroll->stripAxisForScreen(screenId).isVertical();
        });
    }

    // Autotile provider. setContextGapProvider is derived-only
    // (AutotileEngine); m_autotileEngine is held as the base
    // PlacementEngineBase, so use the derived `autotileEngine` pointer
    // captured above — the std::move into m_autotileEngine transferred
    // ownership but not the pointee, so it still points at the live engine.
    // CONTRACT: createEngines() above always constructs both engines and
    // initCoreAdaptors() ran first, so autotileEngine / snapEngine and the
    // adaptor members are non-null throughout this method — no per-use guards.
    // Autotile's scrolling defer term, the reciprocal of scroll's autotile
    // term above and subject to the same liveness requirement: the claiming
    // side (ScrollEngine::claimCrossScreenReopen) checks its own live screen
    // set, so the defer must ask mode AND liveness or a disagreement leaves
    // the window unmanaged by both engines.
    autotileEngine->setScrollingModeResolver(
        [this, scrollEngine](const QString& screenId, int desktop, const QString& activity) {
            return m_layoutManager
                && m_layoutManager->modeForScreen(screenId, desktop, activity)
                == PhosphorZones::AssignmentEntry::Mode::Scrolling
                && scrollEngine->isActiveOnScreen(screenId);
        });

    autotileEngine->setContextGapProvider([this](const QString& screenId) -> QVariantMap {
        if (!m_layoutManager || screenId.isEmpty()) {
            return {};
        }
        // This is the autotile gap path, so resolve against the "tiling"
        // placement mode — a per-mode `Mode Equals "tiling"` gap rule then
        // applies here and a "snapping" one stays inert. The same
        // GeometryUtils::contextGapOverrideMap shaping the snap provider uses
        // below keeps the two paths byte-identical (PerScreenKeys form, with
        // the per-side toggle gating the per-side entries). The config per-
        // monitor gap is merged UNDER the rule override so a user gap rule
        // still wins per slot.
        return GeometryUtils::mergeConfigPerScreenGaps(
            GeometryUtils::contextGapOverrideMap(m_layoutManager->resolveContextGaps(
                screenId, currentDesktopForScreen(screenId), currentActivity(), QStringLiteral("tiling"))),
            m_settings.get(), screenId);
    });

    // Build the PhosphorContext::ContextResolver wiring NOW — after the
    // workspace managers, settings, and router exist; before any D-Bus
    // adaptor or OverlayService method that consumes it runs. Three
    // narrow adapters one-line forward to the existing services; the
    // resolver borrows them. Declaration order in daemon.h guarantees
    // reverse-tear-down: resolver first, then adapters, then services.
    m_workspaceStateAdapter =
        std::make_unique<DaemonWorkspaceStateAdapter>(m_virtualDesktopManager.get(), m_activityManager.get());
    m_screenModeAdapter = std::make_unique<DaemonScreenModeAdapter>(m_screenModeRouter.get());
    m_settingsGateAdapter = std::make_unique<DaemonSettingsGateAdapter>(m_settings.get(), m_layoutManager.get());
    m_contextResolver = std::make_unique<PhosphorContext::ContextResolver>(
        m_workspaceStateAdapter.get(), m_screenModeAdapter.get(), m_settingsGateAdapter.get());
    m_overlayService->setContextResolver(m_contextResolver.get());

    // Late-bind the resolver into consumers that gate their
    // handlers on the disable/lock cascade. Each adaptor was constructed
    // earlier (before m_settings/m_screenModeRouter were ready); the
    // resolver only exists now. The setters mirror setAutotileEngine /
    // setShortcutRegistrar / setScreenModeRouter — same late-binding
    // pattern the daemon already uses for cross-cutting deps.
    m_windowDragAdaptor->setContextResolver(m_contextResolver.get());
    m_windowTrackingAdaptor->setContextResolver(m_contextResolver.get());
    // m_snapAdaptor is constructed below at the engine-adaptor block; its
    // contextResolver wire lives there.

    connect(autotileEngine, &PhosphorEngine::PlacementEngineBase::settingsPersistRequested, this, [this]() {
        if (m_settings) {
            m_settings->save();
        }
    });
    // Wired for symmetry, not because it fires today: the only producer of
    // settingsPersistRequested is AutotileEngine's write-back guard timer, and
    // the scroll engine emits it nowhere. Kept so a scroll-side write-back
    // lands with its persistence already connected rather than silently
    // dropping, which is the failure this signal exists to prevent.
    connect(scrollEngine, &PhosphorEngine::PlacementEngineBase::settingsPersistRequested, this, [this]() {
        if (m_settings) {
            m_settings->save();
        }
    });

    autotileEngine->refreshConfigFromSettings();
    scrollEngine->refreshConfigFromSettings();

    // Give the window drag adaptor access to the autotile engine for per-screen
    // autotile checks (overlay suppression and snap rejection on autotile screens).
    // Uses the base-class pointer — WDA only needs isActiveOnScreen().
    m_windowDragAdaptor->setAutotileEngine(m_autotileEngine.get());
    m_windowDragAdaptor->setScrollEngine(m_scrollEngine.get());

    // SnapEngine owns its per-(screen,desktop,activity) snap stores (symmetric with
    // AutotileEngine/TilingState). Wire the WTS facade through the engine's resolver
    // seam so each windowId-keyed query reaches the store that owns the window and
    // each screen-carrying write reaches — and registers — the store for that screen.
    {
        PhosphorPlacement::WindowTrackingService::SnapStateResolver snapResolver;
        snapResolver.forWindow = [e = QPointer(snapEngine)](const QString& id) -> PhosphorSnapEngine::SnapState* {
            return e ? e->stateForWindow(id) : nullptr;
        };
        snapResolver.forWindowOnScreen =
            [e = QPointer(snapEngine)](const QString& id, const QString& screenId) -> PhosphorSnapEngine::SnapState* {
            return e ? e->stateForWindowOnScreen(id, screenId) : nullptr;
        };
        snapResolver.forScreen = [e = QPointer(snapEngine)](const QString& screenId) -> PhosphorSnapEngine::SnapState* {
            return e ? static_cast<PhosphorSnapEngine::SnapState*>(e->stateForScreen(screenId)) : nullptr;
        };
        snapResolver.globals = [e = QPointer(snapEngine)]() -> PhosphorSnapEngine::SnapState* {
            return e ? e->globalState() : nullptr;
        };
        snapResolver.allStates = [e = QPointer(snapEngine)]() -> QList<PhosphorSnapEngine::SnapState*> {
            return e ? e->allSnapStates() : QList<PhosphorSnapEngine::SnapState*>{};
        };
        snapResolver.forgetWindow = [e = QPointer(snapEngine)](const QString& id) {
            if (e) {
                e->forgetWindow(id);
            }
        };
        m_windowTrackingAdaptor->service()->setSnapStateResolver(std::move(snapResolver));
    }
    m_windowTrackingAdaptor->service()->setSnapEngine(snapEngine);
    // Inject the shared window registry so each SnapState canonicalizes its
    // windowId-keyed stores to the stable first-seen composite (instanceId →
    // first observed appId|instanceId). This makes snap float/zone/screen state
    // immune to the effect-restart-after-WM_CLASS-mutation re-identification
    // skew, mirroring how AutotileEngine canonicalizes tiling state (issue #628).
    snapEngine->setWindowRegistry(m_windowRegistry.get());

    // Filter the unified rule store down to its placement-exclusion slice
    // (Exclude ∪ ExcludePlacement) and hand the address to SnapEngine for
    // its isAppIdExcluded probe. The
    // filtered slice is held as a stable Daemon member (m_excludeRuleSet)
    // and refreshed in-place via setRules so the bound RuleEvaluator's
    // per-revision sort index and resolve cache actually invalidate on
    // each rules-changed edit (a copy-assigned fresh RuleSet would
    // re-import revision=1 every cycle, freezing the cache on the next
    // resolveCached-bearing migration of the call sites). Rebuilt
    // whenever the unified store emits rulesChanged, so a settings-app
    // rule edit propagates without a manual refresh.
    //
    // Initial wiring happens once below, outside the rulesChanged lambda:
    //   - `setExcludeRuleSet(&m_excludeRuleSet)` hands SnapEngine the
    //     stable address. The pointer never changes after this; the
    //     evaluator picks up subsequent in-place edits through the
    //     revision counter, so subsequent re-fences would be no-ops.
    //   - The first `setRules` + `pruneExcludedPendingRestores` priming
    //     pair seeds the filter and drains any restore queue entries
    //     populated by WTA::loadState above.
    // m_ruleStore is ctor-owned and non-null for the daemon's lifetime (same
    // one-comment contract as m_overlayService above), so this function
    // derefs it unguarded; the refilter lambda's null check below exists
    // only for a future refactor that moves store ownership.
    snapEngine->setExcludeRuleSet(&m_excludeRuleSet);
    m_excludeRuleSet.setRules(PhosphorRules::ExclusionRules::excludePlacementRulesFrom(m_ruleStore->ruleSet()).rules());
    m_windowTrackingAdaptor->pruneExcludedPendingRestores(
        PhosphorRules::ExclusionRules::applicationExcludePatternsFrom(m_excludeRuleSet));

    auto refilterExcludeRules = [this, snapEnginePtr = QPointer(snapEngine)] {
        // QPointer null-checks defend the rulesChanged subscription
        // against the shutdown window where m_snapEngine.reset() has
        // already fired but the subscription has not yet auto-
        // disconnected via ~Daemon (the connection's `this`-context only
        // breaks on Daemon destruction, not on a member reset). Mirrors
        // the QPointer pattern used by the persistence-delegate and
        // signal-relay lambdas below.
        if (!snapEnginePtr) {
            // Deliberate coupling: a null engine also freezes the slice and
            // skips the prune. Correct today — the engine is only null after
            // stop(), where the WTA is being torn down too, and the next
            // init() re-primes both unconditionally.
            return;
        }
        // Symmetric guard for the rule store. `m_ruleStore` is a
        // unique_ptr owned by Daemon, so it currently shares Daemon's
        // lifetime; the guard exists so a future refactor that drops
        // and re-creates the store on the fly (or moves ownership
        // out) can't UAF this lambda.
        if (!m_ruleStore) {
            return;
        }
        // Equality-guard against no-op edits: every rulesChanged emission
        // (rename, priority change, non-placement-exclusion action edit, …) fires
        // this lambda, but only changes that affect the placement-exclusion
        // slice (Exclude ∪ ExcludePlacement) should bump the evaluator's
        // revision and walk the (potentially long) pending-restore queues.
        // The guard below compares the two `QList<Rule>` slices element-wise
        // (the same semantics as `RuleSet::operator==`, which delegates to
        // this list compare) — exactly the rules-list-only comparison we want.
        const QList<PhosphorRules::Rule> newSlice =
            PhosphorRules::ExclusionRules::excludePlacementRulesFrom(m_ruleStore->ruleSet()).rules();
        if (newSlice == m_excludeRuleSet.rules()) {
            return;
        }
        // Cache invalidation for matched windows happens through the
        // `setRules` revision bump; the evaluator inside SnapEngine
        // reads `m_excludeRuleSet`'s revision and drops its per-revision
        // index / cache automatically. No `setExcludeRuleSet` re-fence
        // — the pointer was wired once at init above.
        m_excludeRuleSet.setRules(newSlice);
        // Prune any pending-restore queues for apps now covered by an
        // Exclude or ExcludePlacement rule. Snap-engine's resolveWindowRestore
        // already refuses them at runtime, but stale queue entries spam logs and bloat the
        // saved state. The autotile-side queues don't exist yet at init
        // — daemon/signals.cpp's finalizeStartup re-runs the prune once
        // AutotileEngine::loadState has populated them.
        if (m_windowTrackingAdaptor) {
            // Shutdown-window guard, mirrors snapEnginePtr null-check above.
            m_windowTrackingAdaptor->pruneExcludedPendingRestores(
                PhosphorRules::ExclusionRules::applicationExcludePatternsFrom(m_excludeRuleSet));
        }
    };
    // The rule store is constructed once in the Daemon ctor and SURVIVES a
    // stop() → init() cycle, so re-wiring here would stack duplicates of the
    // three rulesChanged subscriptions below (triple refilter/refresh/
    // reconcile per edit after each cycle). Sever every rulesChanged
    // connection targeting the daemon first; all of them are (re)established
    // right here.
    disconnect(m_ruleStore.get(), &PhosphorRules::RuleStore::rulesChanged, this, nullptr);
    connect(m_ruleStore.get(), &PhosphorRules::RuleStore::rulesChanged, this,
            [refilterExcludeRules](bool /*persisted*/) {
                refilterExcludeRules();
            });

    // A rule edit can change the live context-lock state (e.g. toggling,
    // re-prioritising or re-matching a LockContext rule) without touching the
    // manual lock store, so the ISettings::settingsChanged refresh that keeps
    // open zone selectors / the layout picker in sync would miss it. Re-push
    // the lock state to any open overlay on every rule change. QPointer guards
    // the shutdown window (overlay reset before ~Daemon disconnects).
    //
    // Deliberately UNCONDITIONAL — no slice-equality guard like the exclude path
    // above. That guard exists because its work is expensive (walking long
    // pending-restore queues + bumping an evaluator revision); these two
    // refreshes are cheap and self-bounding: refreshContextLockState only acts
    // on live selector/picker slots, and refreshOverlayPropertiesIfShown
    // early-returns unless the overlay is currently shown. rulesChanged also
    // only fires on a real store change (setAllRules no-ops when equal), so a
    // whole-set guard would never trip; a lock/overlay-only slice extractor
    // would be the only guard that could, and it isn't worth the machinery here.
    connect(m_ruleStore.get(), &PhosphorRules::RuleStore::rulesChanged, this,
            [overlay = QPointer(m_overlayService.get())](bool /*persisted*/) {
                if (overlay) {
                    overlay->refreshContextLockState();
                    // A rule change can also alter the resolved overlay shader /
                    // style for the active context; re-apply it live if the
                    // overlay is currently shown (no-op otherwise).
                    overlay->refreshOverlayPropertiesIfShown();
                }
            });

    // A rule edit that changes the ACTIVE context's resolved assignment (engine
    // mode / snapping layout / tiling algorithm) must move live windows — resnap
    // snapping screens, retile autotile screens. The legacy assignment-apply path
    // (assignmentChangesApplied) only fires for setAssignmentEntry-driven edits,
    // so rule-driven changes were silently not applied. reconcileActiveAssignments
    // diffs the per-screen active assignment and drives the same apply path for
    // the screens that actually changed (a no-op for appearance/exclude/lock
    // edits, which don't alter the active assignment).
    //
    // DEFERRED, never inline: rulesChanged is emitted synchronously from
    // inside every store mutation, and the daemon's own assignment writes
    // (mode toggle, quick layouts, KCM batch) are stored as rules via the
    // ContextRuleBridge. Reconciling inline re-entered the full KCM
    // assignment-apply path in the middle of the write's own apply —
    // duplicate OSDs, a duplicate resnapToNewLayout, and a resnap that
    // raced the engine flip (dolphin snapped to a zone rect on a screen
    // mid-flip into scrolling). One event-loop pass later the write's
    // layoutAssigned tail has re-primed m_activeAssignmentByScreen, so a
    // self-inflicted edit diffs empty and only genuinely external rule
    // edits (D-Bus setAllRules / file reload) still move windows. The
    // pending flag compresses a mutation burst (KCM batch) into one pass.
    connect(m_ruleStore.get(), &PhosphorRules::RuleStore::rulesChanged, this, [this](bool /*persisted*/) {
        if (m_reconcileAssignmentsPending) {
            return;
        }
        m_reconcileAssignmentsPending = true;
        QTimer::singleShot(0, this, [this]() {
            m_reconcileAssignmentsPending = false;
            // A rule edit landing in the same event-loop turn as stop()
            // leaves this single-shot queued past teardown; without the gate
            // a non-empty diff would drive a full assignment-apply pass on a
            // stopped daemon (m_layoutAdaptor is Qt-parented and outlives
            // the per-sender connection sweep).
            if (m_shuttingDown) {
                return;
            }
            reconcileActiveAssignments();
        });
    });

    // A system colour-scheme flip changes Field::ColorScheme rule verdicts
    // WITHOUT a rule-set revision bump. The registry's cached context
    // resolvers self-heal through the |cs: cache-key component; what does not
    // self-heal is state already applied from an old verdict, so mirror the
    // rulesChanged re-resolution: drop the WTA's per-window rule memos,
    // reconcile the per-screen assignments (same deferred pending-flag shape
    // as above) and refresh the live selector / overlay surfaces.
    //
    // Duplicate-stacking is prevented by registering the handle in the shared
    // settings-wiring list rather than by a (sender, signal, receiver) sweep:
    // init() drops that list wholesale at the top of
    // initLayoutAndSettingsWiring, which runs earlier in the same pass, so a
    // stop() -> init() cycle drops last cycle's handle before this line
    // reinstalls it. The sweep this replaced was only safe while the daemon was
    // the sole subscriber to the signal.
    m_layoutSettingsWiringConnections.append(
        connect(m_settings.get(), &ISettings::systemColorSchemeChanged, this, [this]() {
            // FIRST, and synchronously: the WTA's per-window rule memos are keyed
            // on (window id, rule revision) and a fixed field list, neither of
            // which moves on a scheme flip, so every window-rule verdict resolved
            // from a ColorScheme condition is stale until they are dropped. This
            // is a map clear plus a cache clear, and it has to precede anything
            // below that could re-resolve and re-seed them with the old token.
            if (m_windowTrackingAdaptor) {
                m_windowTrackingAdaptor->invalidateRuleMemosForColorSchemeChange();
            }
            // The effect's tab-colour cache is the same staleness with a
            // different owner: a scheme flip moves ColorScheme-conditioned
            // verdicts with no revision bump, so it must re-query too. Emitted
            // right after the memo clear, so the re-queries this triggers
            // resolve against the new token.
            if (m_tilingAdaptor) {
                m_tilingAdaptor->relayScrollTabColorsChanged();
            }
            // The overlay refreshes are DEFERRED, unlike the inline shape this
            // block inherited from the rulesChanged twin. They are not the cheap,
            // self-bounding reads that shape assumed: refreshOverlayPropertiesIfShown
            // can reach recreateOverlayWindowsOnTypeMismatch, which destroys and
            // recreates QQuickWindows and reloads their Loaders — and this handler
            // runs while the application's palette-change event is still being
            // delivered. One event-loop pass later the palette has settled.
            //
            // Its OWN latch rather than the reconcile's: sharing that flag would
            // let a rule edit landing in the same turn swallow the scheme flip's
            // refresh entirely and leave a visible overlay on the old palette.
            if (!m_colorSchemeRefreshPending) {
                m_colorSchemeRefreshPending = true;
                QTimer::singleShot(0, this, [this]() {
                    m_colorSchemeRefreshPending = false;
                    if (m_shuttingDown || !m_overlayService) {
                        return;
                    }
                    m_overlayService->refreshContextLockState();
                    m_overlayService->refreshOverlayPropertiesIfShown();
                });
            }
            if (m_reconcileAssignmentsPending) {
                return;
            }
            m_reconcileAssignmentsPending = true;
            QTimer::singleShot(0, this, [this]() {
                m_reconcileAssignmentsPending = false;
                if (m_shuttingDown) {
                    return;
                }
                reconcileActiveAssignments();
            });
        }));

    // Deleting a layout moves resolved assignments without touching a single
    // rule: the assignment entry keeps naming the dead uuid, the cascade stops
    // resolving it, and the level-1 default provider refuses it too (see the
    // dead-id guard in initLayoutAndSettingsWiring). Nothing on the rulesChanged
    // path fires for that, so the published active-layout map kept naming a
    // layout the user removed and the effect's ActiveLayout matcher with it.
    // Republish only — no reconcile: the resnap/OSD apply belongs to assignment
    // edits, and diffActiveAssignments reads layouts without mutating them, so
    // this cannot re-enter layoutsChanged.
    //
    // Disconnect-first for the same reason as the rulesChanged sweep above:
    // m_layoutManager survives a stop() → init() cycle, and it is a mixed sender
    // that stop() deliberately does not blanket-sever. No other daemon-receiver
    // handler sits on this signal, so the targeted sweep only drops our own.
    disconnect(m_layoutManager.get(), &PhosphorZones::LayoutRegistry::layoutsChanged, this, nullptr);
    connect(m_layoutManager.get(), &PhosphorZones::LayoutRegistry::layoutsChanged, this, [this]() {
        diffActiveAssignments();
    });

    // Prime the snapshot from the initial rule set so the first real rule edit
    // diffs against the live assignments rather than an empty baseline.
    diffActiveAssignments();

    // Wire persistence delegate — SnapEngine delegates save/load to WTA's KConfig layer.
    // QPointer guards against late calls during shutdown if WTA is destroyed first.
    snapEngine->setPersistenceDelegate(
        [wta = QPointer(m_windowTrackingAdaptor)]() {
            if (wta)
                wta->saveState();
        },
        [wta = QPointer(m_windowTrackingAdaptor)]() {
            if (wta)
                wta->loadState();
        });

    // Wire engine cross-references (SnapEngine ↔ AutotileEngine, zone detection).
    m_windowTrackingAdaptor->setEngines(snapEngine, autotileEngine, scrollEngine);

    // ───────────────────────────────────────────────────────────────────────────
    // Per-engine float state (root fix for the shared-bit float defect).
    //
    // Float state is genuinely per-engine: a window floated in autotile mode is
    // NOT floating in snapping mode and vice versa. The authoritative store lives
    // in each engine (SnapEngine→SnapState::isFloating / AutotileEngine→
    // TilingState::isFloating). WTS is engine-agnostic (LGPL boundary), so we
    // inject a resolver (reader) and writer that route to the engine owning the
    // window's CURRENT screen mode. This replaces the old single shared
    // m_floatingWindows + m_snapState bit that both engines read/wrote.
    //
    // Mode resolution: the window's tracked screen (WTS screenForWindow; for
    // windows snap never saw, the no-screen fallback below resolves a MODE
    // directly — Autotile when that engine tracks the window, else Snapping)
    // → LayoutRegistry::modeForScreen → the owning engine.
    //
    // Resolved at the WINDOW's OWN desktop and activity (registry context),
    // not the screen's current ones. Those are per-window data, never the
    // context key: reading through the screen's CURRENT desktop/activity
    // made the effective float answer flip when a per-output desktop switch
    // (or an activity switch) crossed a snap↔autotile mode boundary — with
    // no windowFloatingChanged broadcast, stranding every flat float mirror
    // (the effect's FloatingCache) until a daemon reconnect. A window on the
    // screen's current desktop/activity (and the sticky / unknown cases:
    // virtualDesktop 0, empty activity) resolves exactly as before via the
    // fallbacks. The shared lambda serves the float WRITER and the
    // autotile-mode predicate too, so those routing decisions shift to the
    // window's own context along with the reader — deliberate: all three
    // answer "which engine owns this window", and that has one answer.
    {
        auto modeForWindowOnScreen =
            [this, autotilePtr = QPointer(autotileEngine), scrollTrackPtr = QPointer(scrollEngine)](
                const QString& windowId, const QString& screenOverride) -> PhosphorZones::AssignmentEntry::Mode {
            QString screenId = screenOverride;
            const PhosphorPlacement::WindowTrackingService* wts = nullptr;
            if (m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()) {
                wts = m_windowTrackingAdaptor->service();
                if (screenId.isEmpty()) {
                    screenId = wts->screenForWindow(windowId);
                }
            }
            if (!screenId.isEmpty() && m_layoutManager) {
                const int screenCurrent = currentDesktopForScreen(screenId);
                int desktop = screenCurrent;
                QString activity = currentActivity();
                if (wts && wts->windowRegistry()) {
                    const auto ctx =
                        wts->windowRegistry()->windowContext(::PhosphorIdentity::WindowId::extractInstanceId(windowId));
                    if (ctx) {
                        // Own-desktop / multi-desktop-span / sticky policy
                        // lives on WindowContext (see effectiveDesktop's doc);
                        // this resolver just supplies the screen-current
                        // fallbacks.
                        desktop = ctx->effectiveDesktop(screenCurrent);
                        activity = ctx->effectiveActivity(activity);
                    }
                }
                return m_layoutManager->modeForScreen(screenId, desktop, activity);
            }
            // No tracked screen in WTS (e.g. a window snap never saw): if a
            // strip/tiling engine tracks it, that engine's mode wins.
            // Otherwise default to Snapping — the historical no-context
            // fallback.
            if (autotilePtr && autotilePtr->isWindowTracked(windowId)) {
                return PhosphorZones::AssignmentEntry::Autotile;
            }
            if (scrollTrackPtr && scrollTrackPtr->isWindowTracked(windowId)) {
                return PhosphorZones::AssignmentEntry::Scrolling;
            }
            return PhosphorZones::AssignmentEntry::Snapping;
        };
        auto screenModeForWindow =
            [modeForWindowOnScreen](const QString& windowId) -> PhosphorZones::AssignmentEntry::Mode {
            return modeForWindowOnScreen(windowId, QString());
        };

        // Owning-engine-id resolver for synthesized slots (recordFloatingClose,
        // the minimize preserve): same screen→mode resolution as the float
        // routing above, but keyed on an EXPLICIT screen — those call sites
        // hold the authoritative close screen, and the window's tracked screen
        // may already be stale or gone at that point.
        m_windowTrackingAdaptor->service()->setModeEngineIdResolver(
            [modeForWindowOnScreen](const QString& windowId, const QString& screenId) -> QString {
                switch (modeForWindowOnScreen(windowId, screenId)) {
                case PhosphorZones::AssignmentEntry::Autotile:
                    return QString(PhosphorEngine::WindowPlacement::autotileEngineId());
                case PhosphorZones::AssignmentEntry::Scrolling:
                    return QString(PhosphorEngine::WindowPlacement::scrollingEngineId());
                case PhosphorZones::AssignmentEntry::Snapping:
                    break;
                }
                return QString(PhosphorEngine::WindowPlacement::snapEngineId());
            });

        m_windowTrackingAdaptor->service()->setEngineFloatResolver(
            [screenModeForWindow, snapEnginePtr = QPointer(snapEngine), autotilePtr = QPointer(autotileEngine),
             scrollPtr = QPointer(scrollEngine)](const QString& windowId) -> bool {
                switch (screenModeForWindow(windowId)) {
                case PhosphorZones::AssignmentEntry::Autotile:
                    return autotilePtr && autotilePtr->isWindowFloatingInAutotile(windowId);
                case PhosphorZones::AssignmentEntry::Scrolling:
                    return scrollPtr && scrollPtr->isWindowFloatingInScroll(windowId);
                case PhosphorZones::AssignmentEntry::Snapping:
                    break;
                }
                return snapEnginePtr && snapEnginePtr->isFloating(windowId);
            });

        m_windowTrackingAdaptor->service()->setEngineFloatWriter(
            [screenModeForWindow, snapEnginePtr = QPointer(snapEngine)](const QString& windowId, bool floating) {
                // Write ONLY the snap engine's authoritative float store, and
                // only for snap-mode windows. The engines keep INDEPENDENT
                // float state — writing the snap bit for an autotile-mode window
                // is exactly the cross-mode leak this refactor eliminates.
                //
                // Autotile-mode windows are intentionally a no-op here:
                // TilingState::isFloating is the autotile engine's authoritative
                // float store and is already set by the engine itself (via
                // performToggleFloat / setWindowFloat) BEFORE any daemon sync
                // calls WTS::setWindowFloating. Re-driving setWindowFloat here
                // would re-toggle the float and retile — so the engine stays the
                // sole owner of its own float bit.
                // The scrolling engine keeps sole ownership of its float bit
                // for the same reason autotile does: the engine flips its
                // own state before any daemon sync reaches WTS.
                if (screenModeForWindow(windowId) != PhosphorZones::AssignmentEntry::Snapping) {
                    return;
                }
                if (snapEnginePtr) {
                    snapEnginePtr->setFloating(windowId, floating);
                }
            });

        m_windowTrackingAdaptor->service()->setEngineFloatLister([snapEnginePtr = QPointer(snapEngine),
                                                                  autotilePtr = QPointer(autotileEngine),
                                                                  scrollPtr = QPointer(scrollEngine)]() -> QStringList {
            QStringList all;
            if (snapEnginePtr) {
                all += snapEnginePtr->floatingWindows();
            }
            if (autotilePtr) {
                all += autotilePtr->allFloatingWindows();
            }
            if (scrollPtr) {
                all += scrollPtr->allFloatingWindows();
            }
            return all;
        });

        // Owning-engine predicate: WTS answers isWindowInAutotileMode with this
        // (the single owning-engine signal for the capture funnel + float
        // routing), using the same screen→mode resolution as the float resolver
        // above. Float-back geometry itself is single-sourced from the unified
        // WindowPlacementStore, so no per-engine geometry wiring is needed.
        m_windowTrackingAdaptor->service()->setAutotileModePredicate(
            [screenModeForWindow](const QString& windowId) -> bool {
                return screenModeForWindow(windowId) == PhosphorZones::AssignmentEntry::Autotile;
            });

        // Tiled predicate (distinct from the MODE predicate above): live
        // engine state, "is this window actively tiled right now". Guards
        // recordFreeGeometry against recording a tile rect as a float-back —
        // the engine-backed answer survives effect reloads, which the
        // effect-side capture guard cannot. Covers BOTH tiling-family
        // engines: a scroll column rect recorded as float-back is the same
        // poison class the guard exists for.
        m_windowTrackingAdaptor->service()->setEngineTiledPredicate(
            [autotilePtr = QPointer(autotileEngine),
             scrollPtr = QPointer(scrollEngine)](const QString& windowId) -> bool {
                return (autotilePtr && autotilePtr->isWindowTiled(windowId))
                    || (scrollPtr && scrollPtr->isWindowTiled(windowId));
            });
    }

    // Wire SnapEngine's back-reference to the window tracking adaptor.
    // SnapEngine's navigation methods (focusInDirection, moveFocusedInDirection, …)
    // were moved out of WindowTrackingAdaptor and need to reach back into the
    // adaptor for shared state that hasn't been migrated yet: the target
    // resolver, the last-active window/screen shadow, and the snap-
    // bookkeeping helpers (windowSnapped, windowUnsnapped, recordSnapIntent,
    // clearPreTileGeometry). A future refactor should move that state onto
    // SnapEngine or PhosphorPlacement::WindowTrackingService and retire the back-reference.
    snapEngine->setNavigationStateProvider(m_windowTrackingAdaptor);

    // Clear the stale mode-specific float marker of EVERY tiling engine when
    // a window is snapped. A window dragged from a tiling VS to a snap VS
    // retains that engine's float marker; without this, a subsequent mode
    // change on the tiling VS incorrectly processes the already-snapped
    // window as engine-managed. Both engines implement the marker in their
    // own address space and both are reachable by such a drag, so both are
    // swept — like every other cross-engine site here.
    // Wired here (daemon) because engines must not know about each other.
    connect(snapEngine, &PhosphorSnapEngine::SnapEngine::windowSnapStateChanged, this,
            [this](const QString& windowId, const PhosphorProtocol::WindowStateEntry&) {
                for (PhosphorEngine::PlacementEngineBase* engine : {m_autotileEngine.get(), m_scrollEngine.get()}) {
                    if (engine) {
                        engine->clearModeSpecificFloatMarker(windowId);
                    }
                }
            });

    // ScreenModeRouter was created by createEngines() above; wire it to WTA.
    m_windowTrackingAdaptor->setScreenModeRouter(m_screenModeRouter.get());

    // m_virtualScreenStore is constructed in the initializer list (it's a
    // Config arg for m_screenManager). The swapper is constructed here
    // because navigation handlers don't run before init() returns anyway.
    m_virtualScreenSwapper = std::make_unique<PhosphorScreens::VirtualScreenSwapper>(m_virtualScreenStore.get());

    // Wire autotile persistence through WTA's KConfig layer (same delegate pattern as SnapEngine).
    // Note: engine->saveState() intentionally triggers a full WTA save (all window tracking
    // state, not just autotile). This is heavier than a targeted save but ensures consistency
    // — the autotile window orders are embedded in WTA's save cycle via the serialization
    // delegates below. The engine-level delegates exist to satisfy the IPlacementEngine interface.
    // QPointer guards against late calls during shutdown if WTA is destroyed first.
    autotileEngine->setPersistenceDelegate(
        [wta = QPointer(m_windowTrackingAdaptor)]() {
            if (wta)
                wta->saveState();
        },
        [wta = QPointer(m_windowTrackingAdaptor)]() {
            if (wta)
                wta->loadState();
        });
    // Autotile restore persistence (window orders + pending restores) is now
    // subsumed by the unified WindowPlacementStore — an autotiled window's position
    // is one WindowPlacement record, captured by the common save-time snapshot and
    // close hook and restored on reopen by AutotileEngine::insertWindow. Like snap,
    // there is no engine-specific serialize delegate.

    // Trigger a placement save when the autotile layout changes (window added /
    // removed / reordered / floated). markDirty(DirtyWindowPlacements) emits
    // stateChanged → scheduleSaveState (wired in the adaptor ctor), and saveState's
    // refreshOpenWindowPlacements re-captures every open window's current placement
    // (including autotiled positions) into the unified store before writing. This
    // placementChanged bridge is autotile-specific: snap captures directly on
    // windowSnapStateChanged → captureWindowPlacement, whereas autotile has no
    // per-window signal, so its per-screen placementChanged schedules the save and
    // the save-time snapshot does the per-window capture.
    connect(autotileEngine, &PhosphorEngine::PlacementEngineBase::placementChanged, m_windowTrackingAdaptor, [this]() {
        if (m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()) {
            m_windowTrackingAdaptor->service()->markDirty(
                PhosphorPlacement::WindowTrackingService::DirtyWindowPlacements);
        }
    });
    // Scroll strips have the same no-per-window-signal shape as autotile:
    // per-screen placementChanged schedules the save, and the save-time
    // snapshot captures each window's strip slot into the unified store.
    // DirtyScrollStrips rides along: every structural strip change (insert,
    // consume/expel, tab toggle, resize) ends in a relayout that emits
    // placementChanged, so this one mark keeps the durable strip snapshot
    // (serializeStripState via the provider below) in step with the store.
    connect(scrollEngine, &PhosphorEngine::PlacementEngineBase::placementChanged, m_windowTrackingAdaptor, [this]() {
        if (m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()) {
            m_windowTrackingAdaptor->service()->markDirty(
                PhosphorPlacement::WindowTrackingService::DirtyWindowPlacements
                | PhosphorPlacement::WindowTrackingService::DirtyScrollStrips);
        }
    });
    // Strip-structure persistence: the adaptor pulls the snapshot at write
    // time; the engine re-stages the loaded blob into its arrival-restore
    // stash. The adaptor's ctor loadState already ran (engines did not exist
    // yet), so hand that blob over NOW — before the effect's re-announce
    // batch delivers the first windowOpened — and keep the delegate's load
    // path handing it again after any later reload (restoreStripState is
    // additive and skips adopted contexts, so the second call is safe).
    // Only the cross-session RESTORE is gated on
    // scrollingRestoreStripsOnLogin, read live at each firing. The snapshot
    // WRITE always runs: WTA's saveState reads an empty live provider as
    // "this session has no strips" and deleteKey's the stored blob, so a
    // gated provider would destroy the on-disk snapshot on the first save
    // after the user flips the switch off — turning it back on would then
    // have nothing to restore. Keeping the write means the switch decides
    // whether a snapshot is USED, not whether one exists. (Aging the
    // per-tile unclaimedSessions lease is not a reason to keep writing:
    // the lease only ages for tiles restoreStripState staged, and with the
    // read gated nothing is staged.) In-session mode round-trips
    // (stashStripStructure) are deliberately NOT gated either — this switch
    // is about logins, and the stash path never goes through these lambdas.
    m_windowTrackingAdaptor->setScrollStripStateProvider([engine = QPointer(scrollEngine)]() {
        return engine ? engine->serializeStripState() : QJsonObject();
    });
    if (m_settings && m_settings->scrollingRestoreStripsOnLogin()) {
        scrollEngine->restoreStripState(m_windowTrackingAdaptor->loadedScrollStripState());
    }
    scrollEngine->setPersistenceDelegate(
        [wta = QPointer(m_windowTrackingAdaptor)]() {
            if (wta)
                wta->saveState();
        },
        [this, wta = QPointer(m_windowTrackingAdaptor), engine = QPointer(scrollEngine)]() {
            if (wta)
                wta->loadState();
            if (wta && engine && m_settings && m_settings->scrollingRestoreStripsOnLogin())
                engine->restoreStripState(wta->loadedScrollStripState());
        });

    // Re-resolve the per-screen tiling algorithm when a screen's tiled-window
    // count changes, so a Field::TiledWindowCount rule (e.g. a centered
    // single-window layout that gives way once a second window opens) takes
    // effect as windows open and close. Gated on an ACTUAL count change so the
    // per-retile placementChanged stream (drags, resizes) does not re-walk the
    // cascade. A re-resolve that lands on the same count returns the same answer
    // and updateEngineScreens() diffs each screen's overrides before
    // re-applying, so a plain count-keyed switch settles in one step. (A
    // pathological rule whose chosen algorithm caps MaxWindows below the live
    // count would float the excess, drop the count, and could oscillate — that
    // is a self-contradictory config, not a normal one.)
    //
    // ONE gate for both engines, parameterised on which member holds the
    // engine: the two arms were byte-identical apart from that pointer, and
    // the scrolling one only existed so a TiledWindowCount rule keys on
    // scrolling screens too (the provider in init_services consults both).
    const auto onTiledCountChanged = [this](const std::unique_ptr<PhosphorEngine::PlacementEngineBase>& engine,
                                            const QString& screenId) {
        if (!engine) {
            return;
        }
        // const overload: non-creating, returns nullptr (→ count 0) when
        // the screen has no tiling state, so this gate never allocates a
        // phantom state while observing the count.
        const PhosphorEngine::IPlacementState* state = std::as_const(*engine).stateForScreen(screenId);
        const int count = state ? state->tiledWindowCount() : 0;
        // Owner-tagged: a cache entry the OTHER engine wrote never
        // suppresses this engine's first post-flip resolve.
        const auto owned = qMakePair(static_cast<const void*>(engine.get()), count);
        const auto it = m_lastTiledCountByScreen.constFind(screenId);
        if (it != m_lastTiledCountByScreen.constEnd() && it.value() == owned) {
            return; // count unchanged — nothing a count rule could key on moved
        }
        m_lastTiledCountByScreen.insert(screenId, owned);
        updateEngineScreens();
        // A count rule that swaps the screen out of tiling releases its
        // windows in that recompute, and this gate has no resnap of its
        // own to consume the preserved snap-ZONE half.
        flushPendingSnapZoneRestores();
    };
    connect(autotileEngine, &PhosphorEngine::PlacementEngineBase::placementChanged, this,
            [this, onTiledCountChanged](const QString& screenId) {
                onTiledCountChanged(m_autotileEngine, screenId);
            });
    connect(scrollEngine, &PhosphorEngine::PlacementEngineBase::placementChanged, this,
            [this, onTiledCountChanged](const QString& screenId) {
                onTiledCountChanged(m_scrollEngine, screenId);
            });
    // Strip-popup invalidation on structural strip change. Deliberately a
    // SIBLING connect, not a call inside onTiledCountChanged: that lambda
    // early-returns when the (engine, tiledWindowCount) pair is unchanged,
    // which is exactly the column-move case (structure changes, count does
    // not). Every structural strip change (insert, consume/expel, tab toggle,
    // resize, window close) ends in a relayout that emits placementChanged,
    // so a popup rendered from the pre-change card list re-pushes its model
    // and drops its (renumbered, now-stale) selection. The converse does NOT
    // hold — the engine also emits placementChanged on non-structural
    // changes (a pure view-anchor move in applyLayout, two early-exit
    // emits), and those fires drop a live popup pick spuriously; that is
    // accepted as self-healing — the next cursor tick re-selects under the
    // unchanged list — rather than taught to the engine (distinguishing the
    // anchor-only emit would need a new signal contract).
    // During a live drag-insert preview the strip is frozen (detach-once), so
    // this cannot fight the preview. Without it, a window closing on the
    // strip mid-popup left the cards stale and the popup-only drop arm
    // committed indices against a renumbered strip.
    connect(scrollEngine, &PhosphorEngine::PlacementEngineBase::placementChanged, this,
            [this](const QString& screenId) {
                if (m_overlayService) {
                    m_overlayService->refreshStripSelector(screenId);
                }
            });

    // Live-mode resolver for snap's capture gate: the router's
    // live-set-first answer lets a presave capture a screen the cascade
    // already flipped to a tiling mode but no engine claims yet. Cleared
    // in stop() before the router is destroyed.
    snapEngine->setLiveModeResolver([this](const QString& screenId) {
        return m_screenModeRouter ? m_screenModeRouter->modeFor(screenId) : PhosphorZones::AssignmentEntry::Snapping;
    });

    // Create engine D-Bus adaptors — each engine has a dedicated adaptor that
    // connects signals in its constructor (unified pattern for both engines).
    // stop() → init() re-entry: the previous cycle's WHOLE adaptor set
    // (engine and core) is deleted in one dependency-ordered preamble at the
    // top of initCoreAdaptors(), which init() always runs immediately before
    // this function — so these members are null here on a re-cycle.
    m_snapAdaptor = new SnapAdaptor(snapEngine, m_windowTrackingAdaptor, m_settings.get(), this);
    m_snapAdaptor->setContextResolver(m_contextResolver.get());
    // Cross-screen tiling reclaim off the resolveWindowRestore channel. It
    // covers arrivals on SNAP-mode screens, which the tiling dispatch below
    // never hears about — without it a session window KWin dropped on a snap
    // screen would never be offered back to the engine whose record homes
    // it. The client-declared min sizes ride the same D-Bus call (API v9):
    // the adopting engine evaluates its oversized/float verdict ONCE from
    // them, so a 0,0 here left an oversized window tiled for the session.
    // Cleared in stop() and in SnapAdaptor::clearEngine alongside the
    // engines' other injected closures.
    m_snapAdaptor->setCrossScreenTileReclaim(
        [autotile = QPointer<PhosphorTileEngine::AutotileEngine>(autotileEngine),
         scroll = QPointer<PhosphorScrollEngine::ScrollEngine>(scrollEngine)](
            const QString& windowId, const QString& screenId, int minWidth, int minHeight) {
            // QPointer + null check, matching every sibling closure in this
            // file: the hook is cleared in stop() and in SnapAdaptor's
            // clearEngine, but a late D-Bus call racing teardown must not
            // deref a dead engine.
            return (autotile && autotile->claimCrossScreenReopen(windowId, screenId, minWidth, minHeight))
                || (scroll && scroll->claimCrossScreenReopen(windowId, screenId, minWidth, minHeight));
        });
    // Liveness half of the snap engine's cross-screen tile-defer gate: the
    // claiming engines check their own live screen sets, so the deferring
    // side must ask the same question or a disagreement leaves the window
    // unmanaged by every engine.
    snapEngine->setTilingEngineLiveResolver([autotile = QPointer<PhosphorTileEngine::AutotileEngine>(autotileEngine),
                                             scroll = QPointer<PhosphorScrollEngine::ScrollEngine>(scrollEngine)](
                                                PhosphorZones::AssignmentEntry::Mode mode, const QString& screenId) {
        if (mode == PhosphorZones::AssignmentEntry::Mode::Autotile) {
            return autotile && autotile->isActiveOnScreen(screenId);
        }
        if (mode == PhosphorZones::AssignmentEntry::Mode::Scrolling) {
            return scroll && scroll->isActiveOnScreen(screenId);
        }
        return false;
    });
    // org.plasmazones.Tiling is the engine-NEUTRAL transport shared by the
    // whole tiling family (the effect keeps one engine-managed screen set
    // and one tile pipeline; the adaptor routes per screen through
    // IPlacementEngine and never sees a concrete engine type). Each
    // engine's SPECIFIC surface lives on its own sibling adaptor
    // (org.plasmazones.Autotile / org.plasmazones.Scrolling), and every
    // engine-typed connection is made HERE, at the composition root.
    m_tilingAdaptor = new TilingAdaptor(m_screenManager.get(), this);
    // Wire the WTA so the tiling open path can resolve RouteToScreen /
    // RouteToDesktop rules (the rule store + evaluator live on the WTA).
    m_tilingAdaptor->setWindowTrackingAdaptor(m_windowTrackingAdaptor);
    m_tilingAdaptor->setLifecycleEngines({autotileEngine, scrollEngine});
    m_autotileAdaptor = new AutotileAdaptor(autotileEngine, m_algorithmRegistry.get(), this);
    m_scrollingAdaptor = new ScrollingAdaptor(scrollEngine, this);
    connect(autotileEngine, &PhosphorTileEngine::AutotileEngine::windowsTiled, m_tilingAdaptor,
            &TilingAdaptor::relayTileRequestsJson);
    connect(autotileEngine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested, m_tilingAdaptor,
            &TilingAdaptor::focusWindowRequested);
    connect(autotileEngine, &PhosphorEngine::PlacementEngineBase::placementChanged, m_tilingAdaptor,
            &TilingAdaptor::tilingChanged);
    connect(autotileEngine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged, m_tilingAdaptor,
            &TilingAdaptor::relayWindowFloatingChanged);
    connect(autotileEngine, &PhosphorEngine::PlacementEngineBase::windowsReleased, m_tilingAdaptor,
            [adaptor = m_tilingAdaptor](const QStringList& windowIds, const QSet<QString>&) {
                adaptor->relayWindowsReleased(windowIds);
            });
    // LOAD-BEARING (with its scrolling twin below): these two connects are
    // the ONLY drivers of the coalesced managedScreensChanged announce AND
    // its parked-open retry — no unit test pins their existence (that
    // needs a daemon fixture), so dropping either silently disables the
    // whole mid-flip recovery path in production.
    connect(autotileEngine, &PhosphorTileEngine::AutotileEngine::autotileScreensChanged, m_tilingAdaptor,
            [adaptor = m_tilingAdaptor](const QStringList&, bool isDesktopSwitch) {
                adaptor->notifyEngineScreensChanged(isDesktopSwitch);
            });
    // No direct enabledChanged → relayEnabledChanged connect for either
    // engine. Every enabledChanged emit is accompanied by that engine's
    // screens-changed signal from the same call, so the coalesced announce
    // above already relays the flip — and it relays the UNION after both
    // engines have settled. A direct connect instead announced autotile's
    // emptying half first, so on a single-screen tiling→scrolling flip the
    // effect saw enabledChanged(false) a full event-loop pass before the new
    // union and cancelled in-flight unfloat continuations mid-flip (the
    // hazard tilingadaptor.h:100-108 documents). Anything that ever does
    // need a direct relay must route through notifyEngineScreensChanged.
    // Note for whoever wires one: ScrollEngine::enabledChanged carries no
    // wasDesktopSwitch suppression, which is harmless only while it stays
    // unconnected.
    connect(scrollEngine, &PhosphorScrollEngine::ScrollEngine::windowsTiled, m_tilingAdaptor,
            &TilingAdaptor::relayTileRequestsJson);
    connect(scrollEngine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested, m_tilingAdaptor,
            &TilingAdaptor::focusWindowRequested);
    connect(scrollEngine, &PhosphorEngine::PlacementEngineBase::placementChanged, m_tilingAdaptor,
            &TilingAdaptor::tilingChanged);
    connect(scrollEngine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged, m_tilingAdaptor,
            &TilingAdaptor::relayWindowFloatingChanged);
    // The scroll engine manages its float STATE itself, but only the daemon can
    // restore the float-back geometry — floatWindowInternal merely pulls the
    // window out of the strip and windowsTiled never carries release entries, so
    // without this the window sits frozen at its old column rect after Meta+F.
    //
    // This is the ACTIVE arm and windowFloatingChanged now carries only the two
    // user float actions (floatWindowInternal / unfloatWindowInternal). The
    // engine's own-initiative transitions — the rule and record floats at open,
    // the migration drops, the unfloat-by-adoption and the handoffReceive
    // re-float — go out as windowFloatingStateSynced and land on the passive
    // handler below. Routing them all through here treated every one as a user
    // float: a floating window dragged onto a scrolling screen was teleported
    // away from the drop point to its stored free geometry (the discussion #271
    // class), and every window open or stale-key migration raised a spurious
    // floated/tiled OSD. Autotile has always split the two the same way.
    connect(scrollEngine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged, this,
            [this](const QString& windowId, bool floating, const QString& screenId) {
                if (floating && m_windowTrackingAdaptor) {
                    m_windowTrackingAdaptor->applyGeometryForFloat(windowId, screenId);
                }
                if (navigationOsdAllowed(screenId)) {
                    const QString reason = floating ? QStringLiteral("floated") : QStringLiteral("tiled");
                    m_overlayService->showNavigationOsd(true, QStringLiteral("float"), reason, QString(), QString(),
                                                        screenId);
                }
            });
    // Passive arm: engine-initiated float transitions, which must not restore
    // geometry or raise an OSD. It also carries the cross-engine eviction, so a
    // window adopted onto a scrolling screen releases any stale snap or autotile
    // tracking instead of leaving a ghost the sibling engine retiles around.
    connect(scrollEngine, &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced, this,
            &Daemon::syncScrollFloatStatePassive);
    connect(scrollEngine, &PhosphorEngine::PlacementEngineBase::windowsReleased, m_tilingAdaptor,
            [adaptor = m_tilingAdaptor](const QStringList& windowIds, const QSet<QString>&) {
                adaptor->relayWindowsReleased(windowIds);
            });
    // Snap restore on scrolling→snapping flips: the same handler the
    // autotile release path uses (autotile_init.cpp) — without it a screen
    // leaving scrolling kept its column rects and lost its snap float bits.
    connect(scrollEngine, &PhosphorEngine::PlacementEngineBase::windowsReleased, this,
            [this](const QStringList& windowIds, const QSet<QString>& releasedScreenIds) {
                handleEngineWindowsReleased(m_scrollEngine.get(), windowIds, releasedScreenIds);
            });
    connect(scrollEngine, &PhosphorScrollEngine::ScrollEngine::scrollingScreensChanged, m_tilingAdaptor,
            [adaptor = m_tilingAdaptor](const QStringList&, bool isDesktopSwitch) {
                adaptor->notifyEngineScreensChanged(isDesktopSwitch);
            });

    // Tab-strip indicators for tabbed scrolling columns, relayed to the KWin
    // effect. The engine emits the structural model (column rects + window
    // ids) after every strip relayout; the effect paints the pills itself and
    // resolves titles and colours through its own queries, so it takes the
    // engine's payload verbatim.
    connect(scrollEngine, &PhosphorScrollEngine::ScrollEngine::tabStripsChanged, m_tilingAdaptor,
            [adaptor = m_tilingAdaptor](const QString& screenId, const QString& stripsJson) {
                adaptor->relayScrollTabStrips(screenId, stripsJson);
            });

    // A rules save changes what the per-window TabColor* actions resolve to.
    //
    // NO disconnect-first here. The rulesChanged family is swept ONCE, at the
    // top of the block that establishes it (see the sever above the refilter
    // subscription), because a blanket disconnect names the (sender, signal,
    // receiver) triple and cannot single out one subscription. Sweeping again
    // HERE would run after the refilter, overlay-refresh and
    // assignment-reconcile subscriptions were established and would silently
    // sever all three. The stop() → init() duplicate this connect needs
    // protecting from is already handled by that one sweep, since it precedes
    // every rulesChanged connect including this. Unguarded for the same reason
    // as the three rulesChanged connects above: m_ruleStore is ctor-owned and
    // non-null for the daemon's lifetime.
    connect(m_ruleStore.get(), &PhosphorRules::RuleStore::rulesChanged, this, [this]() {
        // The effect caches its own scrollTabColors answers, and a rules save
        // moves every window's verdict at once, so it gets the all-windows
        // broadcast rather than a per-window payload the daemon would have to
        // walk each strip to build.
        if (m_tilingAdaptor) {
            m_tilingAdaptor->relayScrollTabColorsChanged();
        }
    });

    // Control adaptor - high-level convenience API for third-party integrations.
    // Held as a member so stop() can detach() it before the unique_ptr members
    // it borrows are destroyed.
    m_controlAdaptor =
        new ControlAdaptor(m_windowTrackingAdaptor, m_snapAdaptor, m_layoutAdaptor, m_layoutManager.get(),
                           autotileEngine, m_screenManager.get(), m_compositorBridge, this);

    // Handle KCM assignment change resnap/OSD. This runs AFTER the KCM's batch
    // save completes (all setAssignmentEntry + notifyReload finished), so all
    // assignments and settings are fully committed. Separated from settingsChanged
    // handler to avoid feedback loops with autotile/snapping transitions. The
    // handler body lives in init_assignment_apply.cpp.
    //
    // No disconnect-first here: initCoreAdaptors deletes and re-news
    // m_layoutAdaptor in the same preamble that clears the engine adaptors
    // (init_adaptors.cpp), and it always runs immediately before this
    // function, so a stop() -> init() cycle hands us a freshly constructed
    // adaptor that carries no connections to sweep.
    connect(m_layoutAdaptor, &LayoutAdaptor::assignmentChangesApplied, this, &Daemon::handleAssignmentChangesApplied);
}

} // namespace PlasmaZones
