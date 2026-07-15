// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingscontroller.h"

#include "editorpagecontroller.h"
#include "generalpagecontroller.h"
#include "registryshaderpreviewbackend.h"
#include "snappingzonescontroller.h"
#include "../shaderpreview/shaderpreviewcontroller.h"
#include "snappingbehaviorcontroller.h"
#include "snappingeffectscontroller.h"
#include "snappingzoneselectorcontroller.h"
#include "tilingalgorithmcontroller.h"
#include "windowappearancecontroller.h"
#include "tilingbehaviorcontroller.h"
#include "virtualscreenutils.h"
#include "../config/configbackends.h"
#include "../config/configdefaults.h"
#include "../config/configmigration.h"
#include "../common/layoutpreviewserialize.h"
#include "../common/screenidresolver.h"
#include "../common/layoutbundlebuilder.h"
#include "../core/constants.h"
#include "../core/geometryutils.h"
#include <PhosphorZones/LayoutComputeService.h>
#include "../core/logging.h"
#include "../core/utils.h"
#include "../phosphor_i18n.h"
#include "dbusutils.h"
#include "pageadapter.h"
#include "settingsstagingdomain.h"
#include "version.h"

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorProtocol/ClientHelpers.h>
// std::make_unique<RuleStore> in the ctor needs the complete
// type. The header forward-declares it to avoid pulling the
// dependency graph into every consumer of SettingsController.
#include <PhosphorRules/RuleStore.h>
#include <PhosphorRules/RuleStoreWatcher.h>

#include "../core/shaderregistry.h"
#include "snappingshaderspagecontroller.h"

#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorFsLoader/SchemaValidator.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>
#include <PhosphorLayoutApi/LayoutPreview.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/ZonesLayoutSource.h>

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>
#include <QWindow>

#include <algorithm>
#include <memory>

namespace PlasmaZones {

namespace {

// Materialise + register the XDG search dirs for a settings-side shader-pack
// registry (animation / surface): system dirs reversed to lowest-priority-first
// (the loaders apply first-registration-wins, yielding user > sys-high > ... >
// sys-low once the user dir is appended last), user dir created up-front so
// the watcher attaches a direct watch instead of a parent-watch proxy.
// @p subdir is the ConfigDefaults::user*Subdir() constant (leading '/'):
// stripped for locateAll's relative-path arg, kept for the writable-base join.
template<typename Registry>
void registerXdgPackDirs(Registry* registry, const QString& subdir)
{
    QStringList dirs =
        QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, subdir.mid(1), QStandardPaths::LocateDirectory);
    std::reverse(dirs.begin(), dirs.end());
    const QString userDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + subdir;
    if (!dirs.contains(userDir))
        dirs.append(userDir);
    QDir().mkpath(userDir);
    registry->setUserPath(userDir);
    registry->addSearchPaths(dirs);
}

} // namespace

// Member-function definition for the static sort helper declared in the
// header. Both settingscontroller.cpp and settingscontroller_layouts.cpp
// call it via the qualified `SettingsController::sortMergedLayoutList(...)`
// name, so the build is unity-batch-independent.
void SettingsController::sortMergedLayoutList(QVariantList& list)
{
    std::sort(list.begin(), list.end(), [](const QVariant& a, const QVariant& b) {
        const QVariantMap mapA = a.toMap();
        const QVariantMap mapB = b.toMap();
        const bool aIsAutotile = mapA.value(QStringLiteral("isAutotile")).toBool();
        const bool bIsAutotile = mapB.value(QStringLiteral("isAutotile")).toBool();
        if (aIsAutotile != bIsAutotile)
            return !aIsAutotile;
        return mapA.value(QStringLiteral("displayName")).toString().toLower()
            < mapB.value(QStringLiteral("displayName")).toString().toLower();
    });
}

SettingsController::~SettingsController()
{
    // Tear down the RuleController's label lookups while the
    // captured member containers (m_layouts, m_activities, m_screens,
    // etc.) are still alive. Members destruct in reverse declaration
    // order BEFORE ~QObject tears down child QObjects — so by the time
    // ~RuleController runs as part of the QObject teardown, those
    // captured containers are already gone. Any model-signal slot that
    // reaches a lookup during teardown would deref destroyed state.
    // RuleModel::leafLabel/actionLabel treat empty lookups as
    // identity, so clearing here is the safe contract.
    if (m_rulesPage) {
        m_rulesPage->setScreenLookup({});
        m_rulesPage->setActivityLookup({});
        m_rulesPage->setZoneLookup({});
        m_rulesPage->setVirtualDesktopLookup({});
        m_rulesPage->setSnappingLayoutLookup({});
        m_rulesPage->setTilingAlgorithmLookup({});
        // The shader resolver captures `this` and reaches m_animationShaderRegistry;
        // clear it too so the cleared set stays symmetric with what's installed.
        m_rulesPage->setShaderEffectLookup({});
        // The overlay-shader resolver reaches m_overlayShaderRegistry — clear it
        // too for the same symmetry.
        m_rulesPage->setOverlayShaderLookup({});
        // The decoration-pack resolver captures `this` and dereferences
        // m_surfaceShaderRegistry (a QObject child of this, destroyed during
        // ~QObject teardown); clear it too so a deferred dataChanged during
        // teardown can't run the lookup against the dangling registry.
        m_rulesPage->setDecorationPackLookup({});
        // Drain any in-flight `dataChanged` emissions queued against
        // the cleared lookups before the model captures the now-
        // empty resolvers. refreshLabels walks every row once and
        // rebuilds the label cache with the identity (empty) lookups
        // so the next paint reads consistent data.
        if (m_rulesPage->model())
            m_rulesPage->model()->refreshLabels();
    }
}

