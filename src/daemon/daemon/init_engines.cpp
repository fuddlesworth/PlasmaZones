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
#include "dbus/overlayadaptor.h"
#include "dbus/zonedetectionadaptor.h"
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

    // Autotile provider. setContextGapProvider is derived-only
    // (AutotileEngine); m_autotileEngine is held as the base
    // PlacementEngineBase, so use the derived `autotileEngine` pointer
    // captured above — the std::move into m_autotileEngine transferred
    // ownership but not the pointee, so it still points at the live engine.
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

    // Late-bind the resolver into the D-Bus adaptors that gate their
    // handlers on the disable/lock cascade. Each adaptor was constructed
    // earlier (before m_settings/m_screenModeRouter were ready); the
    // resolver only exists now. The setters mirror setAutotileEngine /
    // setShortcutRegistrar / setScreenModeRouter — same late-binding
    // pattern the daemon already uses for cross-cutting deps.
    if (m_windowDragAdaptor) {
        m_windowDragAdaptor->setContextResolver(m_contextResolver.get());
    }
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->setContextResolver(m_contextResolver.get());
    }
    // m_snapAdaptor is constructed below at the engine-adaptor block; its
    // contextResolver wire lives there.

    connect(autotileEngine, &PhosphorEngine::PlacementEngineBase::settingsPersistRequested, this, [this]() {
        if (m_settings) {
            m_settings->save();
        }
    });
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

    // Filter the unified rule store down to its Exclude-shaped slice and
    // hand the address to SnapEngine for its isAppIdExcluded probe. The
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
    snapEngine->setExcludeRuleSet(&m_excludeRuleSet);
    m_excludeRuleSet.setRules(PhosphorRules::ExclusionRules::excludeRulesFrom(m_ruleStore->ruleSet()).rules());
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
        // (rename, priority change, non-Exclude action edit, …) fires
        // this lambda, but only changes that affect the Exclude slice
        // should bump the evaluator's revision and walk the (potentially
        // long) pending-restore queues. The guard below compares the two
        // `QList<Rule>` slices element-wise (the same semantics as
        // `RuleSet::operator==`, which delegates to this list compare) —
        // exactly the rules-list-only comparison we want.
        const QList<PhosphorRules::Rule> newSlice =
            PhosphorRules::ExclusionRules::excludeRulesFrom(m_ruleStore->ruleSet()).rules();
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
        // Exclude rule. Snap-engine's resolveWindowRestore already refuses
        // them at runtime, but stale queue entries spam logs and bloat the
        // saved state. The autotile-side queues don't exist yet at init
        // — daemon/signals.cpp's finalizeStartup re-runs the prune once
        // AutotileEngine::loadState has populated them.
        if (m_windowTrackingAdaptor) {
            // Shutdown-window guard, mirrors snapEnginePtr null-check above.
            m_windowTrackingAdaptor->pruneExcludedPendingRestores(
                PhosphorRules::ExclusionRules::applicationExcludePatternsFrom(m_excludeRuleSet));
        }
    };
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
    connect(m_ruleStore.get(), &PhosphorRules::RuleStore::rulesChanged, this, [this](bool /*persisted*/) {
        reconcileActiveAssignments();
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
        auto screenModeForWindow =
            [this, autotilePtr = QPointer(autotileEngine),
             scrollTrackPtr = QPointer(scrollEngine)](const QString& windowId) -> PhosphorZones::AssignmentEntry::Mode {
            QString screenId;
            const PhosphorPlacement::WindowTrackingService* wts = nullptr;
            if (m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()) {
                wts = m_windowTrackingAdaptor->service();
                screenId = wts->screenForWindow(windowId);
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
                // only for snap-mode windows. The two engines keep INDEPENDENT
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

    // Clear stale autotile-floated flag when a window is snapped. A window
    // dragged from an autotile VS to a snap VS retains its autotileFloated
    // marker; without this, a subsequent mode change on the autotile VS
    // incorrectly processes the already-snapped window as autotile-managed.
    // Wired here (daemon) because engines must not know about each other.
    connect(snapEngine, &PhosphorSnapEngine::SnapEngine::windowSnapStateChanged, this,
            [this](const QString& windowId, const PhosphorProtocol::WindowStateEntry&) {
                if (m_autotileEngine) {
                    m_autotileEngine->clearModeSpecificFloatMarker(windowId);
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
    connect(scrollEngine, &PhosphorEngine::PlacementEngineBase::placementChanged, m_windowTrackingAdaptor, [this]() {
        if (m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()) {
            m_windowTrackingAdaptor->service()->markDirty(
                PhosphorPlacement::WindowTrackingService::DirtyWindowPlacements);
        }
    });
    scrollEngine->setPersistenceDelegate(
        [wta = QPointer(m_windowTrackingAdaptor)]() {
            if (wta)
                wta->saveState();
        },
        [wta = QPointer(m_windowTrackingAdaptor)]() {
            if (wta)
                wta->loadState();
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
    connect(
        autotileEngine, &PhosphorEngine::PlacementEngineBase::placementChanged, this, [this](const QString& screenId) {
            if (!m_autotileEngine) {
                return;
            }
            // const overload: non-creating, returns nullptr (→ count 0) when
            // the screen has no tiling state, so this gate never allocates a
            // phantom state while observing the count.
            const PhosphorEngine::IPlacementState* state = std::as_const(*m_autotileEngine).stateForScreen(screenId);
            const int count = state ? state->tiledWindowCount() : 0;
            const auto it = m_lastTiledCountByScreen.constFind(screenId);
            if (it != m_lastTiledCountByScreen.constEnd() && it.value() == count) {
                return; // count unchanged — nothing a count rule could key on moved
            }
            m_lastTiledCountByScreen.insert(screenId, count);
            updateEngineScreens();
        });
    // Scrolling twin of the count-change gate above, so a TiledWindowCount
    // rule keys on scrolling screens too (the provider in init_services
    // consults both engines).
    connect(scrollEngine, &PhosphorEngine::PlacementEngineBase::placementChanged, this,
            [this](const QString& screenId) {
                if (!m_scrollEngine) {
                    return;
                }
                const PhosphorEngine::IPlacementState* state = std::as_const(*m_scrollEngine).stateForScreen(screenId);
                const int count = state ? state->tiledWindowCount() : 0;
                const auto it = m_lastTiledCountByScreen.constFind(screenId);
                if (it != m_lastTiledCountByScreen.constEnd() && it.value() == count) {
                    return;
                }
                m_lastTiledCountByScreen.insert(screenId, count);
                updateEngineScreens();
            });

    // Create engine D-Bus adaptors — each engine has a dedicated adaptor that
    // connects signals in its constructor (unified pattern for both engines)
    // Live-mode resolver for snap's capture gate: the router's
    // live-set-first answer lets a presave capture a screen the cascade
    // already flipped to a tiling mode but no engine claims yet. Cleared
    // in stop() before the router is destroyed.
    snapEngine->setLiveModeResolver([this](const QString& screenId) {
        return m_screenModeRouter ? m_screenModeRouter->modeFor(screenId) : PhosphorZones::AssignmentEntry::Snapping;
    });
    m_snapAdaptor = new SnapAdaptor(snapEngine, m_windowTrackingAdaptor, m_settings.get(), this);
    m_snapAdaptor->setContextResolver(m_contextResolver.get());
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
    connect(autotileEngine, &PhosphorTileEngine::AutotileEngine::enabledChanged, m_tilingAdaptor,
            [adaptor = m_tilingAdaptor](bool) {
                adaptor->relayEnabledChanged();
            });
    connect(scrollEngine, &PhosphorScrollEngine::ScrollEngine::windowsTiled, m_tilingAdaptor,
            &TilingAdaptor::relayTileRequestsJson);
    connect(scrollEngine, &PhosphorEngine::PlacementEngineBase::activateWindowRequested, m_tilingAdaptor,
            &TilingAdaptor::focusWindowRequested);
    connect(scrollEngine, &PhosphorEngine::PlacementEngineBase::placementChanged, m_tilingAdaptor,
            &TilingAdaptor::tilingChanged);
    connect(scrollEngine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged, m_tilingAdaptor,
            &TilingAdaptor::relayWindowFloatingChanged);
    // Float parity with autotile's sync path: the scroll engine manages
    // its float STATE itself, but only the daemon can restore the
    // float-back geometry — floatWindowInternal merely pulls the window
    // out of the strip and windowsTiled never carries release entries, so
    // without this the window sits frozen at its old column rect after
    // Meta+F. Same applyGeometryForFloat the autotile sync uses, plus the
    // navigation OSD.
    connect(scrollEngine, &PhosphorEngine::PlacementEngineBase::windowFloatingChanged, this,
            [this](const QString& windowId, bool floating, const QString& screenId) {
                if (floating && m_windowTrackingAdaptor) {
                    m_windowTrackingAdaptor->applyGeometryForFloat(windowId, screenId);
                }
                if (m_settings && m_settings->showNavigationOsd() && m_overlayService) {
                    const QString reason = floating ? QStringLiteral("floated") : QStringLiteral("tiled");
                    m_overlayService->showNavigationOsd(true, QStringLiteral("float"), reason, QString(), QString(),
                                                        screenId);
                }
            });
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

    // Tab-strip indicators for tabbed scrolling columns. The engine emits
    // the structural model (column rects + window ids) after every strip
    // relayout; the daemon enriches the ids with live titles from the
    // window registry and drives the per-screen overlay slot.
    connect(scrollEngine, &PhosphorScrollEngine::ScrollEngine::tabStripsChanged, this,
            [this](const QString& screenId, const QString& stripsJson) {
                if (!m_overlayService) {
                    return;
                }
                const QJsonDocument doc = QJsonDocument::fromJson(stripsJson.toUtf8());
                QVariantList strips;
                const QJsonArray arr = doc.array();
                for (const QJsonValue& stripValue : arr) {
                    const QJsonObject stripObj = stripValue.toObject();
                    QVariantMap strip;
                    strip.insert(QStringLiteral("x"), stripObj.value(QLatin1String("x")).toInt());
                    strip.insert(QStringLiteral("y"), stripObj.value(QLatin1String("y")).toInt());
                    strip.insert(QStringLiteral("width"), stripObj.value(QLatin1String("width")).toInt());
                    const int activeIndex = stripObj.value(QLatin1String("activeIndex")).toInt();
                    QVariantList tabs;
                    const QJsonArray tabIds = stripObj.value(QLatin1String("tabs")).toArray();
                    for (int i = 0; i < tabIds.size(); ++i) {
                        const QString windowId = tabIds.at(i).toString();
                        QString title;
                        if (m_windowRegistry) {
                            const auto meta =
                                m_windowRegistry->metadata(PhosphorIdentity::WindowId::extractInstanceId(windowId));
                            if (meta) {
                                title = meta->title;
                            }
                        }
                        if (title.isEmpty()) {
                            title = PhosphorIdentity::WindowId::extractAppId(windowId);
                        }
                        QVariantMap tab;
                        tab.insert(QStringLiteral("title"), title);
                        tab.insert(QStringLiteral("active"), i == activeIndex);
                        tabs.append(tab);
                    }
                    strip.insert(QStringLiteral("tabs"), tabs);
                    strips.append(strip);
                }
                m_overlayService->updateScrollTabStrips(screenId, strips);
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
    // handler to avoid feedback loops with autotile/snapping transitions.
    connect(
        m_layoutAdaptor, &LayoutAdaptor::assignmentChangesApplied, this,
        [this](const QStringList& changedScreenIdsList) {
            const QSet<QString> changedScreenIds(changedScreenIdsList.begin(), changedScreenIdsList.end());
            if (!m_snapEngine || !m_windowTrackingAdaptor || !m_screenManager || !m_layoutManager)
                return;

            const QString activity = currentActivity();

            // Collect ENGINE-MANAGED screens (autotile AND scrolling — both
            // must be excluded from the snap resnap below; a scrolling
            // screen's "scrolling:" assignment id makes mode flips visible
            // in changedScreenIds now) and per-screen OSD data in one pass.
            QSet<QString> engineManagedScreens;
            struct ScreenOsd
            {
                QString screenId;
                PhosphorZones::AssignmentEntry::Mode mode;
                QString algoId;
            };
            QVector<ScreenOsd> osdEntries;
            const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
            for (const QString& screenId : effectiveIds) {
                // Per-output virtual desktops (#648): each screen resolves its own desktop.
                const int desktop = currentDesktopForScreen(screenId);
                const QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
                const bool isAutotileScreen = PhosphorLayout::LayoutId::isAutotile(assignmentId);
                const bool isScrollingScreen = PhosphorLayout::LayoutId::isScrolling(assignmentId);
                if (isAutotileScreen || isScrollingScreen) {
                    engineManagedScreens.insert(screenId);
                }
                // Only show OSD for screens that actually changed. Three-way:
                // a Scrolling screen must neither be announced with a snap
                // layout nor have its lock queried against the wrong mode.
                if (changedScreenIds.isEmpty() || changedScreenIds.contains(screenId)) {
                    if (isAutotileScreen) {
                        osdEntries.append({screenId, PhosphorZones::AssignmentEntry::Autotile,
                                           PhosphorLayout::LayoutId::extractAlgorithmId(assignmentId)});
                    } else if (isScrollingScreen) {
                        osdEntries.append({screenId, PhosphorZones::AssignmentEntry::Scrolling, {}});
                    } else {
                        osdEntries.append({screenId, PhosphorZones::AssignmentEntry::Snapping, {}});
                    }
                }
            }

            // Resnap only the snapping-mode screens whose assignments actually changed.
            // changedScreenIds scopes the resnap to avoid spurious geometry-set on
            // screens whose layout didn't change (prevents flicker on unrelated VS).
            // Restrict the resnap to each screen's CURRENT virtual desktop (the
            // filter compares every window against its own screen's desktop, so
            // multi-screen KCM applies stay correct). Without it, a per-desktop
            // assignment change resnaps windows parked on OTHER desktops into the
            // just-assigned layout's zones — the user sees one desktop's layout
            // leak onto every desktop. Mirrors resnapIfManualMode (navigation.cpp).
            armResnapOsdSuppression(osdEntries.size());
            m_windowTrackingAdaptor->service()->populateResnapBufferForAllScreens(engineManagedScreens,
                                                                                  changedScreenIds, currentDesktop());
            m_snapAdaptor->resnapToNewLayout();
            // Restore snap-float positions for windows this KCM apply released
            // from autotile — the buffer-based resnap above cannot cover
            // floating windows (see the helper).
            emitPendingSnapFloatRestoresForResnapBuffer();

            // Show OSD for changed screens — use locked OSD variant when context is locked.
            // KCM Apply is an explicit user-driven layout assignment change, so the regular
            // preview OSDs gate on showOsdOnLayoutSwitch (matching cycle / quick-layout /
            // zone-selector-drop). The locked-context OSD bypasses the toggle by design — it
            // explains why a requested change had no visible effect on that screen, the same
            // pattern used for the mode-toggle locked feedback in connectShortcutSignals().
            const bool osdEnabled = m_settings && m_settings->showOsdOnLayoutSwitch();
            for (const auto& osd : std::as_const(osdEntries)) {
                // Suppressed context → no active layout; skip its OSD, mirroring
                // the per-screen desktop-switch OSD gate in showOsdForScreens.
                if (m_layoutManager
                    && m_layoutManager->isContextActiveLayoutSuppressed(
                        osd.screenId, currentDesktopForScreen(osd.screenId), activity)) {
                    continue;
                }
                if (isCurrentContextLockedForMode(osd.screenId, osd.mode)) {
                    showLockedPreviewOsd(osd.screenId);
                } else if (!osdEnabled) {
                    continue;
                } else if (osd.mode == PhosphorZones::AssignmentEntry::Scrolling) {
                    // Scrolling has no layout entity to announce; the mode
                    // switch itself is the OSD content (mirrors
                    // cheatsheetModeString's three-way handling).
                    showScrollingModeOsd(osd.screenId);
                } else if (osd.mode == PhosphorZones::AssignmentEntry::Autotile) {
                    if (!osd.algoId.isEmpty()) {
                        // Resolve the algorithm's human-readable display
                        // name via the registry instead of surfacing the
                        // wire-format id (e.g. "bsp" → "Binary Split").
                        // Mirrors the algorithm display-name resolution in the
                        // per-screen OSD path (showOsdForScreens, osd.cpp).
                        const auto* algo = m_algorithmRegistry ? m_algorithmRegistry->algorithm(osd.algoId) : nullptr;
                        const QString displayName = algo ? algo->name() : osd.algoId;
                        showLayoutOsdForAlgorithm(osd.algoId, displayName, osd.screenId);
                    }
                } else {
                    // Per-output virtual desktops (#648): each screen resolves its own desktop.
                    const int desktop = currentDesktopForScreen(osd.screenId);
                    PhosphorZones::Layout* layout = m_layoutManager->layoutForScreen(osd.screenId, desktop, activity);
                    if (layout)
                        showLayoutOsd(layout, osd.screenId);
                }
            }

            // Refresh the active-assignment snapshot to what was just applied,
            // so a later rule edit diffs against reality (this path also runs
            // for legacy setAssignmentEntry-driven applies, which bypass
            // reconcileActiveAssignments and would otherwise leave it stale).
            diffActiveAssignments();
        });
}

bool Daemon::registerDBusService()
{
    // Register D-Bus service and object with error handling and retry logic
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCCritical(lcDaemon) << "Session D-Bus: cannot connect, daemon cannot function";
        return false;
    }

    // Retry D-Bus service registration with exponential backoff.
    // Synchronous retry is required here because init() runs before QGuiApplication::exec(),
    // so QTimer-based async approaches won't fire. Delays are kept short (300ms total max).
    constexpr int maxRetries = 3;
    constexpr int baseDelayMs = 100; // backoff sleeps 100ms then 200ms
    // Worst-case blocking: 100 + 200 = 300 ms on the GUI thread. The third
    // attempt does not sleep — the `attempt < maxRetries - 1` gate below skips
    // the final (would-be 400ms) delay and returns instead.
    // init() runs before QGuiApplication::exec(), so QTimer-based async
    // approaches don't fire — synchronous sleep is the only retry path
    // available here. The retry is bounded by `maxRetries`, and a bus
    // disconnect during the wait would render every subsequent retry
    // pointless (lastError type stays ServiceUnknown but the actual
    // problem is connection-level).
    bool serviceRegistered = false;
    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        if (!bus.isConnected()) {
            qCCritical(lcDaemon) << "D-Bus bus connection lost mid-retry — aborting service registration";
            return false;
        }
        if (bus.registerService(QString(PhosphorProtocol::Service::Name))) {
            serviceRegistered = true;
            break;
        }

        QDBusError error = bus.lastError();
        if (error.type() == QDBusError::ServiceUnknown || error.type() == QDBusError::NoReply) {
            // Transient error - retry with exponential backoff
            if (attempt < maxRetries - 1) {
                const int delayMs = baseDelayMs * (1 << attempt);
                qCWarning(lcDaemon) << "D-Bus service registration: failed (attempt" << (attempt + 1) << "/"
                                    << maxRetries << ")," << error.message() << "retrying in" << delayMs << "ms";
                QThread::msleep(delayMs);
                continue;
            }
        }

        // Non-retryable error or max retries reached
        qCCritical(lcDaemon) << "Failed to register D-Bus service=" << PhosphorProtocol::Service::Name
                             << "error=" << error.message() << "type=" << error.type();
        return false;
    }

    if (!serviceRegistered) {
        qCCritical(lcDaemon) << "Failed to register D-Bus service after" << maxRetries << "attempts";
        return false;
    }

    // Register D-Bus object (no retry needed - service is already registered)
    if (!bus.registerObject(QString(PhosphorProtocol::Service::ObjectPath), this)) {
        QDBusError error = bus.lastError();
        qCCritical(lcDaemon) << "Failed to register D-Bus object=" << PhosphorProtocol::Service::ObjectPath
                             << "error=" << error.message();
        // Cleanup: unregister service if object registration fails
        bus.unregisterService(QString(PhosphorProtocol::Service::Name));
        return false;
    }

    qCInfo(lcDaemon) << "D-Bus service registered service=" << PhosphorProtocol::Service::Name
                     << "path=" << PhosphorProtocol::Service::ObjectPath;

    // Connect overlay adaptor signals to daemon overlay control
    connect(m_overlayAdaptor, &OverlayAdaptor::overlayVisibilityChanged, this, [this](bool visible) {
        if (visible) {
            showOverlay();
        } else {
            hideOverlay();
        }
    });

    // Connect zone detection to overlay updates
    connect(m_zoneDetectionAdaptor, &ZoneDetectionAdaptor::zoneDetected, this,
            [this](const QString& zoneId, const PhosphorProtocol::ZoneGeometryRect& geometry) {
                Q_UNUSED(zoneId)
                Q_UNUSED(geometry)
                // Update overlay when zone is detected
                m_overlayService->updateGeometries();
            });

    return true;
}

} // namespace PlasmaZones
