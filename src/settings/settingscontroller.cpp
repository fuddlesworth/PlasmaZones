// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingscontroller.h"

#include "editorpagecontroller.h"
#include "generalpagecontroller.h"
#include "kzonesimporter.h"
#include "snappingappearancecontroller.h"
#include "snappingbehaviorcontroller.h"
#include "snappingeffectscontroller.h"
#include "snappingzoneselectorcontroller.h"
#include "tilingalgorithmcontroller.h"
#include "tilingappearancecontroller.h"
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
#include "../pz_i18n.h"
#include "dbusutils.h"
#include "pageadapter.h"
#include "settingsstagingdomain.h"
#include "version.h"

#include <PhosphorProtocol/ClientHelpers.h>
// std::make_unique<WindowRuleStore> in the ctor needs the complete
// type. The header forward-declares it to avoid pulling the
// dependency graph into every consumer of SettingsController.
#include <PhosphorWindowRule/WindowRuleStore.h>

#include "../core/shaderregistry.h"
#include "snappingshaderspagecontroller.h"

#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorLayoutApi/LayoutPreview.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorTiles/ScriptedAlgorithm.h>
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

SettingsController::~SettingsController() = default;

// ensureScreenIdResolver() now lives in src/common/screenidresolver.{h,cpp}
// so daemon/editor/settings share the same install-once helper instead of
// maintaining three parallel copies.