SettingsController::SettingsController(QObject* parent)
    : QObject(parent)
    // m_localRuleStore is constructed first (declared before m_settings) so the
    // single shared store exists before m_settings borrows it. Parent stays
    // null on m_settings — it is a value member, not a QObject child of `this`.
    , m_localRuleStore(std::make_unique<PhosphorRules::RuleStore>(ConfigDefaults::rulesFilePath()))
    , m_localRuleStoreWatcher(std::make_unique<PhosphorRules::RuleStoreWatcher>(*m_localRuleStore))
    // Comma-expression: install the library-level screen-id resolver, then store
    // `true`. Runs BEFORE m_settings (next) whose constructor load()s and
    // migrates per-screen override keys via idForName — so the very first load
    // canonicalises connector names to EDID instead of silently no-op'ing.
    // First call initialises the static; later constructions reuse it.
    , m_screenIdResolverReady((ensureScreenIdResolver(), true))
    , m_settings(m_localRuleStore.get(), nullptr)
    , m_screenHelper(&m_settings, this)
    , m_localAlgorithmRegistry(std::make_unique<PhosphorTiles::AlgorithmRegistry>(nullptr))
    , m_localLayoutManager(
          std::make_unique<PhosphorZones::LayoutRegistry>(m_localRuleStore.get(), ConfigDefaults::layoutsSubdir()))
{
    // The screen-id resolver is installed in the member-init list (see
    // m_screenIdResolverReady) so it is ready before m_settings load()s. It
    // lives in src/common/screenidresolver.{h,cpp} so daemon/editor/settings
    // share one install-once helper.

    // Auto-discovery pattern: every linked provider library has
    // already registered a builder via static-init. The KCM just
    // publishes the registries it owns via the shared helper
    // (buildStandardLayoutSourceBundle) so the context-wiring is the
    // same across daemon/editor/settings. Adding a new engine library
    // doesn't require editing this file unless the engine demands a
    // service the KCM doesn't already publish.
    buildStandardLayoutSourceBundle(m_localSources, m_localLayoutManager.get(), m_localAlgorithmRegistry.get());

    // Begin watching rules.json for external writes. Complements the
    // daemon's rulesChanged D-Bus signal (reloadLocalRuleStore) so the
    // in-process LayoutRegistry's assignment cascade stays fresh even with no
    // daemon running; the store's idempotent load() makes the overlap a no-op.
    m_localRuleStoreWatcher->start();

    // Wire the layoutsChanged handler BEFORE the initial loadLayouts() so
    // any QFileSystemWatcher event landing in the window between load +
    // connect (e.g. the daemon writing to ~/.local/share/plasmazones/layouts/
    // mid-ctor) is handled. ZonesLayoutSource self-wires to the registry's
    // unified ILayoutSourceRegistry::contentsChanged; future reads through
    // the local source reflect current geometry once recalcLocalLayouts has
    // run. When the file watcher detects a layout change on disk, refresh
    // m_layouts from the local composite (manual + autotile) so QML rebinds
    // even if the daemon isn't broadcasting D-Bus signals (or is down). The
    // async refresh through scheduleLayoutLoad() / loadLayoutsAsync() also
    // fires from daemon-side D-Bus signals and replaces m_layouts with the
    // D-Bus-enriched view when the daemon is up — these two paths converge
    // at m_layouts.
    connect(m_localLayoutManager.get(), &PhosphorZones::LayoutRegistry::layoutsChanged, this, [this]() {
        recalcLocalLayouts();
        QVariantList localLayouts = localLayoutPreviews();
        // Assign unconditionally: when the user deletes every layout we
        // want m_layouts to reflect the empty state. Previously this
        // path guarded on `!localLayouts.isEmpty()`, which left stale
        // entries in m_layouts after a wipe when the daemon was down.
        SettingsController::sortMergedLayoutList(localLayouts);
        // Skip when the disk view matches what we already have — file-
        // watcher events fire on every daemon write during a save
        // batch, and identical payloads would re-emit the model on
        // each tick.
        const bool actuallyChanged = m_layouts != localLayouts;
        if (actuallyChanged) {
            m_layouts = std::move(localLayouts);
        }
        // Suppress the local-path emit while a D-Bus getLayoutList
        // call is in flight — the async reply lambda will emit once
        // it replaces m_layouts with the daemon-enriched view. If the
        // daemon is unreachable or the call errors, the gate is
        // cleared in the reply lambda's head and subsequent local
        // emits run normally (fallback behaviour).
        if (actuallyChanged && !m_awaitingDaemonLayouts) {
            Q_EMIT layoutsChanged();
        }
    });

    // Load the user's layouts immediately so localLayoutPreviews() returns
    // a populated list on first call (before any QML query has had a
    // chance to trigger the legacy D-Bus loadLayoutsAsync path). The
    // PhosphorZones::LayoutRegistry scans ~/.local/share/plasmazones/layouts/ on demand
    // and installs a QFileSystemWatcher so any subsequent disk changes
    // (daemon writes, editor saves) auto-reload without a D-Bus round-trip.
    m_localLayoutManager->loadLayouts();
    // `loadLayouts()` emits `layoutsChanged` synchronously, which the
    // handler wired above already routes through `recalcLocalLayouts()`
    // — so manual layouts with fixed-geometry zones have a non-empty
    // `lastRecalcGeometry()` and ZonesLayoutSource populates
    // LayoutPreview::zones / referenceAspectRatio without needing a
    // second explicit call here. Daemon runs the same recalc in
    // Daemon::init(); settings owns an in-process LayoutRegistry
    // independent of the daemon and gets there via the signal path.

    // Load scripted algorithms so they appear in the algorithm dropdown.
    // The daemon owns its own AlgorithmRegistry + loader; the KCM runs in
    // a separate process and binds to its own per-process registry
    // (m_localAlgorithmRegistry) so picker-side previews stay live even
    // when the daemon is down.
    //
    // Owned by unique_ptr (not parented to `this`) so reverse member-
    // destruction tears the loader down BEFORE m_localAlgorithmRegistry.
    // Parenting to `this` would defer destruction to ~QObject, which runs
    // AFTER the registry's unique_ptr has already been reset — a UAF in
    // ~ScriptedAlgorithmLoader's unregisterAlgorithm loop.
    m_scriptLoader = std::make_unique<PhosphorTiles::ScriptedAlgorithmLoader>(QString(ScriptedAlgorithmSubdir),
                                                                              m_localAlgorithmRegistry.get());

    // Algorithm-registry / scripted-loader surface. Owned via unique_ptr so
    // reverse-order destruction runs the service's dtor (which disconnects
    // its watchers) BEFORE m_scriptLoader and m_localAlgorithmRegistry
    // reset — the service holds raw-pointer borrows of both.
    //
    // Construct the service and wire all algorithmsChanged consumers
    // BEFORE scanAndRegister so the canonical
    //   make_unique → connect → scanAndRegister
    // ordering holds — the empty→populated `algorithmsChanged` emit
    // from the initial scan must reach every subscriber, otherwise a
    // future consumer that relies on the transition (rather than
    // querying availableAlgorithms() directly) silently misses initial
    // population.
    m_algorithmService = std::make_unique<AlgorithmService>(m_settings, *m_localAlgorithmRegistry, *m_scriptLoader);
    connect(m_algorithmService.get(), &AlgorithmService::algorithmCreated, this, &SettingsController::algorithmCreated);
    connect(m_algorithmService.get(), &AlgorithmService::algorithmOperationFailed, this,
            &SettingsController::algorithmOperationFailed);
    connect(m_algorithmService.get(), &AlgorithmService::availableAlgorithmsChanged, this,
            &SettingsController::availableAlgorithmsChanged);

    // Also refresh the layout list when scripted algorithms change (hot-reload):
    // LayoutComboBox's model includes autotile entries from the registry.
    connect(m_scriptLoader.get(), &PhosphorTiles::ScriptedAlgorithmLoader::algorithmsChanged, this,
            &SettingsController::layoutsChanged);

    m_scriptLoader->scanAndRegister();

    // All D-Bus broadcast subscriptions (settings reload, layout
    // mutations, virtual desktop / activity changes, rules mirror)
    // are wired in settingscontroller_dbuswire.cpp so this TU stays under
    // the project's 800-line cap. Any subscription that returns false at
    // construction is appended to @c failedSubscriptions so the post-ctor
    // summary below can surface them in one batched warning.
    QStringList failedSubscriptions;
    wireDaemonSubscriptions(failedSubscriptions);

    // Client-side timeout for the async window picker. If the daemon
    // never fans out a runningWindowsAvailable signal (KWin effect
    // unloaded, crashed, or slow), the timer fires runningWindowsTimedOut()
    // so the UI can switch from a spinner to an error state.
    m_runningWindowsTimeout.setSingleShot(true);
    m_runningWindowsTimeout.setInterval(RunningWindowsTimeoutMs);
    connect(&m_runningWindowsTimeout, &QTimer::timeout, this, [this]() {
        qCWarning(PlasmaZones::lcCore) << "requestRunningWindows: no reply within" << RunningWindowsTimeoutMs
                                       << "ms — KWin effect unresponsive?";
        Q_EMIT runningWindowsTimedOut();
    });

    // Forward daemon running state changes and refresh data when daemon starts
    connect(&m_daemonController, &DaemonController::runningChanged, this, [this]() {
        Q_EMIT daemonRunningChanged();
        if (m_daemonController.isRunning()) {
            // Daemon just came online — reload all D-Bus-dependent data
            scheduleLayoutLoad();
            refreshVirtualDesktops();
            refreshActivities();
            m_screenHelper.refreshScreens();
        }
    });

    // Mark needsSave when any Settings property changes (from QML edits).
    // Use a meta-object connection to catch all NOTIFY signals.
    //
    // Replaced the previous string-based `indexOfSlot("onSettings…")` lookup
    // with `QMetaMethod::fromSignal` over a private forwarder signal:
    // routing through a signal-to-signal hop keeps the connect(QMetaMethod,
    // QMetaMethod) overload (the only path that lets us connect a property
    // notifySignal — discovered reflectively — to a sink) while making the
    // sink side PMF-typed at compile time. A future rename of
    // `onSettingsPropertyChanged` or `_settingsPropertyNotifyForwarder`
    // now fails compilation instead of returning -1 from indexOfSlot at
    // runtime and silently no-op'ing the entire dirty-tracking loop.
    static const QMetaMethod settingsChangedSink =
        QMetaMethod::fromSignal(&SettingsController::_settingsPropertyNotifyForwarder);
    connect(this, &SettingsController::_settingsPropertyNotifyForwarder, this,
            &SettingsController::onSettingsPropertyChanged);
    const QMetaObject* mo = m_settings.metaObject();
    // Walk from 0 (not propertyOffset()) so Q_PROPERTYs declared on the
    // ISettings base or any future intermediate class are also wired —
    // hasNotifySignal() filters out properties without NOTIFY.
    for (int i = 0; i < mo->propertyCount(); ++i) {
        QMetaProperty prop = mo->property(i);
        if (prop.hasNotifySignal()) {
            connect(&m_settings, prop.notifySignal(), this, settingsChangedSink);
        }
    }

    // Per-screen override maps have no Q_PROPERTY, so the meta-object loop
    // above never wires their change signals. Connect them explicitly. The
    // Settings layer emits these ONLY when an override actually changes — a
    // no-op write (same value) or a rejected key early-returns without
    // emitting — so routing them through onSettingsPropertyChanged() gives
    // correct change-only dirty tracking (and load() populates the maps
    // directly, never via the setters, so this stays quiet during load).
    // Re-emitting perScreenOverridesChanged() refreshes the scope-chip
    // override dots and the bound per-screen card values. The Q_INVOKABLE
    // wrappers in settingscontroller_perscreen.cpp therefore do NOT mark
    // dirty or emit themselves — this change signal is the single source of
    // truth, which is also why clicking a value already set no longer flips
    // the page to "unsaved changes".
    const auto wirePerScreenOverrideSignal = [this](void (Settings::*signal)()) {
        connect(&m_settings, signal, this, &SettingsController::onSettingsPropertyChanged);
        connect(&m_settings, signal, this, &SettingsController::perScreenOverridesChanged);
    };
    wirePerScreenOverrideSignal(&Settings::perScreenAutotileSettingsChanged);
    wirePerScreenOverrideSignal(&Settings::perScreenSnappingSettingsChanged);
    wirePerScreenOverrideSignal(&Settings::perScreenZoneSelectorSettingsChanged);

    // Editor + fill-on-drop settings lack Q_PROPERTY on Settings, so the
    // meta-object loop above misses them. EditorPageController forwards each
    // NOTIFY to QML and emits changed() which drives dirty tracking here.
    m_editorPage = new EditorPageController(m_settings, this);
    connect(m_editorPage, &EditorPageController::changed, this, &SettingsController::onSettingsPropertyChanged);

    // Snapping→Behavior + Tiling→Behavior page sub-controllers. Their
    // underlying settings ARE Q_PROPERTY on Settings, so the meta-object
    // loop above already wires them to onSettingsPropertyChanged(); the
    // sub-controllers only provide the QML-facing forwarders + storage/QML
    // trigger-list conversion.
    m_snappingBehaviorPage = new SnappingBehaviorController(m_settings, this);
    m_tilingBehaviorPage = new TilingBehaviorController(m_settings, this);

    // Snapping→Zone Selector page sub-controller. Pure CONSTANT bounds
    // facade over ConfigDefaults — no Settings wiring required.
    m_snappingZoneSelectorPage = new SnappingZoneSelectorController(this);

    // Snapping→Zones page sub-controller (the drag-time zone overlay). Owns
    // border bounds plus the color-import action surface; its changed() signal
    // drives dirty tracking on successful imports.
    m_snappingZonesPage = new SnappingZonesController(m_settings, this);
    connect(m_snappingZonesPage, &SnappingZonesController::changed, this,
            &SettingsController::onSettingsPropertyChanged);

    // Snapping→Effects page — CONSTANT-only bounds facade. The Window Appearance
    // page is ISettings-backed: it forwards its window border / title bar and the
    // shared inner/outer gap values straight to config (Windows.* / Gaps.*). Those
    // values ARE Q_PROPERTY on Settings, so the meta-object loop above already
    // wires their NOTIFY to onSettingsPropertyChanged(); the controller only
    // provides the QML-facing forwarders + the slider bounds.
    m_snappingEffectsPage = new SnappingEffectsController(this);
    m_windowAppearancePage = new WindowAppearanceController(m_settings, this);

    // Tiling→Algorithm page sub-controller. Owns the algorithm slider bounds +
    // the custom-parameter CRUD surface. Borrows the algorithm registry this
    // controller already owns; declared as a unique_ptr AFTER
    // m_localAlgorithmRegistry so reverse-order member destruction tears the
    // sub-controller down BEFORE the registry resets.
    //
    // Parented to `this` so ApplicationController::registerPage does NOT adopt
    // it: registerPage reparents parent-LESS pages to m_app, and m_app —
    // declared last, destroyed first — would then delete this page, leaving the
    // unique_ptr to double-free it (SIGSEGV on close). The parent is purely an
    // ownership marker; the unique_ptr still resets in member order (before the
    // borrowed registry, so raw m_registry never dangles), and ~QObject(this)
    // finds nothing left to delete.
    m_tilingAlgorithmPage = std::make_unique<TilingAlgorithmController>(m_settings, *m_localAlgorithmRegistry, this);
    connect(m_tilingAlgorithmPage.get(), &TilingAlgorithmController::changed, this,
            &SettingsController::onSettingsPropertyChanged);

    // General page sub-controller — owns rendering-backend picker data and
    // animation bounds. Its startup backend snapshot is captured at ctor
    // time, so this must run AFTER m_settings is fully initialised (which
    // is guaranteed since m_settings is the first member declared).
    m_generalPage = new GeneralPageController(m_settings, this);

    // Animation shader registry — settings-side mirror of the daemon's.
    // Both processes scan the same XDG dirs independently; FS watching
    // keeps each in sync without IPC. Mirrors daemon.cpp:setupAnimation
    // ShaderEffects (XDG search paths + user dir, materialised before
    // registration so the watcher attaches a direct watch).
    m_animationShaderRegistry = new PhosphorAnimationShaders::AnimationShaderRegistry(this);
    // The centralised subdir constant matches
    // AnimationsPageController::userShaderDirectoryPath so the two
    // settings-side consumers can never drift apart.
    registerXdgPackDirs(m_animationShaderRegistry, ConfigDefaults::userAnimationsSubdir());

    // Animations page sub-controller — Q_PROPERTY surface for the new
    // animation-event drilldown. Per-event motion overrides persist as
    // JSON files under `~/.local/share/plasmazones/profiles/`, picked up
    // by the daemon's existing `PhosphorAnimation::ProfileLoader` watch;
    // shader assignments persist via Settings::shaderProfileTree.
    m_animationsPage = new AnimationsPageController(m_animationShaderRegistry, &m_settings, this);
    // Mark dirty whenever the user has unsaved animation changes the
    // Discard button could revert. We don't auto-clear when pending
    // becomes false (commit/revert do that explicitly) — that would
    // race with `setNeedsSave(false)` in load()/save().
    connect(m_animationsPage, &AnimationsPageController::pendingChangesChanged, this, [this]() {
        if (m_loading || m_saving || !m_animationsPage->hasPendingChanges())
            return;
        // An animations pending-change flip can fire from a daemon-side
        // profile-file watcher or registry repopulation, not just from the
        // user viewing the animations page — target the page explicitly
        // so dirty state attaches to the right tab even when the user is
        // currently viewing a different page. Symmetric with the
        // rules handler below.
        //
        // Prefer the user's current animations sub-page when they're
        // already in the animations branch (animations-windows /
        // animations-overlays / animations-side-panels / animations-
        // shaders) so the dirty marker lands where the edit actually
        // happened, not always on the General sub-page. The daemon-
        // file-watcher case (user off the animations tree) still
        // falls back to "animations-general" as a stable parent.
        const QString animationsTarget =
            m_activePage.startsWith(QLatin1String("animations-")) ? m_activePage : QStringLiteral("animations-general");
        beginExternalEdit(animationsTarget);
        setNeedsSave(true);
        endExternalEdit();
    });

    // Surface shader registry — settings-side mirror of the daemon's /
    // compositor's. Scans the same XDG `plasmazones/surface` dirs
    // independently; FS watching keeps each in sync without IPC. Mirrors
    // the animation-shader registry block above (XDG search paths + user
    // dir, materialised before registration so the watcher attaches a
    // direct watch).
    m_surfaceShaderRegistry = new PhosphorSurfaceShaders::SurfaceShaderRegistry(this);
    registerXdgPackDirs(m_surfaceShaderRegistry, ConfigDefaults::userSurfaceSubdir());

    // Decoration drill-down sub-controller. PER-SURFACE scope: edits a
    // DecorationProfileTree (per-surface chains of decoration packs) with a
    // baseline global default + walk-up inheritance. The tree persists via the
    // Settings decorationProfileTreeJson Q_PROPERTY, whose NOTIFY
    // (decorationProfileTreeChanged) the meta-object loop above already routes
    // into onSettingsPropertyChanged for dirty tracking — so this controller
    // needs no per-page staging (isDirty/apply/discard are no-ops). It is
    // registered with the framework as a headless domain (the drill-down nav
    // nodes are virtual PageAdapters) in buildApplicationController.
    m_decorationPage = new DecorationPageController(m_surfaceShaderRegistry, &m_settings, this);

    // Rules page sub-controller — the unified rule surface. It owns
    // its own RuleModel and talks to the daemon's
    // org.plasmazones.Rules adaptor. Dirty-tracking mirrors the
    // animations page: a staged edit flips needsSave; commit/revert run
    // from this controller's save()/load() so they don't race the
    // setNeedsSave(false) those methods emit.
    m_rulesPage = new RuleController(this);
    // Attribute rule-model dirtiness to the Rules page. Window appearance and
    // per-monitor gaps are config-backed now (refreshed via
    // perScreenAutotileSettingsChanged, wired above), so the Rules page is the
    // only page riding this shared controller's dirty state.
    // reconcileRuleBackedDirty() is value-based — it compares the staged model
    // to the last daemon-synced snapshot — so it stays correct even when the
    // shared dirty bit does not transition (e.g. editing a baseline while user
    // rules are already dirty). It is driven off the model's per-EDIT signals
    // (dataChanged / rows{Inserted,Removed,Moved}), plus revert/apply/load
    // completion.
    //
    // NOTE: modelReset is deliberately NOT wired here. The only source of a
    // model reset is RuleController::fetchAndLoad's setRules(), whose
    // beginResetModel/endResetModel fires modelReset SYNCHRONOUSLY *before* the
    // controller re-baselines its saved snapshot (captureSavedSnapshot runs on
    // the next line). Reconciling on modelReset would therefore compare the
    // fresh model against the STALE snapshot and spuriously mark both rule-backed
    // pages dirty on every startup / daemon rulesChanged broadcast. That
    // repopulation is instead handled by the rulesLoaded / revertFinished
    // connections below, which fire AFTER the re-baseline.
    const auto reattributeRuleDirty = [this]() {
        if (m_loading || m_saving)
            return;
        reconcileRuleBackedDirty();
    };
    connect(m_rulesPage, &RuleController::dirtyChanged, this, reattributeRuleDirty);
    // rulesLoaded fires after fetchAndLoad re-baselines the snapshot (both the
    // initial/broadcast reload and the revert path), so reconciling here sees the
    // fresh snapshot and clears any dirty the daemon set didn't actually change.
    // revertFinished / applyResult land after load()/save() have run their
    // setNeedsSave(false) blanket-clear; reconcile unconditionally so a failed
    // revert/push (model still divergent from the snapshot) re-marks the right
    // page, and a successful one leaves both clean.
    connect(m_rulesPage, &RuleController::rulesLoaded, this, [this]() {
        reconcileRuleBackedDirty();
    });
    connect(m_rulesPage, &RuleController::revertFinished, this, [this](bool) {
        reconcileRuleBackedDirty();
    });
    connect(m_rulesPage, &RuleController::applyResult, this, [this](bool, const QString&) {
        reconcileRuleBackedDirty();
    });
    if (m_rulesPage->model() != nullptr) {
        RuleModel* ruleModel = m_rulesPage->model();
        connect(ruleModel, &QAbstractItemModel::dataChanged, this, reattributeRuleDirty);
        connect(ruleModel, &QAbstractItemModel::rowsInserted, this, reattributeRuleDirty);
        connect(ruleModel, &QAbstractItemModel::rowsRemoved, this, reattributeRuleDirty);
        connect(ruleModel, &QAbstractItemModel::rowsMoved, this, reattributeRuleDirty);
    }

    // Wire screen / activity / layout label resolvers so the rule model and
    // monitor-overview render friendly names instead of raw connector strings,
    // activity UUIDs and layout UUIDs.
    //
    // The closures capture `this` and read live snapshot state on every call,
    // so they need to be installed exactly ONCE — re-installing on every
    // upstream change was wasteful (three model-wide `dataChanged` emits per
    // signal × three signals = nine emits). Upstream changes are now routed
    // to `RuleModel::refreshLabels()` which emits a single dataChanged
    // covering every label-derived role.
    m_rulesPage->setScreenLookup([this](const QString& screenId) -> QString {
        const QVariantList all = screens();
        for (const QVariant& sv : all) {
            const QVariantMap m = sv.toMap();
            // Match against `name` (the connector / virtual-screen id) or
            // `screenId` (the daemon-stable screen identifier). The screen
            // payload built by `screenInfoListToVariantList` never emits an
            // `id` key — comparing against `"id"` would be dead code.
            if (m.value(QStringLiteral("name")).toString() == screenId
                || m.value(QStringLiteral("screenId")).toString() == screenId) {
                const QString label = m.value(QStringLiteral("displayLabel")).toString();
                return label.isEmpty() ? screenId : label;
            }
        }
        return screenId;
    });
    m_rulesPage->setActivityLookup([this](const QString& activityId) -> QString {
        for (const QVariant& av : std::as_const(m_activities)) {
            const QVariantMap m = av.toMap();
            if (m.value(QStringLiteral("id")).toString() == activityId) {
                const QString name = m.value(QStringLiteral("name")).toString();
                return name.isEmpty() ? activityId : name;
            }
        }
        return activityId;
    });
    m_rulesPage->setVirtualDesktopLookup([this](const QString& desktopNumber) -> QString {
        // Desktop numbers are 1-based; the names list is 0-indexed. Return the name
        // for a valid in-range number; an out-of-range / unnamed / unparseable value
        // returns empty so the summary falls back to the bare number.
        bool ok = false;
        const int num = desktopNumber.toInt(&ok);
        if (ok && num >= 1 && num <= m_virtualDesktopNames.size()) {
            return m_virtualDesktopNames.at(num - 1);
        }
        return QString();
    });
    // Zone (snap-zone UUID) → friendly "<layout> — <zone>" label, walking the
    // local manual layouts for the zone whose id matches. Resolved live so a
    // later layout/zone rename surfaces on the next refreshLabels(). The zone-name
    // data is not in the LayoutPreview list (it carries geometry + numbers, not
    // UUIDs), so this reads the registry's actual Zone objects directly. Unknown
    // ids (deleted layout, hand-edited rule) round-trip verbatim.
    m_rulesPage->setZoneLookup([this](const QString& zoneId) -> QString {
        if (zoneId.isEmpty() || !m_localLayoutManager) {
            return zoneId;
        }
        for (PhosphorZones::Layout* layout : m_localLayoutManager->layouts()) {
            if (!layout) {
                continue;
            }
            for (PhosphorZones::Zone* zone : layout->zones()) {
                if (!zone || zone->id().toString() != zoneId) {
                    continue;
                }
                const QString zoneName =
                    zone->name().isEmpty() ? PhosphorI18n::tr("Zone %1").arg(zone->zoneNumber()) : zone->name();
                const QString layoutName = layout->name();
                return layoutName.isEmpty() ? zoneName : PhosphorI18n::tr("%1 — %2").arg(layoutName, zoneName);
            }
        }
        return zoneId;
    });
    // SettingsController::layouts() is the union of snapping layouts
    // (UUID-keyed) and autotile entries (algorithm-token-keyed via the
    // "autotile:<token>" or bare-token shape PhosphorTiles ships) — one
    // resolver lambda is sufficient. The typed setters below are about
    // CONTRACT clarity at the RuleController API surface so a
    // future caller can wire a more restrictive snapping-only lookup
    // without also constraining the tiling resolver.
    auto resolveByLayoutsLookup = [this](const QString& tokenOrId) -> QString {
        for (const QVariant& lv : std::as_const(m_layouts)) {
            const QVariantMap m = lv.toMap();
            if (m.value(QStringLiteral("id")).toString() == tokenOrId) {
                // Layouts are serialised via `toVariantMap(LayoutPreview)`
                // which stamps the friendly label under `displayName`, not
                // `name`. Reading `name` here would always return an empty
                // string and the tile caption would show the raw UUID.
                const QString name = m.value(QStringLiteral("displayName")).toString();
                return name.isEmpty() ? tokenOrId : name;
            }
        }
        return tokenOrId;
    };
    // Snapping layouts are stored by UUID, which matches the layouts-list id
    // directly. Tiling-algorithm actions, however, store the BARE algorithm
    // token ("bsp"), while the layouts list keys autotile entries by the
    // "autotile:<token>" form — so the bare token must be prefixed before the
    // lookup, or the list shows the raw id instead of the friendly name. Try
    // the prefixed form first, then fall back to the bare token (covering the
    // bare-keyed shape PhosphorTiles can also ship, and already-prefixed data).
    auto resolveTilingAlgorithmLookup = [resolveByLayoutsLookup](const QString& algorithmToken) -> QString {
        const QString prefixed = QStringLiteral("autotile:") + algorithmToken;
        const QString label = resolveByLayoutsLookup(prefixed);
        return label == prefixed ? resolveByLayoutsLookup(algorithmToken) : label;
    };
    m_rulesPage->setSnappingLayoutLookup(resolveByLayoutsLookup);
    m_rulesPage->setTilingAlgorithmLookup(resolveTilingAlgorithmLookup);
    // OverrideAnimationShader actions store an effect id ("dissolve"); resolve
    // it to the friendly name via the same animation shader registry the rule
    // editor's shader picker reads (availableShaderEffects), so the list shows
    // "Shader: Dissolve" rather than the raw id. Unknown ids round-trip
    // verbatim (registry miss → raw id), matching the editor's fallback.
    auto resolveShaderEffectLookup = [this](const QString& effectId) -> QString {
        if (effectId.isEmpty() || !m_animationShaderRegistry || !m_animationShaderRegistry->hasEffect(effectId)) {
            return effectId;
        }
        const QString name = m_animationShaderRegistry->effect(effectId).name;
        return name.isEmpty() ? effectId : name;
    };
    m_rulesPage->setShaderEffectLookup(resolveShaderEffectLookup);
    // OverrideOverlayShader stores an overlay/snapping shader id; resolve it to
    // the friendly name via the overlay shader registry (the same source the
    // rule editor's overlay-shader picker reads), so the list shows
    // "Overlay shader: <name>" rather than the raw id. Unknown ids round-trip
    // verbatim (registry miss → empty name → raw id). m_overlayShaderRegistry is
    // constructed later in this ctor; the `!m_overlayShaderRegistry` guard below
    // covers that window — the lambda captures `this` and is invoked only lazily
    // (on the model's first label render, after construction completes).
    auto resolveOverlayShaderLookup = [this](const QString& effectId) -> QString {
        if (effectId.isEmpty() || !m_overlayShaderRegistry) {
            return effectId;
        }
        const QString name = m_overlayShaderRegistry->shader(effectId).name;
        return name.isEmpty() ? effectId : name;
    };
    m_rulesPage->setOverlayShaderLookup(resolveOverlayShaderLookup);
    // OverrideDecorationChain stores surface-pack ids ("frosted-glass");
    // resolve them to friendly names via the surface shader registry (the
    // same source the decoration pages' pack picker reads), so the list
    // shows "Decoration: Frosted Glass, Glow" rather than raw ids. Unknown
    // ids round-trip verbatim, matching the other lookups' fallbacks.
    auto resolveDecorationPackLookup = [this](const QString& packId) -> QString {
        if (packId.isEmpty() || !m_surfaceShaderRegistry || !m_surfaceShaderRegistry->hasEffect(packId)) {
            return packId;
        }
        const QString name = m_surfaceShaderRegistry->effect(packId).name;
        return name.isEmpty() ? packId : name;
    };
    m_rulesPage->setDecorationPackLookup(resolveDecorationPackLookup);
    auto refreshRuleLabels = [this]() {
        if (m_rulesPage && m_rulesPage->model()) {
            m_rulesPage->model()->refreshLabels();
        }
    };
    connect(this, &SettingsController::screensChanged, this, refreshRuleLabels);
    connect(this, &SettingsController::activitiesChanged, this, refreshRuleLabels);
    connect(this, &SettingsController::layoutsChanged, this, refreshRuleLabels);
    // A shader-pack rescan (user drops in a new effect, or one is removed)
    // can change an id→name mapping; refresh so resolved Shader labels track it.
    connect(m_animationShaderRegistry, &PhosphorAnimationShaders::AnimationShaderRegistry::effectsChanged, this,
            refreshRuleLabels);
    // Same refresh for surface-pack rescans so resolved Decoration labels
    // track pack installs/removals.
    connect(m_surfaceShaderRegistry, &PhosphorSurfaceShaders::SurfaceShaderRegistry::effectsChanged, this,
            refreshRuleLabels);

    // Overlay shader registry — settings-side mirror of the daemon's. The
    // PlasmaZones::ShaderRegistry subclass auto-wires the standard system
    // + user search paths (`plasmazones/overlays`), so no extra path
    // bookkeeping is needed here. Read-only browser surface — there is no
    // per-event override store; assignments live on `Layout::shaderId`
    // (per-layout) and the snapping page surfaces "Used by" by walking
    // m_localLayoutManager's catalogue.
    //
    // The page controller is a `unique_ptr<>` declared after
    // `m_localLayoutManager` (see header). Constructed without a QObject
    // parent so member-destructor reverse-order tears it down BEFORE the
    // borrowed layout registry — a QObject-child parent would defer
    // destruction to ~QObject, dangling the layout-registry pointer.
    m_overlayShaderRegistry = new PlasmaZones::ShaderRegistry(this);

    // Shared live-preview feed (T3.1): backed by the local overlay registry +
    // settings (audio-visualizer config). Owned here (unique_ptr, no QObject
    // parent) so it tears down before m_overlayShaderRegistry / m_settings; the
    // browser bridge below borrows it.
    m_shaderPreviewBackend = std::make_unique<RegistryShaderPreviewBackend>(m_overlayShaderRegistry, &m_settings);
    m_shaderPreviewController = std::make_unique<ShaderPreviewController>(m_shaderPreviewBackend.get());

    // Parented to `this` for the same reason as m_tilingAlgorithmPage above:
    // without a parent, registerPage would adopt it to m_app (destroyed first)
    // and the unique_ptr would then double-free it on close. The unique_ptr
    // still drives destruction in member order, before the borrowed registries.
    m_snappingShadersPage = std::make_unique<SnappingShadersPageController>(
        m_overlayShaderRegistry, m_localLayoutManager.get(), m_shaderPreviewController.get(), this);

    // Screen helper signals — wire BEFORE the initial refreshScreens()
    // so a synchronous screensChanged emit from the refresh reaches our
    // forwarders (and the dirty-tracking guard). Previously refreshScreens
    // ran first and any same-tick screensChanged was lost, leaving QML
    // bindings stale until the next external daemon-driven refresh.
    m_screenHelper.connectToDaemonSignals();
    // Hot-unplug: if the monitor the per-monitor groups are scoped to goes
    // away, fall back to "All Monitors". Lives here (not in the transient
    // DisplayMap popover) so the scope is pruned even when no scope UI is open.
    // Connected BEFORE the QML-facing screensChanged forward below so a QML
    // onScreensChanged handler observes the already-pruned scope rather than a
    // stale value that only corrects on the trailing scopeScreenNameChanged.
    connect(&m_screenHelper, &ScreenHelper::screensChanged, this, [this]() {
        if (m_scopeScreenName.isEmpty())
            return;
        // Compare physical parents on BOTH sides so the scope survives removal
        // of a sibling virtual child of the same physical output (stripping
        // only the live name while leaving the stored scope unstripped would
        // never match a virtual-id scope).
        const QString scopePhysicalId = PhosphorIdentity::VirtualScreenId::extractPhysicalId(m_scopeScreenName);
        const QVariantList liveScreens = m_screenHelper.screens();
        for (const QVariant& v : liveScreens) {
            const QString name = v.toMap().value(QStringLiteral("name")).toString();
            // String-equal in the common case (both forms come from the same
            // daemon payload), physical-parent-equal for virtual siblings, and
            // screensMatch() as a connector ↔ EDID-id reconciler so a scope
            // stored in one form survives a live name reported in the other.
            if (name == m_scopeScreenName
                || PhosphorIdentity::VirtualScreenId::extractPhysicalId(name) == scopePhysicalId
                || PhosphorScreens::ScreenIdentity::screensMatch(name, m_scopeScreenName))
                return;
        }
        setScopeScreenName(QString());
    });
    connect(&m_screenHelper, &ScreenHelper::screensChanged, this, &SettingsController::screensChanged);
    connect(&m_screenHelper, &ScreenHelper::needsSave, this, [this]() {
        // A daemon-driven screen refresh that fires while load()/save() is
        // batching its own state-transitions must not flip the page dirty
        // — the same guard every other dirty-tracking path uses.
        if (m_loading || m_saving)
            return;
        setNeedsSave(true);
    });
    m_screenHelper.refreshScreens();

    // PhosphorZones::Layout load timer (debounce). The five layout-mutation
    // subscriptions (layoutCreated / layoutDeleted / layoutChanged /
    // layoutPropertyChanged / layoutListChanged) funnel into the 50 ms debounce
    // slot below, so a burst of them coalesces into a single
    // loadLayoutsAsync(). They are wired in
    // settingscontroller_dbuswire.cpp::wireDaemonSubscriptions, alongside the
    // screen-layout, quick-slot, virtual-desktop and activity broadcasts, which
    // reach their own slots rather than this timer.
    m_layoutLoadTimer.setSingleShot(true);
    m_layoutLoadTimer.setInterval(50);
    connect(&m_layoutLoadTimer, &QTimer::timeout, this, &SettingsController::loadLayoutsAsync);

    // (Editor NOTIFY signals are forwarded to QML by EditorPageController
    // itself — see its constructor — so no SettingsController-side plumbing
    // is needed here.)

    // Load dismissed update version from app-local settings
    {
        QSettings appSettings;
        m_dismissedUpdateVersion = appSettings.value(ConfigDefaults::settingsAppDismissedUpdateVersionKey()).toString();
        m_lastSeenWhatsNewVersion =
            appSettings.value(ConfigDefaults::settingsAppLastSeenWhatsNewVersionKey()).toString();
    }

    // Load What's New entries from embedded resource
    {
        QFile whatsNewFile(QStringLiteral(":/whatsnew.json"));
        if (whatsNewFile.open(QIODevice::ReadOnly)) {
            const auto doc = QJsonDocument::fromJson(whatsNewFile.readAll());

            // Validate against the embedded schema before consuming. The same
            // schema CI validates the file against. fromResource fails closed if
            // the resource is missing (a build error), so malformed or
            // unvalidatable data is skipped rather than surfaced (the page
            // simply shows nothing).
            const auto validator = PhosphorFsLoader::SchemaValidator::fromResource(
                QStringLiteral(":/schemas/whatsnew.schema.json"), PlasmaZones::lcCore());
            QJsonArray releases;
            if (const auto errors = validator.validate(doc.object())) {
                qCWarning(PlasmaZones::lcCore) << "whatsnew.json failed schema validation; skipping What's New entries";
                PhosphorFsLoader::logSchemaErrors(PlasmaZones::lcCore(), *errors);
            } else {
                releases = doc.object().value(QLatin1String("releases")).toArray();
            }
            for (const auto& entry : releases) {
                const auto obj = entry.toObject();
                QVariantMap release;
                release[QStringLiteral("version")] = obj.value(QLatin1String("version")).toString();
                release[QStringLiteral("date")] = obj.value(QLatin1String("date")).toString();
                QVariantList highlights;
                const auto arr = obj.value(QLatin1String("highlights")).toArray();
                for (const auto& h : arr)
                    highlights.append(h.toString());
                release[QStringLiteral("highlights")] = highlights;
                m_whatsNewEntries.append(release);
            }
        }
    }

    // PhosphorControl integration — must run AFTER every page controller
    // has been constructed (the registry holds stable pointers to them).
    buildApplicationController();

    // Summary if any D-Bus subscription dropped during ctor. Surfaces a
    // single boot-time line listing every unwired route so the operator
    // can quickly see "daemon was not up at settings-app start, these
    // signals are silently dead" — far easier to diagnose than scattered
    // per-call warnings throughout the log.
    if (!failedSubscriptions.isEmpty()) {
        qCWarning(PlasmaZones::lcCore)
            << "SettingsController: " << failedSubscriptions.size()
            << " D-Bus signal subscription(s) failed at construction — affected routes:"
            << failedSubscriptions.join(QStringLiteral(", "))
            << "— corresponding live-update features will be inert until the next settings-app launch.";
    }

    // Initial loads
    scheduleLayoutLoad();
    refreshVirtualDesktops();
    refreshActivities();
    m_updateChecker.checkForUpdates();
}

// Out-of-line page getters (kept here rather than inline in the header to hold
// settingscontroller.h under the 800-line cap).
SnappingZonesController* SettingsController::snappingZonesPage() const
{
    return m_snappingZonesPage;
}

WindowAppearanceController* SettingsController::windowAppearancePage() const
{
    return m_windowAppearancePage;
}

SnappingEffectsController* SettingsController::snappingEffectsPage() const
{
    return m_snappingEffectsPage;
}

SnappingShadersPageController* SettingsController::snappingShadersPage() const
{
    return m_snappingShadersPage.get();
}

TilingAlgorithmController* SettingsController::tilingAlgorithmPage() const
{
    return m_tilingAlgorithmPage.get();
}

// setActivePage / dirty-tracking / external-edit methods live in
// settingscontroller_pagestate.cpp (split to keep this file under the
// 800-line cap).

} // namespace PlasmaZones