SettingsController::SettingsController(QObject* parent)
    : QObject(parent)
    , m_screenHelper(&m_settings, this)
    , m_localAlgorithmRegistry(std::make_unique<PhosphorTiles::AlgorithmRegistry>(nullptr))
    , m_localRuleStore(std::make_unique<PhosphorWindowRule::WindowRuleStore>(ConfigDefaults::windowRulesFilePath()))
    , m_localLayoutManager(
          std::make_unique<PhosphorZones::LayoutRegistry>(m_localRuleStore.get(), ConfigDefaults::layoutsSubdir()))
{
    // Install the library-level screen-id resolver before any layout load
    // runs. First call initialises the static; subsequent constructions
    // in the same process reuse it. Moved out of the ctor-initializer
    // comma-operator trick so the intent is obvious at a glance —
    // matches the daemon's handling.
    ensureScreenIdResolver();

    // Auto-discovery pattern: every linked provider library has
    // already registered a builder via static-init. The KCM just
    // publishes the registries it owns via the shared helper
    // (buildStandardLayoutSourceBundle) so the context-wiring is the
    // same across daemon/editor/settings. Adding a new engine library
    // doesn't require editing this file unless the engine demands a
    // service the KCM doesn't already publish.
    buildStandardLayoutSourceBundle(m_localSources, m_localLayoutManager.get(), m_localAlgorithmRegistry.get());

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
        m_layouts = std::move(localLayouts);
        // Suppress the local-path emit while a D-Bus getLayoutList
        // call is in flight — the async reply lambda will emit once
        // it replaces m_layouts with the daemon-enriched view. If the
        // daemon is unreachable or the call errors, the gate is
        // cleared in the reply lambda's head and subsequent local
        // emits run normally (fallback behaviour).
        if (!m_awaitingDaemonLayouts) {
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
    m_algorithmService =
        std::make_unique<AlgorithmService>(&m_settings, m_localAlgorithmRegistry.get(), m_scriptLoader.get());
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

    // D-Bus subscription helper. QDBusConnection::connect's API is
    // fundamentally string-based (signal name + SLOT() signature) —
    // it can't use the modern member-function-pointer connect syntax
    // because D-Bus signals are dynamically named. The lambda just
    // factors the repeated `service + objectPath` tuple out of every
    // call site so each subscription is one line, and centralises
    // failure logging in one place.
    //
    // The const-char* SLOT signature is normalised through
    // QMetaObject::normalizedSignature inside QDBusConnection, so
    // spacing variations between call sites are harmless — but the
    // helper takes the un-normalised string so call sites can be
    // grep'd consistently.
    // Accumulate failed subscription tuples so the post-loop summary can
    // surface ALL missing routes at once, rather than scattering one
    // warning per failure across the boot log. Helps diagnose the
    // "daemon not up yet at construct time" case where many signals
    // miss together.
    QStringList failedSubscriptions;
    const auto subscribeDaemonSignal = [this, &failedSubscriptions](const QString& interfaceName,
                                                                    const QString& signalName, const char* slot) {
        const bool ok = QDBusConnection::sessionBus().connect(QString(PhosphorProtocol::Service::Name),
                                                              QString(PhosphorProtocol::Service::ObjectPath),
                                                              interfaceName, signalName, this, slot);
        if (!ok) {
            qCWarning(PlasmaZones::lcCore)
                << "SettingsController: failed to connect D-Bus signal" << signalName << "on" << interfaceName;
            failedSubscriptions.append(interfaceName + QStringLiteral(".") + signalName);
        }
    };

    // Listen for external settings changes from the daemon
    const QString settingsIface = QString(PhosphorProtocol::Service::Interface::Settings);
    subscribeDaemonSignal(settingsIface, QStringLiteral("settingsChanged"), SLOT(onExternalSettingsChanged()));

    // Async window picker reply channel. Emitted by SettingsAdaptor whenever
    // the KWin effect answers a runningWindowsRequested call via
    // provideRunningWindows(). The signal carries the JSON payload directly
    // so clients don't need a follow-up blocking fetch.
    subscribeDaemonSignal(settingsIface, QStringLiteral("runningWindowsAvailable"),
                          SLOT(onRunningWindowsAvailable(QString)));

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
    // Use a meta-object connection to catch all NOTIFY signals. The slot
    // lookup is cached + asserted up front so a future rename of
    // `onSettingsPropertyChanged()` doesn't silently turn this loop into
    // a no-op (indexOfSlot returns -1 on miss and `method(-1)` is an
    // invalid QMetaMethod that the connect() call ignores).
    const int settingsChangedSlotIdx = metaObject()->indexOfSlot("onSettingsPropertyChanged()");
    Q_ASSERT_X(settingsChangedSlotIdx >= 0, "SettingsController::ctor",
               "onSettingsPropertyChanged() slot not found — was it renamed?");
    if (settingsChangedSlotIdx < 0) {
        // Release-build fallback: Q_ASSERT compiles to nothing, the
        // metaObject()->method(-1) call returns an invalid QMetaMethod, and
        // every connect() below would silently no-op — turning the entire
        // dirty-tracking loop into a permanent regression with zero
        // diagnostics. Log critically and bail so the failure is at least
        // visible, even if the page can't dirty-track until the rename is
        // resolved.
        qCCritical(lcCore) << "SettingsController::ctor: onSettingsPropertyChanged() slot not found in meta-object — "
                              "Settings NOTIFY → dirty-tracking is DISABLED. Was the slot renamed?";
    } else {
        const QMetaMethod settingsChangedSlot = metaObject()->method(settingsChangedSlotIdx);
        const QMetaObject* mo = m_settings.metaObject();
        // Walk from 0 (not propertyOffset()) so Q_PROPERTYs declared on the
        // ISettings base or any future intermediate class are also wired —
        // hasNotifySignal() filters out properties without NOTIFY.
        for (int i = 0; i < mo->propertyCount(); ++i) {
            QMetaProperty prop = mo->property(i);
            if (prop.hasNotifySignal()) {
                connect(&m_settings, prop.notifySignal(), this, settingsChangedSlot);
            }
        }
    }

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
    m_snappingBehaviorPage = new SnappingBehaviorController(&m_settings, this);
    m_tilingBehaviorPage = new TilingBehaviorController(&m_settings, this);

    // Snapping→Zone Selector page sub-controller. Pure CONSTANT bounds
    // facade over ConfigDefaults — no Settings wiring required.
    m_snappingZoneSelectorPage = new SnappingZoneSelectorController(this);

    // Snapping→Appearance page sub-controller. Owns border bounds plus the
    // color-import action surface; its changed() signal drives dirty
    // tracking on successful imports.
    m_snappingAppearancePage = new SnappingAppearanceController(&m_settings, this);
    connect(m_snappingAppearancePage, &SnappingAppearanceController::changed, this,
            &SettingsController::onSettingsPropertyChanged);

    // Snapping→Effects + Tiling→Appearance pages — CONSTANT-only bounds facades.
    m_snappingEffectsPage = new SnappingEffectsController(this);
    m_tilingAppearancePage = new TilingAppearanceController(this);

    // Tiling→Algorithm page sub-controller. Owns 7 slider bounds + the
    // custom-parameter CRUD surface. Borrows the algorithm registry this
    // controller already owns; declared as a unique_ptr AFTER
    // m_localAlgorithmRegistry so reverse-order member destruction tears
    // the sub-controller down BEFORE the registry resets. Parenting to
    // `this` would defer destruction to ~QObject, which runs AFTER the
    // registry unique_ptr — leaving the controller's raw m_registry pointer
    // briefly dangling.
    m_tilingAlgorithmPage =
        std::make_unique<TilingAlgorithmController>(&m_settings, m_localAlgorithmRegistry.get(), nullptr);
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
    {
        // Centralised subdir constant (with leading "/") — strip the slash for
        // locateAll's relative-path arg, keep it as-is for the writable-base
        // join. This matches AnimationsPageController::userShaderDirectoryPath
        // so the two settings-side consumers can never drift apart.
        const QString subdir = ConfigDefaults::userAnimationsSubdir();
        QStringList animDirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, subdir.mid(1),
                                                         QStandardPaths::LocateDirectory);
        std::reverse(animDirs.begin(), animDirs.end());
        const QString userAnimDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + subdir;
        if (!animDirs.contains(userAnimDir))
            animDirs.append(userAnimDir);
        QDir().mkpath(userAnimDir);
        m_animationShaderRegistry->setUserPath(userAnimDir);
        m_animationShaderRegistry->addSearchPaths(animDirs);
    }

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
        // window-rules handler below.
        beginExternalEdit(QStringLiteral("animations-general"));
        setNeedsSave(true);
        endExternalEdit();
    });

    // Window Rules page sub-controller — the unified rule surface. It owns
    // its own WindowRuleModel and talks to the daemon's
    // org.plasmazones.WindowRules adaptor. Dirty-tracking mirrors the
    // animations page: a staged edit flips needsSave; commit/revert run
    // from this controller's save()/load() so they don't race the
    // setNeedsSave(false) those methods emit.
    m_windowRulesPage = new WindowRuleController(this);
    connect(m_windowRulesPage, &WindowRuleController::dirtyChanged, this, [this]() {
        if (m_loading || m_saving)
            return;
        if (m_windowRulesPage->isDirty()) {
            // A window-rule edit can be driven by a background daemon signal, not
            // just by the user viewing the page — so mark the "window-rules" page
            // explicitly rather than letting setNeedsSave() target m_activePage.
            beginExternalEdit(QStringLiteral("window-rules"));
            setNeedsSave(true);
            endExternalEdit();
            return;
        }
        // Controller transitioned to clean (e.g. a successful fetchAndLoad
        // flipped m_dirty false→true→false during initial async load, or a
        // direct revert from QML). Mirror the dirty-side behaviour: remove
        // "window-rules" from m_dirtyPages and emit dirtyPagesChanged when
        // the set actually shrinks. setNeedsSave(false) cannot be used here
        // — it blanket-clears every page, which would wipe other unrelated
        // dirty leaves.
        if (m_dirtyPages.remove(QStringLiteral("window-rules"))) {
            Q_EMIT dirtyPagesChanged();
        }
    });
    // A user-driven Discard fires WindowRuleController::revert() inside our
    // load() under m_loading=true, which suppresses the dirtyChanged → dirty
    // pages plumbing for the duration of the call. The async re-fetch lands
    // AFTER load() has already done `setNeedsSave(false)` (which blanket-
    // clears m_dirtyPages). If the re-fetch *fails*, the controller's m_dirty
    // stays true per its documented contract — but its dirtyChanged signal
    // never fires (value didn't change) and the cleared dirty-page entry is
    // never re-added. Listen to revertFinished here so a failed revert can
    // re-mark the page dirty.
    connect(m_windowRulesPage, &WindowRuleController::revertFinished, this, [this](bool success) {
        if (success || !m_windowRulesPage->isDirty()) {
            return;
        }
        // m_loading is already false by the time this async reply lands.
        beginExternalEdit(QStringLiteral("window-rules"));
        setNeedsSave(true);
        endExternalEdit();
    });

    // Wire screen / activity / layout label resolvers so the rule model and
    // monitor-overview render friendly names instead of raw connector strings,
    // activity UUIDs and layout UUIDs.
    //
    // The closures capture `this` and read live snapshot state on every call,
    // so they need to be installed exactly ONCE — re-installing on every
    // upstream change was wasteful (three model-wide `dataChanged` emits per
    // signal × three signals = nine emits). Upstream changes are now routed
    // to `WindowRuleModel::refreshLabels()` which emits a single dataChanged
    // covering every label-derived role.
    m_windowRulesPage->setScreenLookup([this](const QString& screenId) -> QString {
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
    m_windowRulesPage->setActivityLookup([this](const QString& activityId) -> QString {
        for (const QVariant& av : std::as_const(m_activities)) {
            const QVariantMap m = av.toMap();
            if (m.value(QStringLiteral("id")).toString() == activityId) {
                const QString name = m.value(QStringLiteral("name")).toString();
                return name.isEmpty() ? activityId : name;
            }
        }
        return activityId;
    });
    m_windowRulesPage->setLayoutLookup([this](const QString& layoutId) -> QString {
        for (const QVariant& lv : std::as_const(m_layouts)) {
            const QVariantMap m = lv.toMap();
            if (m.value(QStringLiteral("id")).toString() == layoutId) {
                // Layouts are serialised via `toVariantMap(LayoutPreview)`
                // which stamps the friendly label under `displayName`, not
                // `name`. Reading `name` here would always return an empty
                // string and the tile caption would show the raw UUID.
                const QString name = m.value(QStringLiteral("displayName")).toString();
                return name.isEmpty() ? layoutId : name;
            }
        }
        return layoutId;
    });
    auto refreshRuleLabels = [this]() {
        if (m_windowRulesPage && m_windowRulesPage->model()) {
            m_windowRulesPage->model()->refreshLabels();
        }
    };
    connect(this, &SettingsController::screensChanged, this, refreshRuleLabels);
    connect(this, &SettingsController::activitiesChanged, this, refreshRuleLabels);
    connect(this, &SettingsController::layoutsChanged, this, refreshRuleLabels);

    // Overlay shader registry — settings-side mirror of the daemon's. The
    // PlasmaZones::ShaderRegistry subclass auto-wires the standard system
    // + user search paths (`plasmazones/shaders`), so no extra path
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
    m_snappingShadersPage =
        std::make_unique<SnappingShadersPageController>(m_overlayShaderRegistry, m_localLayoutManager.get());

    // Screen helper signals
    m_screenHelper.connectToDaemonSignals();
    m_screenHelper.refreshScreens();
    connect(&m_screenHelper, &ScreenHelper::screensChanged, this, &SettingsController::screensChanged);
    connect(&m_screenHelper, &ScreenHelper::needsSave, this, [this]() {
        // A daemon-driven screen refresh that fires while load()/save() is
        // batching its own state-transitions must not flip the page dirty
        // — the same guard every other dirty-tracking path uses.
        if (m_loading || m_saving)
            return;
        setNeedsSave(true);
    });

    // PhosphorZones::Layout load timer (debounce)
    m_layoutLoadTimer.setSingleShot(true);
    m_layoutLoadTimer.setInterval(50);
    connect(&m_layoutLoadTimer, &QTimer::timeout, this, &SettingsController::loadLayoutsAsync);

    // Connect layout D-Bus signals for live updates — route through the 50 ms
    // scheduleLayoutLoad() debounce slot so a burst of signals (e.g. editor
    // save → layoutChanged + layoutListChanged together, or KCM property
    // tweak → layoutPropertyChanged + layoutListChanged) coalesces into
    // one loadLayoutsAsync() call instead of recomputing the full preview
    // list + D-Bus round-trip for every hit.
    const QString layoutIface = QString(PhosphorProtocol::Service::Interface::LayoutRegistry);
    subscribeDaemonSignal(layoutIface, QStringLiteral("layoutCreated"), SLOT(scheduleLayoutLoad()));
    subscribeDaemonSignal(layoutIface, QStringLiteral("layoutDeleted"), SLOT(scheduleLayoutLoad()));
    // layoutChanged fires when a layout is modified (editor saves, zone changes, rename)
    subscribeDaemonSignal(layoutIface, QStringLiteral("layoutChanged"), SLOT(scheduleLayoutLoad()));
    // layoutPropertyChanged fires on compact property mutations (hidden, autoAssign,
    // aspectRatioClass) — Phase 4 of refactor/dbus-performance. The settings UI still
    // triggers a full reload so the layout list view refreshes, but the daemon side
    // saved a full JSON serialization per mutation by not emitting layoutChanged.
    subscribeDaemonSignal(layoutIface, QStringLiteral("layoutPropertyChanged"), SLOT(scheduleLayoutLoad()));
    // layoutListChanged fires when the layout list changes (editor, import, system layout reload)
    subscribeDaemonSignal(layoutIface, QStringLiteral("layoutListChanged"), SLOT(scheduleLayoutLoad()));
    // screenLayoutChanged(QString,QString,int) fires when assignments change (hotkeys, scripts, toggle)
    subscribeDaemonSignal(layoutIface, QStringLiteral("screenLayoutChanged"),
                          SLOT(onScreenLayoutChanged(QString, QString, int)));
    // quickLayoutSlotsChanged fires when quick layout slots are modified externally
    subscribeDaemonSignal(layoutIface, QStringLiteral("quickLayoutSlotsChanged"), SIGNAL(quickLayoutSlotsChanged()));

    // Connect virtual desktop / activity D-Bus signals for reactive updates
    subscribeDaemonSignal(layoutIface, QStringLiteral("virtualDesktopCountChanged"), SLOT(onVirtualDesktopsChanged()));
    subscribeDaemonSignal(layoutIface, QStringLiteral("virtualDesktopNamesChanged"), SLOT(onVirtualDesktopsChanged()));
    subscribeDaemonSignal(layoutIface, QStringLiteral("activitiesChanged"), SLOT(onActivitiesChanged()));
    subscribeDaemonSignal(layoutIface, QStringLiteral("currentActivityChanged"), SLOT(onActivitiesChanged()));

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
            const auto releases = doc.object().value(QLatin1String("releases")).toArray();
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

    // PhosphorSettingsUi integration — must run AFTER every page controller
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

void SettingsController::setActivePage(const QString& page)
{
    // Resolve parent category names (e.g. "snapping" → "snapping-appearance")
    const QString resolved = parentPageRedirects().value(page, page);

    if (!validPageNames().contains(resolved)) {
        qCWarning(PlasmaZones::lcCore) << "Unknown settings page:" << page;
        return;
    }
    if (m_activePage != resolved) {
        // m_loading suppresses onSettingsPropertyChanged — the QML Loader
        // reacts synchronously to activePageChanged and new page creation
        // may trigger NOTIFY signals that would otherwise mark pages dirty.
        m_loading = true;
        m_activePage = resolved;
        Q_EMIT activePageChanged();
        m_loading = false;
    }
}

void SettingsController::onSettingsPropertyChanged()
{
    if (!m_saving && !m_loading) {
        setNeedsSave(true);
    }
}

void SettingsController::onExternalSettingsChanged()
{
    if (!m_saving) {
        load();
    }
}

void SettingsController::setNeedsSave(bool needs)
{
    // Mark the target page as dirty, or clear all dirty pages if needs ==
    // false. The target is m_externalEditPage when set (sidebar / global
    // widgets that mutate settings owned by a different page than the one
    // the user is viewing), otherwise m_activePage. Parent categories
    // ("snapping", "tiling") are never the active page — setActivePage
    // redirects them to their first child — so the target always resolves
    // to a concrete leaf page.
    if (needs) {
        const QString target = m_externalEditPage.isEmpty() ? m_activePage : m_externalEditPage;
        Q_ASSERT(!parentPageRedirects().contains(target));
        if (!m_dirtyPages.contains(target)) {
            m_dirtyPages.insert(target);
            Q_EMIT dirtyPagesChanged();
        }
    } else if (!m_dirtyPages.isEmpty()) {
        m_dirtyPages.clear();
        Q_EMIT dirtyPagesChanged();
    }
}

QStringList SettingsController::dirtyPages() const
{
    // Order is unspecified — QML uses this only as a binding dependency
    // and calls isPageDirty() for the actual lookup.
    return QStringList(m_dirtyPages.begin(), m_dirtyPages.end());
}

bool SettingsController::isPageDirty(const QString& page) const
{
    if (m_dirtyPages.contains(page))
        return true;
    // Parent / virtual-parent category: dirty if any child leaf in
    // the group is dirty. Single direct-membership lookup against
    // `pageGroupChildren()` rather than the old prefix-walk-or-hash-
    // lookup branch — top-level parents (snapping / tiling /
    // animations) and virtual mid-level parents (animations-surfaces /
    // animations-library) share the same code path now.
    const auto& groups = pageGroupChildren();
    const auto it = groups.constFind(page);
    if (it != groups.constEnd()) {
        for (const QString& child : *it) {
            if (m_dirtyPages.contains(child))
                return true;
        }
    }
    return false;
}

void SettingsController::beginExternalEdit(const QString& page)
{
    // Resolve parent categories to their canonical leaf — same rules as
    // setActivePage — so the sidebar can pass "snapping" or "tiling".
    const QString resolved = parentPageRedirects().value(page, page);
    if (!validPageNames().contains(resolved)) {
        qCWarning(PlasmaZones::lcCore) << "beginExternalEdit: unknown page" << page;
        return;
    }
    // Nested begin without a prior end means the previous caller leaked
    // state — dirty tracking for subsequent changes would target the wrong
    // page. Warn loudly (debug-assert in dev builds) and DO NOT overwrite
    // the existing target in release builds: silently replacing it would
    // mis-route every subsequent dirty signal until the outer caller's
    // matching endExternalEdit fires (at which point both targets are
    // cleared anyway). Better to drop the inner begin and keep the outer
    // target so the next edit still lands on a coherent page.
    if (!m_externalEditPage.isEmpty()) {
        qCWarning(PlasmaZones::lcCore) << "beginExternalEdit: nested call without endExternalEdit — previous target:"
                                       << m_externalEditPage << "new target:" << resolved << "(ignoring nested begin)";
        Q_ASSERT_X(false, "SettingsController::beginExternalEdit",
                   "Nested call without endExternalEdit. Wrap sidebar/global edits with matched begin/end pairs.");
        return;
    }
    m_externalEditPage = resolved;
}

void SettingsController::endExternalEdit()
{
    m_externalEditPage.clear();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Font helpers (for FontPickerDialog)
// ═══════════════════════════════════════════════════════════════════════════════

} // namespace PlasmaZones
