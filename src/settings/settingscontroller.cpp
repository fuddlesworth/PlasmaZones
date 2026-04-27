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
#include "../core/layoutworker/layoutcomputeservice.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include "../pz_i18n.h"
#include "dbusutils.h"
#include "version.h"

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

// File-scope helper used by both the ctor (file-watcher rebind path) and
// loadLayoutsAsync (D-Bus refresh path). Manual layouts sort first;
// within each category alphabetical by name.
static void sortMergedLayoutList(QVariantList& list)
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
    , m_localLayoutManager(std::make_unique<PhosphorZones::LayoutRegistry>(createAssignmentsBackend(),
                                                                           QStringLiteral("plasmazones/layouts")))
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
        if (!localLayouts.isEmpty()) {
            sortMergedLayoutList(localLayouts);
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
        }
    });

    // Load the user's layouts immediately so localLayoutPreviews() returns
    // a populated list on first call (before any QML query has had a
    // chance to trigger the legacy D-Bus loadLayoutsAsync path). The
    // PhosphorZones::LayoutRegistry scans ~/.local/share/plasmazones/layouts/ on demand
    // and installs a QFileSystemWatcher so any subsequent disk changes
    // (daemon writes, editor saves) auto-reload without a D-Bus round-trip.
    m_localLayoutManager->loadLayouts();
    // Force a synchronous recalc over the primary screen so manual layouts
    // with fixed-geometry zones have a non-empty lastRecalcGeometry() —
    // ZonesLayoutSource reads that to populate LayoutPreview::zones and
    // referenceAspectRatio. Without this, settings-process previews render
    // with zero-size rects for authored-pixel layouts. Daemon runs the
    // same recalc in Daemon::init(); settings does it here because it owns
    // an in-process PhosphorZones::LayoutRegistry independent of the daemon.
    recalcLocalLayouts();

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
    m_scriptLoader->scanAndRegister();

    // Algorithm-registry / scripted-loader surface. Owned via unique_ptr so
    // reverse-order destruction runs the service's dtor (which disconnects
    // its watchers) BEFORE m_scriptLoader and m_localAlgorithmRegistry
    // reset — the service holds raw-pointer borrows of both.
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

    // Listen for external settings changes from the daemon
    QDBusConnection::sessionBus().connect(QString(PhosphorProtocol::Service::Name),
                                          QString(PhosphorProtocol::Service::ObjectPath),
                                          QString(PhosphorProtocol::Service::Interface::Settings),
                                          QStringLiteral("settingsChanged"), this, SLOT(onExternalSettingsChanged()));

    // Async window picker reply channel. Emitted by SettingsAdaptor whenever
    // the KWin effect answers a runningWindowsRequested call via
    // provideRunningWindows(). The signal carries the JSON payload directly
    // so clients don't need a follow-up blocking fetch.
    QDBusConnection::sessionBus().connect(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::Settings), QStringLiteral("runningWindowsAvailable"), this,
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

    // Mark needsSave when any Settings property changes (from QML edits)
    // Use a meta-object connection to catch all NOTIFY signals
    const QMetaObject* mo = m_settings.metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        QMetaProperty prop = mo->property(i);
        if (prop.hasNotifySignal()) {
            connect(&m_settings, prop.notifySignal(), this,
                    metaObject()->method(metaObject()->indexOfSlot("onSettingsPropertyChanged()")));
        }
    }

    // Editor + fill-on-drop settings lack Q_PROPERTY on Settings, so the
    // meta-object loop above misses them. EditorPageController forwards each
    // NOTIFY to QML and emits changed() which drives dirty tracking here.
    m_editorPage = new EditorPageController(&m_settings, this);
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
    m_generalPage = new GeneralPageController(&m_settings, this);

    // Screen helper signals
    m_screenHelper.connectToDaemonSignals();
    m_screenHelper.refreshScreens();
    connect(&m_screenHelper, &ScreenHelper::screensChanged, this, &SettingsController::screensChanged);
    connect(&m_screenHelper, &ScreenHelper::needsSave, this, [this]() {
        setNeedsSave(true);
    });

    // Forward the per-mode disable signals from the underlying Settings up
    // through the controller so QML bridges react regardless of write origin:
    //   - in-process toggle in this UI (controller invokables → Settings setters)
    //   - cross-process external write reflected via load() (daemon shortcut,
    //     D-Bus call to SettingsAdaptor) — see Settings::load() for the
    //     post-reparse emission of these signals
    //   - ScreenHelper-mediated monitor toggle (the helper writes through to
    //     Settings, which fires the underlying signal)
    // Single forwarding point keeps the three modes' write paths symmetrical
    // and avoids per-callsite Q_EMIT bookkeeping.
    connect(&m_settings, &ISettings::disabledMonitorsChanged, this, [this](PhosphorZones::AssignmentEntry::Mode mode) {
        Q_EMIT disabledMonitorsChanged(static_cast<int>(mode));
    });
    connect(&m_settings, &ISettings::disabledDesktopsChanged, this, [this](PhosphorZones::AssignmentEntry::Mode mode) {
        Q_EMIT disabledDesktopsChanged(static_cast<int>(mode));
    });
    connect(&m_settings, &ISettings::disabledActivitiesChanged, this,
            [this](PhosphorZones::AssignmentEntry::Mode mode) {
                Q_EMIT disabledActivitiesChanged(static_cast<int>(mode));
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
    QDBusConnection::sessionBus().connect(QString(PhosphorProtocol::Service::Name),
                                          QString(PhosphorProtocol::Service::ObjectPath),
                                          QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                          QStringLiteral("layoutCreated"), this, SLOT(scheduleLayoutLoad()));
    QDBusConnection::sessionBus().connect(QString(PhosphorProtocol::Service::Name),
                                          QString(PhosphorProtocol::Service::ObjectPath),
                                          QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                          QStringLiteral("layoutDeleted"), this, SLOT(scheduleLayoutLoad()));
    // layoutChanged fires when a layout is modified (editor saves, zone changes, rename)
    QDBusConnection::sessionBus().connect(QString(PhosphorProtocol::Service::Name),
                                          QString(PhosphorProtocol::Service::ObjectPath),
                                          QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                          QStringLiteral("layoutChanged"), this, SLOT(scheduleLayoutLoad()));
    // layoutPropertyChanged fires on compact property mutations (hidden, autoAssign,
    // aspectRatioClass) — Phase 4 of refactor/dbus-performance. The settings UI still
    // triggers a full reload so the layout list view refreshes, but the daemon side
    // saved a full JSON serialization per mutation by not emitting layoutChanged.
    QDBusConnection::sessionBus().connect(QString(PhosphorProtocol::Service::Name),
                                          QString(PhosphorProtocol::Service::ObjectPath),
                                          QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                          QStringLiteral("layoutPropertyChanged"), this, SLOT(scheduleLayoutLoad()));
    // layoutListChanged fires when the layout list changes (editor, import, system layout reload)
    QDBusConnection::sessionBus().connect(QString(PhosphorProtocol::Service::Name),
                                          QString(PhosphorProtocol::Service::ObjectPath),
                                          QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                          QStringLiteral("layoutListChanged"), this, SLOT(scheduleLayoutLoad()));
    // screenLayoutChanged(QString,QString,int) fires when assignments change (hotkeys, scripts, toggle)
    QDBusConnection::sessionBus().connect(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::LayoutRegistry), QStringLiteral("screenLayoutChanged"), this,
        SLOT(onScreenLayoutChanged(QString, QString, int)));
    // quickLayoutSlotsChanged fires when quick layout slots are modified externally
    QDBusConnection::sessionBus().connect(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::LayoutRegistry), QStringLiteral("quickLayoutSlotsChanged"), this,
        SIGNAL(quickLayoutSlotsChanged()));

    // Connect virtual desktop / activity D-Bus signals for reactive updates
    QDBusConnection::sessionBus().connect(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::LayoutRegistry), QStringLiteral("virtualDesktopCountChanged"),
        this, SLOT(onVirtualDesktopsChanged()));
    QDBusConnection::sessionBus().connect(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::LayoutRegistry), QStringLiteral("virtualDesktopNamesChanged"),
        this, SLOT(onVirtualDesktopsChanged()));
    QDBusConnection::sessionBus().connect(QString(PhosphorProtocol::Service::Name),
                                          QString(PhosphorProtocol::Service::ObjectPath),
                                          QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                          QStringLiteral("activitiesChanged"), this, SLOT(onActivitiesChanged()));
    QDBusConnection::sessionBus().connect(QString(PhosphorProtocol::Service::Name),
                                          QString(PhosphorProtocol::Service::ObjectPath),
                                          QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                          QStringLiteral("currentActivityChanged"), this, SLOT(onActivitiesChanged()));

    // (Editor NOTIFY signals are forwarded to QML by EditorPageController
    // itself — see its constructor — so no SettingsController-side plumbing
    // is needed here.)
    // Forward lock state changes (from Settings::load() after external D-Bus settingsChanged)
    connect(&m_settings, &Settings::lockedScreensChanged, this, &SettingsController::lockedScreensChanged);

    // Load dismissed update version from app-local settings
    {
        QSettings appSettings;
        m_dismissedUpdateVersion = appSettings.value(QStringLiteral("dismissedUpdateVersion")).toString();
        m_lastSeenWhatsNewVersion = appSettings.value(QStringLiteral("lastSeenWhatsNewVersion")).toString();
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

    // Initial loads
    scheduleLayoutLoad();
    refreshVirtualDesktops();
    refreshActivities();
    m_updateChecker.checkForUpdates();
}

void SettingsController::setDismissedUpdateVersion(const QString& version)
{
    if (m_dismissedUpdateVersion != version) {
        m_dismissedUpdateVersion = version;
        QSettings appSettings;
        appSettings.setValue(QStringLiteral("dismissedUpdateVersion"), version);
        Q_EMIT dismissedUpdateVersionChanged();
    }
}

void SettingsController::dismissUpdate()
{
    setDismissedUpdateVersion(m_updateChecker.latestVersion());
}

// Highest version among m_whatsNewEntries, using QVersionNumber so "1.10.0"
// sorts after "1.9.0" (plain string compare gets that wrong). Entries come
// from the bundled whatsnew.json resource in no guaranteed order.
QString SettingsController::latestWhatsNewVersion() const
{
    QVersionNumber best;
    QString bestStr;
    for (const QVariant& v : m_whatsNewEntries) {
        const QString ver = v.toMap().value(QStringLiteral("version")).toString();
        const QVersionNumber parsed = QVersionNumber::fromString(ver);
        if (parsed.isNull())
            continue;
        if (bestStr.isEmpty() || best < parsed) {
            best = parsed;
            bestStr = ver;
        }
    }
    return bestStr;
}

bool SettingsController::hasUnseenWhatsNew() const
{
    const QString latest = latestWhatsNewVersion();
    if (latest.isEmpty())
        return false;
    // Unseen iff the latest bundled entry is strictly newer than what the
    // user last marked seen. String compare after normalisation would still
    // mis-order "1.10" vs "1.9", so go through QVersionNumber.
    const QVersionNumber latestV = QVersionNumber::fromString(latest);
    const QVersionNumber seenV = QVersionNumber::fromString(m_lastSeenWhatsNewVersion);
    return seenV < latestV;
}

void SettingsController::markWhatsNewSeen()
{
    const QString latest = latestWhatsNewVersion();
    if (latest.isEmpty())
        return;
    if (m_lastSeenWhatsNewVersion != latest) {
        m_lastSeenWhatsNewVersion = latest;
        QSettings appSettings;
        appSettings.setValue(QStringLiteral("lastSeenWhatsNewVersion"), latest);
        Q_EMIT lastSeenWhatsNewVersionChanged();
    }
}

const QHash<QString, QString>& SettingsController::parentPageRedirects()
{
    // Parent sidebar categories have no QML component — resolve them to their
    // first child so D-Bus / CLI callers get a sensible result.
    static const QHash<QString, QString> redirects{
        {QStringLiteral("snapping"), QStringLiteral("snapping-appearance")},
        {QStringLiteral("tiling"), QStringLiteral("tiling-appearance")},
    };
    return redirects;
}

const QSet<QString>& SettingsController::validPageNames()
{
    // Keep in sync with _pageComponents in Main.qml — every entry here must
    // have a corresponding QML component file in that map.
    static const QSet<QString> pages{
        QStringLiteral("overview"),
        QStringLiteral("layouts"),
        QStringLiteral("snapping-appearance"),
        QStringLiteral("snapping-behavior"),
        QStringLiteral("snapping-zoneselector"),
        QStringLiteral("snapping-effects"),
        QStringLiteral("snapping-assignments"),
        QStringLiteral("snapping-shortcuts"),
        QStringLiteral("tiling-appearance"),
        QStringLiteral("tiling-behavior"),
        QStringLiteral("tiling-algorithm"),
        QStringLiteral("tiling-assignments"),
        QStringLiteral("tiling-shortcuts"),
        QStringLiteral("snapping-ordering"),
        QStringLiteral("tiling-ordering"),
        QStringLiteral("snapping-apprules"),
        QStringLiteral("exclusions"),
        QStringLiteral("editor"),
        QStringLiteral("general"),
        QStringLiteral("about"),
        QStringLiteral("virtualscreens"),
    };
    return pages;
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

void SettingsController::load()
{
    m_loading = true;
    m_settings.load();
    m_screenHelper.refreshScreens();
    scheduleLayoutLoad();
    m_staging.clearAll();
    m_stagedSnappingOrder.reset();
    m_stagedTilingOrder.reset();
    Q_EMIT stagedSnappingOrderChanged();
    Q_EMIT stagedTilingOrderChanged();
    m_loading = false;
    setNeedsSave(false);
}

void SettingsController::save()
{
    m_saving = true;

    // Flush staged ordering to settings before persisting
    if (m_stagedSnappingOrder.has_value()) {
        m_settings.setSnappingLayoutOrder(*m_stagedSnappingOrder);
        m_stagedSnappingOrder.reset();
    }
    if (m_stagedTilingOrder.has_value()) {
        m_settings.setTilingAlgorithmOrder(*m_stagedTilingOrder);
        m_stagedTilingOrder.reset();
    }

    // Persistence phase (pre-save): staged tiling-quick-slot writes + VS
    // configs need to be in Settings before the save flushes to disk.
    m_staging.flushTilingQuickSlotsToSettings(m_settings);
    m_staging.flushVirtualScreensToSettings(m_settings);

    // Save main settings (includes editor settings + VS configs persisted above)
    m_settings.save();

    // Flush staged VS configs to daemon BEFORE notifyReload so virtual screen
    // IDs exist when assignments referencing them are processed.
    m_staging.flushVirtualScreensToDaemon();

    // Notify daemon to reload KConfig settings (before D-Bus assignment mutations)
    DaemonDBus::notifyReload();

    // Flush staged snapping quick-layout slots via D-Bus (after reload).
    m_staging.flushSnappingQuickSlotsToDaemon();

    // Flush staged assignment changes to daemon (same batch protocol as KCM).
    // This must happen AFTER notifyReload so the reload doesn't overwrite
    // the assignment changes.
    if (m_staging.hasPendingAssignments()) {
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                               QStringLiteral("setSaveBatchMode"), {true});
        m_staging.flushAssignmentsToDaemon();
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                               QStringLiteral("applyAssignmentChanges"));
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                               QStringLiteral("setSaveBatchMode"), {false});
    }

    // Defer `m_saving = false` to the next event-loop tick. Although
    // notifyReload() is synchronous at the D-Bus level, the daemon's
    // reply-time emission of its own settingsChanged broadcast is a
    // separate D-Bus message that lands in this process's connection
    // queue and is dispatched only when control returns to the event
    // loop. Clearing m_saving immediately exposes a narrow race where
    // onExternalSettingsChanged() fires with m_saving=false and triggers
    // a spurious load() that reverts just-saved assignments. Posting the
    // reset through singleShot(0) drains those queued signals first, so
    // onExternalSettingsChanged() sees m_saving=true and returns early.
    setNeedsSave(false);
    QTimer::singleShot(0, this, [this]() {
        m_saving = false;
    });
}

void SettingsController::defaults()
{
    // reset() deletes all config groups, syncs to disk, then calls load()
    // internally — load()'s reflective NOTIFY emission would otherwise
    // route through onSettingsPropertyChanged and incrementally mark the
    // active page dirty before we overwrite m_dirtyPages below. Suppress
    // it so we get one clean dirtyPagesChanged emit instead of two.
    m_loading = true;
    m_settings.reset();
    m_loading = false;

    m_staging.clearAll();
    m_stagedSnappingOrder.reset();
    m_stagedTilingOrder.reset();
    Q_EMIT stagedSnappingOrderChanged();
    Q_EMIT stagedTilingOrderChanged();

    // Notify daemon to reload — reset() wrote defaults to disk
    DaemonDBus::notifyReload();

    // Defaults is a global action — mark every valid page dirty so the
    // unsaved indicator appears next to each of them.
    m_dirtyPages = validPageNames();
    Q_EMIT dirtyPagesChanged();
}

void SettingsController::launchEditor()
{
    QProcess::startDetached(QStringLiteral("plasmazones-editor"), {});
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
    // Parent category: dirty if any child page is dirty. The redirect map
    // lists parents → first child, so every key here is a parent category.
    if (parentPageRedirects().contains(page)) {
        const QString prefix = page + QStringLiteral("-");
        for (const QString& dirty : m_dirtyPages) {
            if (dirty.startsWith(prefix))
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
    // page. Warn loudly (debug-assert in dev builds) and fall through so
    // the new target wins.
    if (!m_externalEditPage.isEmpty()) {
        qCWarning(PlasmaZones::lcCore) << "beginExternalEdit: nested call without endExternalEdit — previous target:"
                                       << m_externalEditPage << "new target:" << resolved;
        Q_ASSERT_X(false, "SettingsController::beginExternalEdit",
                   "Nested call without endExternalEdit. Wrap sidebar/global edits with matched begin/end pairs.");
    }
    m_externalEditPage = resolved;
}

void SettingsController::endExternalEdit()
{
    m_externalEditPage.clear();
}

// ═══════════════════════════════════════════════════════════════════════════════
// PhosphorZones::Layout management (D-Bus to daemon, no KCM PhosphorZones::LayoutRegistry class needed)
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::scheduleLayoutLoad()
{
    m_layoutLoadTimer.start();
}

void SettingsController::loadLayoutsAsync()
{
    // Force-reload the in-process PhosphorZones::LayoutRegistry from disk before reading.
    // The LayoutManager's QFileSystemWatcher catches most disk changes,
    // but Qt's QFSW has known misses on cross-process atomic-rename
    // writes (the daemon writes layouts via QSaveFile, which creates a
    // new inode the watcher may not bind to in time). Belt-and-suspenders:
    // every D-Bus layout signal that triggers loadLayoutsAsync (layoutCreated
    // / layoutDeleted / layoutChanged / layoutPropertyChanged /
    // layoutListChanged — see the connect block in the ctor) ALSO forces
    // an explicit reload here, so the local-source preview path stays
    // strictly in sync with the daemon's view regardless of which file-
    // event path fires first.
    if (m_localLayoutManager) {
        m_localLayoutManager->loadLayouts();
    }

    // Step 1: instant paint from the in-process composite source is handled
    // by the ctor-wired PhosphorZones::LayoutRegistry::layoutsChanged lambda (see ~line 180
    // — it calls recalcLocalLayouts() + swaps m_layouts from localLayoutPreviews()
    // and emits layoutsChanged). loadLayouts() above triggers that signal
    // synchronously when the disk contents actually changed, so the instant-paint
    // path runs without a duplicate recalc/emit here.

    // Step 2: async D-Bus call to pick up daemon-side enrichment
    // (hasSystemOrigin / hiddenFromSelector / defaultOrder / allow-lists)
    // that the local composite can't know about. On reply the enriched
    // list replaces m_layouts; if the call errors we keep the local
    // previews from Step 1 visible rather than blanking the page.
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::LayoutRegistry), QStringLiteral("getLayoutList"));

    // Gate the local-path layoutsChanged emit (see the ctor-wired lambda
    // on PhosphorZones::LayoutRegistry::layoutsChanged). The reply lambda clears this
    // unconditionally so any subsequent local-only refresh (daemon down)
    // emits as usual.
    m_awaitingDaemonLayouts = true;
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        // Clear the gate first so any local-path emit that arrives after
        // an error reply (or after a successful one) runs normally.
        m_awaitingDaemonLayouts = false;

        QDBusPendingReply<QStringList> reply = *w;
        if (reply.isError()) {
            qCWarning(lcCore) << "Failed to load layouts (D-Bus):" << reply.error().message()
                              << "— keeping local manual-layout previews from Step 1.";
            return;
        }

        QVariantList newLayouts;
        const QStringList layoutJsonList = reply.value();
        for (const QString& layoutJson : layoutJsonList) {
            QJsonDocument doc = QJsonDocument::fromJson(layoutJson.toUtf8());
            if (!doc.isNull() && doc.isObject()) {
                newLayouts.append(doc.object().toVariantMap());
            }
        }

        sortMergedLayoutList(newLayouts);
        m_layouts = newLayouts;
        Q_EMIT layoutsChanged();

        // Emit pending select after model is populated
        if (!m_pendingSelectLayoutId.isEmpty()) {
            QString id = m_pendingSelectLayoutId;
            m_pendingSelectLayoutId.clear();
            Q_EMIT layoutAdded(id);
        }
    });
}

// ── Daemon-independent layout previews (PhosphorZones::ILayoutSource) ───────
// See header doc for why these exist. Both helpers route through the shared
// toVariantMap so settings + editor + future consumers emit the
// same QML-compatible shape (drop-in replacement for the legacy m_layouts
// produced by LayoutAdaptor::getLayoutList).

QVariantList SettingsController::localLayoutPreviews() const
{
    QVariantList list;
    if (!m_localSources.composite()) {
        return list;
    }
    const auto previews = m_localSources.composite()->availableLayouts();
    list.reserve(previews.size());
    for (const auto& preview : previews) {
        list.append(toVariantMap(preview));
    }
    return list;
}

void SettingsController::recalcLocalLayouts()
{
    if (!m_localLayoutManager) {
        return;
    }
    QScreen* primary = Utils::primaryScreen();
    if (!primary) {
        return;
    }
    for (PhosphorZones::Layout* layout : m_localLayoutManager->layouts()) {
        if (!layout) {
            continue;
        }
        // Settings app is a separate process without a daemon ScreenManager — pass
        // nullptr and accept the Qt-availableGeometry fallback (this preview code
        // path doesn't need VS-aware sub-regions).
        LayoutComputeService::recalculateSync(layout, GeometryUtils::effectiveScreenGeometry(nullptr, layout, primary));
    }
}

QVariantMap SettingsController::localLayoutPreview(const QString& id, int windowCount)
{
    if (id.isEmpty() || !m_localSources.composite()) {
        return {};
    }
    const auto preview = m_localSources.composite()->previewAt(id, windowCount);
    if (preview.id.isEmpty()) {
        return {};
    }
    return toVariantMap(preview);
}

void SettingsController::createNewLayout()
{
    createNewLayout(PzI18n::tr("New Layout"), QStringLiteral("custom"), -1, true);
}

bool SettingsController::createNewLayout(const QString& name, const QString& type, int aspectRatioClass,
                                         bool openInEditor)
{
    QString sanitizedName = name.trimmed();
    if (sanitizedName.isEmpty())
        sanitizedName = PzI18n::tr("New Layout");

    const QString layoutType = type.isEmpty() ? QStringLiteral("custom") : type;

    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("createLayout"), {sanitizedName, layoutType});

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString newLayoutId = reply.arguments().first().toString();
        if (!newLayoutId.isEmpty()) {
            if (aspectRatioClass >= 0) {
                QDBusMessage arReply = DaemonDBus::callDaemon(
                    QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                    QStringLiteral("setLayoutAspectRatioClass"), {newLayoutId, aspectRatioClass});
                if (arReply.type() == QDBusMessage::ErrorMessage) {
                    qCWarning(lcCore) << "setLayoutAspectRatioClass failed:" << arReply.errorMessage();
                }
            }
            if (openInEditor) {
                editLayout(newLayoutId);
            }
            m_pendingSelectLayoutId = newLayoutId;
            scheduleLayoutLoad();
            return true;
        }
        // Daemon returned a reply but with an empty layout ID
        Q_EMIT layoutOperationFailed(PzI18n::tr("Could not create layout — daemon returned an empty layout ID."));
        scheduleLayoutLoad();
        return false;
    }
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "createNewLayout failed:" << reply.errorMessage();
        Q_EMIT layoutOperationFailed(reply.errorMessage());
    } else {
        Q_EMIT layoutOperationFailed(PzI18n::tr("Could not create layout — the daemon may not be running."));
    }
    // Still refresh — the daemon may have partially processed the request
    scheduleLayoutLoad();
    return false;
}

void SettingsController::deleteLayout(const QString& layoutId)
{
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("deleteLayout"), {layoutId});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "deleteLayout failed:" << reply.errorMessage();
        Q_EMIT layoutOperationFailed(PzI18n::tr("Could not delete layout: %1").arg(reply.errorMessage()));
    }
    scheduleLayoutLoad();
}

void SettingsController::duplicateLayout(const QString& layoutId)
{
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("duplicateLayout"), {layoutId});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString newId = reply.arguments().first().toString();
        if (!newId.isEmpty()) {
            m_pendingSelectLayoutId = newId;
        }
    } else if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "duplicateLayout failed:" << reply.errorMessage();
        Q_EMIT layoutOperationFailed(PzI18n::tr("Could not duplicate layout: %1").arg(reply.errorMessage()));
    }
    scheduleLayoutLoad();
}

QVariantMap SettingsController::physicalScreenResolution(const QString& screenId) const
{
    QVariantMap result;
    QScreen* screen = Phosphor::Screens::ScreenIdentity::findByIdOrName(screenId);
    if (screen) {
        result[QStringLiteral("width")] = screen->geometry().width();
        result[QStringLiteral("height")] = screen->geometry().height();
    }
    return result;
}

void SettingsController::editLayout(const QString& layoutId)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::LayoutRegistry), QStringLiteral("openEditorForLayoutOnScreen"));
    msg << layoutId << QString();
    QDBusConnection::sessionBus().asyncCall(msg);
}

void SettingsController::editLayoutOnScreen(const QString& layoutId, const QString& screenId)
{
    if (layoutId.isEmpty() || screenId.isEmpty())
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::LayoutRegistry), QStringLiteral("openEditorForLayoutOnScreen"));
    msg << layoutId << screenId;
    QDBusConnection::sessionBus().asyncCall(msg);
}

void SettingsController::openLayoutsFolder()
{
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/plasmazones/layouts");
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void SettingsController::importLayout(const QString& filePath)
{
    if (filePath.isEmpty())
        return;
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("importLayout"), {filePath});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString newLayoutId = reply.arguments().first().toString();
        if (!newLayoutId.isEmpty()) {
            m_pendingSelectLayoutId = newLayoutId;
        }
    }
    scheduleLayoutLoad();
}

void SettingsController::exportLayout(const QString& layoutId, const QString& filePath)
{
    if (layoutId.isEmpty() || filePath.isEmpty())
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::LayoutRegistry), QStringLiteral("exportLayout"));
    msg << layoutId << filePath;
    QDBusConnection::sessionBus().asyncCall(msg);
}

void SettingsController::setLayoutHidden(const QString& layoutId, bool hidden)
{
    if (layoutId.isEmpty())
        return;
    DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                           QStringLiteral("setLayoutHidden"), {layoutId, hidden});
    scheduleLayoutLoad();
}

void SettingsController::setLayoutAutoAssign(const QString& layoutId, bool enabled)
{
    if (layoutId.isEmpty())
        return;
    DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                           QStringLiteral("setLayoutAutoAssign"), {layoutId, enabled});
    scheduleLayoutLoad();
}

void SettingsController::setLayoutAspectRatio(const QString& layoutId, int aspectRatioClass)
{
    if (layoutId.isEmpty())
        return;
    DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                           QStringLiteral("setLayoutAspectRatioClass"), {layoutId, aspectRatioClass});
    scheduleLayoutLoad();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Assignment helpers (D-Bus to daemon PhosphorZones::LayoutRegistry)
// ═══════════════════════════════════════════════════════════════════════════════

QStringList SettingsController::fontStylesForFamily(const QString& family) const
{
    return QFontDatabase::styles(family);
}

int SettingsController::fontStyleWeight(const QString& family, const QString& style) const
{
    return QFontDatabase::weight(family, style);
}

bool SettingsController::fontStyleItalic(const QString& family, const QString& style) const
{
    return QFontDatabase::italic(family, style);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Assignment staging / mutations — QML-callable methods forward to StagingService
// and flip the dirty flag; all state + flush logic lives in the service.
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::assignLayoutToScreen(const QString& screenName, const QString& layoutId)
{
    m_staging.stageSnapping(screenName, 0, QString(), layoutId);
    setNeedsSave(true);
}

void SettingsController::assignLayoutToScreenDesktop(const QString& screenName, int virtualDesktop,
                                                     const QString& layoutId)
{
    m_staging.stageSnapping(screenName, virtualDesktop, QString(), layoutId);
    setNeedsSave(true);
}

void SettingsController::assignLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                                      const QString& layoutId)
{
    m_staging.stageSnapping(screenName, 0, activityId, layoutId);
    setNeedsSave(true);
}

void SettingsController::assignTilingLayoutToScreen(const QString& screenName, const QString& layoutId)
{
    m_staging.stageTiling(screenName, 0, QString(), layoutId);
    setNeedsSave(true);
}

void SettingsController::assignTilingLayoutToScreenDesktop(const QString& screenName, int virtualDesktop,
                                                           const QString& layoutId)
{
    m_staging.stageTiling(screenName, virtualDesktop, QString(), layoutId);
    setNeedsSave(true);
}

void SettingsController::assignTilingLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                                            const QString& layoutId)
{
    m_staging.stageTiling(screenName, 0, activityId, layoutId);
    setNeedsSave(true);
}

void SettingsController::clearScreenAssignment(const QString& screenName)
{
    m_staging.stageFullClear(screenName, 0, QString());
    setNeedsSave(true);
}

void SettingsController::clearTilingScreenAssignment(const QString& screenName)
{
    m_staging.stageTilingClear(screenName, 0, QString());
    setNeedsSave(true);
}

void SettingsController::clearScreenDesktopAssignment(const QString& screenName, int virtualDesktop)
{
    m_staging.stageFullClear(screenName, virtualDesktop, QString());
    setNeedsSave(true);
}

void SettingsController::clearScreenActivityAssignment(const QString& screenName, const QString& activityId)
{
    m_staging.stageFullClear(screenName, 0, activityId);
    setNeedsSave(true);
}

void SettingsController::clearTilingScreenDesktopAssignment(const QString& screenName, int virtualDesktop)
{
    m_staging.stageTilingClear(screenName, virtualDesktop, QString());
    setNeedsSave(true);
}

void SettingsController::clearTilingScreenActivityAssignment(const QString& screenName, const QString& activityId)
{
    m_staging.stageTilingClear(screenName, 0, activityId);
    setNeedsSave(true);
}

void SettingsController::stageAssignmentEntry(const QString& screenName, int virtualDesktop, const QString& activityId,
                                              int mode, const QString& snappingLayoutId,
                                              const QString& tilingAlgorithmId)
{
    m_staging.stageAssignmentEntry(screenName, virtualDesktop, activityId, mode, snappingLayoutId, tilingAlgorithmId);
    setNeedsSave(true);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Assignment query helpers (check staged state, then fall back to D-Bus)
// ═══════════════════════════════════════════════════════════════════════════════

QString SettingsController::getLayoutForScreen(const QString& screenName) const
{
    QString staged;
    if (m_staging.stagedSnappingLayout(screenName, 0, QString(), staged))
        return staged;
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("getLayoutForScreen"), {screenName});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

QString SettingsController::getTilingLayoutForScreen(const QString& screenName) const
{
    QString staged;
    if (m_staging.stagedTilingLayout(screenName, 0, QString(), staged))
        return staged;
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("getTilingAlgorithmForScreenDesktop"), {screenName, 0});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

QString SettingsController::getLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    QString staged;
    if (m_staging.stagedSnappingLayout(screenName, virtualDesktop, QString(), staged))
        return staged;
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                               QStringLiteral("getLayoutForScreenDesktop"), {screenName, virtualDesktop});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

QString SettingsController::getSnappingLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    QString staged;
    if (m_staging.stagedSnappingLayout(screenName, virtualDesktop, QString(), staged))
        return staged;
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                               QStringLiteral("getSnappingLayoutForScreenDesktop"), {screenName, virtualDesktop});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

bool SettingsController::hasExplicitAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    QString snap, tile;
    bool hasSnap = m_staging.stagedSnappingLayout(screenName, virtualDesktop, QString(), snap);
    bool hasTile = m_staging.stagedTilingLayout(screenName, virtualDesktop, QString(), tile);
    if (hasSnap || hasTile) {
        // If either staged field is non-empty, we definitely have an assignment
        if (!snap.isEmpty() || !tile.isEmpty())
            return true;
        // Only short-circuit to false when BOTH fields are staged and empty;
        // otherwise fall through to D-Bus so the daemon's other field is checked.
        if (hasSnap && hasTile)
            return false;
    }
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                               QStringLiteral("hasExplicitAssignmentForScreenDesktop"), {screenName, virtualDesktop});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toBool();
    return false;
}

QString SettingsController::getTilingLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    QString staged;
    if (m_staging.stagedTilingLayout(screenName, virtualDesktop, QString(), staged))
        return staged;
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                               QStringLiteral("getTilingAlgorithmForScreenDesktop"), {screenName, virtualDesktop});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString algo = reply.arguments().first().toString();
        if (!algo.isEmpty())
            return PhosphorLayout::LayoutId::makeAutotileId(algo);
    }
    return {};
}

bool SettingsController::hasExplicitTilingAssignmentForScreenDesktop(const QString& screenName,
                                                                     int virtualDesktop) const
{
    QString staged;
    if (m_staging.stagedTilingLayout(screenName, virtualDesktop, QString(), staged))
        return !staged.isEmpty();
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                               QStringLiteral("getTilingAlgorithmForScreenDesktop"), {screenName, virtualDesktop});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return !reply.arguments().first().toString().isEmpty();
    return false;
}

QString SettingsController::getLayoutForScreenActivity(const QString& screenName, const QString& activityId) const
{
    QString staged;
    if (m_staging.stagedSnappingLayout(screenName, 0, activityId, staged))
        return staged;
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("getLayoutForScreenActivity"), {screenName, activityId});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

QString SettingsController::getSnappingLayoutForScreenActivity(const QString& screenName,
                                                               const QString& activityId) const
{
    QString staged;
    if (m_staging.stagedSnappingLayout(screenName, 0, activityId, staged))
        return staged;
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("getLayoutForScreenActivity"), {screenName, activityId});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString layoutId = reply.arguments().first().toString();
        if (!layoutId.isEmpty() && !PhosphorLayout::LayoutId::isAutotile(layoutId))
            return layoutId;
    }
    return {};
}

bool SettingsController::hasExplicitAssignmentForScreenActivity(const QString& screenName,
                                                                const QString& activityId) const
{
    QString snap, tile;
    bool hasSnap = m_staging.stagedSnappingLayout(screenName, 0, activityId, snap);
    bool hasTile = m_staging.stagedTilingLayout(screenName, 0, activityId, tile);
    if (hasSnap || hasTile) {
        if (!snap.isEmpty() || !tile.isEmpty())
            return true;
        if (hasSnap && hasTile)
            return false;
    }
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                               QStringLiteral("hasExplicitAssignmentForScreenActivity"), {screenName, activityId});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toBool();
    return false;
}

QString SettingsController::getTilingLayoutForScreenActivity(const QString& screenName, const QString& activityId) const
{
    QString staged;
    if (m_staging.stagedTilingLayout(screenName, 0, activityId, staged))
        return staged;
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("getLayoutForScreenActivity"), {screenName, activityId});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString layoutId = reply.arguments().first().toString();
        if (PhosphorLayout::LayoutId::isAutotile(layoutId))
            return layoutId;
    }
    return {};
}

bool SettingsController::hasExplicitTilingAssignmentForScreenActivity(const QString& screenName,
                                                                      const QString& activityId) const
{
    QString tiling = getTilingLayoutForScreenActivity(screenName, activityId);
    return !tiling.isEmpty();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Quick layout slots (D-Bus to daemon)
// ═══════════════════════════════════════════════════════════════════════════════

QString SettingsController::getQuickLayoutSlot(int slotNumber) const
{
    if (slotNumber < 1 || slotNumber > 9)
        return {};
    QString staged;
    if (m_staging.stagedSnappingQuickSlot(slotNumber, staged))
        return staged;
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("getQuickLayoutSlot"), {slotNumber});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

void SettingsController::setQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    if (slotNumber < 1 || slotNumber > 9)
        return;
    m_staging.stageSnappingQuickSlot(slotNumber, layoutId);
    setNeedsSave(true);
}

QString SettingsController::getQuickLayoutShortcut(int slotNumber) const
{
    if (slotNumber < 1 || slotNumber > 9)
        return {};
    // Return the default shortcut string -- the standalone cannot query KGlobalAccel
    // since it doesn't link KF6::GlobalAccel. The shortcut is Meta+Alt+N.
    return QStringLiteral("Meta+Alt+%1").arg(slotNumber);
}

QString SettingsController::getTilingQuickLayoutSlot(int slotNumber) const
{
    if (slotNumber < 1 || slotNumber > 9)
        return {};
    QString staged;
    if (m_staging.stagedTilingQuickSlot(slotNumber, staged))
        return staged;
    return m_settings.readTilingQuickLayoutSlot(slotNumber);
}

void SettingsController::setTilingQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    if (slotNumber < 1 || slotNumber > 9)
        return;
    m_staging.stageTilingQuickSlot(slotNumber, layoutId);
    setNeedsSave(true);
}

// ═══════════════════════════════════════════════════════════════════════════════
// App-to-zone rules (D-Bus to daemon, reading from layout JSON)
// ═══════════════════════════════════════════════════════════════════════════════

QVariantList SettingsController::getAppRulesForLayout(const QString& layoutId) const
{
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("getLayout"), {layoutId});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return {};

    QString json = reply.arguments().first().toString();
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (doc.isNull() || !doc.isObject())
        return {};

    QJsonArray rulesArray = doc.object()[QLatin1String("appRules")].toArray();
    QVariantList result;
    for (const auto& ruleVal : rulesArray) {
        QJsonObject ruleObj = ruleVal.toObject();
        QVariantMap rule;
        rule[QStringLiteral("pattern")] = ruleObj[QLatin1String("pattern")].toString();
        rule[QStringLiteral("zoneNumber")] = ruleObj[QLatin1String("zoneNumber")].toInt();
        if (ruleObj.contains(QLatin1String("targetScreen")))
            rule[QStringLiteral("targetScreen")] = ruleObj[QLatin1String("targetScreen")].toString();
        result.append(rule);
    }
    return result;
}

void SettingsController::addAppRuleToLayout(const QString& layoutId, const QString& pattern, int zoneNumber,
                                            const QString& targetScreen)
{
    QString trimmed = pattern.trimmed();
    if (trimmed.isEmpty() || zoneNumber < 1)
        return;

    QVariantList rules = getAppRulesForLayout(layoutId);

    // Check for duplicates
    for (const auto& ruleVar : rules) {
        QVariantMap existing = ruleVar.toMap();
        if (existing[QStringLiteral("pattern")].toString().compare(trimmed, Qt::CaseInsensitive) == 0
            && existing[QStringLiteral("targetScreen")].toString() == targetScreen) {
            return;
        }
    }

    QVariantMap newRule;
    newRule[QStringLiteral("pattern")] = trimmed;
    newRule[QStringLiteral("zoneNumber")] = zoneNumber;
    if (!targetScreen.isEmpty())
        newRule[QStringLiteral("targetScreen")] = targetScreen;
    rules.append(newRule);

    // Save via updateLayout D-Bus
    saveAppRulesToDaemon(layoutId, rules);
}

void SettingsController::removeAppRuleFromLayout(const QString& layoutId, int index)
{
    QVariantList rules = getAppRulesForLayout(layoutId);
    if (index < 0 || index >= rules.size())
        return;
    rules.removeAt(index);
    saveAppRulesToDaemon(layoutId, rules);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Assignment lock helpers
// ═══════════════════════════════════════════════════════════════════════════════

static QString lockKey(const QString& screenName, int mode)
{
    // Resolve connector names (e.g., "DP-3") to EDID-based screen IDs
    // to match the daemon's lock key format
    QString resolved = Phosphor::Screens::ScreenIdentity::idForName(screenName);
    return QString::number(mode) + QStringLiteral(":") + resolved;
}

bool SettingsController::isScreenLocked(const QString& screenName, int mode) const
{
    return m_settings.isScreenLocked(lockKey(screenName, mode));
}

void SettingsController::toggleScreenLock(const QString& screenName, int mode)
{
    const QString key = lockKey(screenName, mode);
    m_settings.setScreenLocked(key, !m_settings.isScreenLocked(key));
    Q_EMIT lockedScreensChanged();
    setNeedsSave(true);
}

bool SettingsController::isContextLocked(const QString& screenName, int virtualDesktop, const QString& activity,
                                         int mode) const
{
    return m_settings.isContextLocked(lockKey(screenName, mode), virtualDesktop, activity);
}

void SettingsController::toggleContextLock(const QString& screenName, int virtualDesktop, const QString& activity,
                                           int mode)
{
    const QString key = lockKey(screenName, mode);
    bool locked = m_settings.isContextLocked(key, virtualDesktop, activity);
    m_settings.setContextLocked(key, virtualDesktop, activity, !locked);
    Q_EMIT lockedScreensChanged();
    setNeedsSave(true);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Screen helpers
// ═══════════════════════════════════════════════════════════════════════════════

// Convert the QML-side `viewMode` int to PhosphorZones::AssignmentEntry::Mode.
// The numeric values match by design (0 = Snapping, 1 = Autotile) but routing
// every call through the helper keeps the cast explicit and gives us a single
// place to add range-clamping if a future mode is introduced.
//
// SharedBridge.qml sets `assignmentViewMode: -1` as a sentinel that the
// SnappingBridge / TilingBridge subclass MUST override. If a future bridge
// subclass forgets the override, every disable read/write would silently
// land on the snapping list — exactly the mode confusion this whole
// machinery is meant to eliminate. Warn loudly so the bug is caught the
// first time a developer tests the affected page.
static PhosphorZones::AssignmentEntry::Mode modeFromViewMode(int viewMode)
{
    if (viewMode != static_cast<int>(PhosphorZones::AssignmentEntry::Snapping)
        && viewMode != static_cast<int>(PhosphorZones::AssignmentEntry::Autotile)) {
        qCWarning(PlasmaZones::lcCore)
            << "modeFromViewMode: unexpected viewMode" << viewMode
            << "— defaulting to Snapping. A bridge subclass likely forgot to set assignmentViewMode.";
        Q_ASSERT(false && "modeFromViewMode received a value outside {Snapping=0, Autotile=1}");
    }
    return viewMode == static_cast<int>(PhosphorZones::AssignmentEntry::Autotile)
        ? PhosphorZones::AssignmentEntry::Autotile
        : PhosphorZones::AssignmentEntry::Snapping;
}

bool SettingsController::isMonitorDisabled(int viewMode, const QString& screenName) const
{
    return m_screenHelper.isMonitorDisabled(modeFromViewMode(viewMode), screenName);
}

void SettingsController::setMonitorDisabled(int viewMode, const QString& screenName, bool disabled)
{
    m_screenHelper.setMonitorDisabled(modeFromViewMode(viewMode), screenName, disabled);
}

bool SettingsController::isDesktopDisabled(int viewMode, const QString& screenName, int desktop) const
{
    return m_settings.isDesktopDisabled(modeFromViewMode(viewMode), screenName, desktop);
}

void SettingsController::setDesktopDisabled(int viewMode, const QString& screenName, int desktop, bool disabled)
{
    // The disabledDesktopsChanged(viewMode) signal is fired by the
    // m_settings → controller forward in the constructor — no manual emit
    // needed here.
    const auto mode = modeFromViewMode(viewMode);
    QString key = screenName + QLatin1Char('/') + QString::number(desktop);
    QStringList entries = m_settings.disabledDesktops(mode);
    if (disabled && !entries.contains(key)) {
        entries.append(key);
        m_settings.setDisabledDesktops(mode, entries);
        setNeedsSave(true);
    } else if (!disabled && entries.removeAll(key) > 0) {
        m_settings.setDisabledDesktops(mode, entries);
        setNeedsSave(true);
    }
}

bool SettingsController::isActivityDisabled(int viewMode, const QString& screenName, const QString& activityId) const
{
    return m_settings.isActivityDisabled(modeFromViewMode(viewMode), screenName, activityId);
}

void SettingsController::setActivityDisabled(int viewMode, const QString& screenName, const QString& activityId,
                                             bool disabled)
{
    // See setDesktopDisabled — the per-mode signal is forwarded from
    // m_settings via the connect set up in the constructor.
    const auto mode = modeFromViewMode(viewMode);
    QString key = screenName + QLatin1Char('/') + activityId;
    QStringList entries = m_settings.disabledActivities(mode);
    if (disabled && !entries.contains(key)) {
        entries.append(key);
        m_settings.setDisabledActivities(mode, entries);
        setNeedsSave(true);
    } else if (!disabled && entries.removeAll(key) > 0) {
        m_settings.setDisabledActivities(mode, entries);
        setNeedsSave(true);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual desktops / activities (D-Bus queries to daemon)
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::refreshVirtualDesktops()
{
    QDBusMessage countReply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                     QStringLiteral("getVirtualDesktopCount"));
    if (countReply.type() == QDBusMessage::ReplyMessage && !countReply.arguments().isEmpty()) {
        m_virtualDesktopCount = countReply.arguments().first().toInt();
    }

    QDBusMessage namesReply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                     QStringLiteral("getVirtualDesktopNames"));
    if (namesReply.type() == QDBusMessage::ReplyMessage && !namesReply.arguments().isEmpty()) {
        m_virtualDesktopNames = namesReply.arguments().first().toStringList();
    }
}

void SettingsController::refreshActivities()
{
    QDBusMessage availReply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                     QStringLiteral("isActivitiesAvailable"));
    if (availReply.type() == QDBusMessage::ReplyMessage && !availReply.arguments().isEmpty()) {
        m_activitiesAvailable = availReply.arguments().first().toBool();
    }

    if (m_activitiesAvailable) {
        QDBusMessage infoReply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                        QStringLiteral("getAllActivitiesInfo"));
        if (infoReply.type() == QDBusMessage::ReplyMessage && !infoReply.arguments().isEmpty()) {
            QString json = infoReply.arguments().first().toString();
            QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
            if (doc.isArray()) {
                m_activities.clear();
                for (const auto& val : doc.array()) {
                    m_activities.append(val.toObject().toVariantMap());
                }
            }
        }

        QDBusMessage currentReply = DaemonDBus::callDaemon(
            QString(PhosphorProtocol::Service::Interface::LayoutRegistry), QStringLiteral("getCurrentActivity"));
        if (currentReply.type() == QDBusMessage::ReplyMessage && !currentReply.arguments().isEmpty()) {
            m_currentActivity = currentReply.arguments().first().toString();
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual desktop / activity D-Bus signal handlers
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::onVirtualDesktopsChanged()
{
    refreshVirtualDesktops();

    // Prune both per-mode disabled-desktop lists — see start.cpp comment re:
    // mid-range renumbering limitation. The per-mode disabledDesktopsChanged
    // signal is forwarded from m_settings (see ctor); no manual emit here.
    for (const auto mode : {PhosphorZones::AssignmentEntry::Snapping, PhosphorZones::AssignmentEntry::Autotile}) {
        QStringList disabled = m_settings.disabledDesktops(mode);
        if (pruneDisabledDesktopEntries(disabled, m_virtualDesktopCount)) {
            m_settings.setDisabledDesktops(mode, disabled);
            setNeedsSave(true);
        }
    }

    Q_EMIT virtualDesktopsChanged();
}

void SettingsController::onActivitiesChanged()
{
    refreshActivities();

    // Prune disabled-activity entries that reference removed activities
    if (!m_activities.isEmpty()) {
        QSet<QString> validIds;
        for (const QVariant& v : std::as_const(m_activities)) {
            const QVariantMap map = v.toMap();
            const QString id = map.value(QStringLiteral("id")).toString();
            if (!id.isEmpty()) {
                validIds.insert(id);
            }
        }
        // Per-mode signal forwarded from m_settings (see ctor) — no manual emit.
        for (const auto mode : {PhosphorZones::AssignmentEntry::Snapping, PhosphorZones::AssignmentEntry::Autotile}) {
            QStringList disabledActs = m_settings.disabledActivities(mode);
            if (pruneDisabledActivityEntries(disabledActs, validIds)) {
                m_settings.setDisabledActivities(mode, disabledActs);
                setNeedsSave(true);
            }
        }
    }

    Q_EMIT activitiesChanged();
}

void SettingsController::onScreenLayoutChanged(const QString& screenId, const QString& layoutId, int virtualDesktop)
{
    Q_UNUSED(screenId)
    Q_UNUSED(layoutId)
    Q_UNUSED(virtualDesktop)
    // External assignment change (hotkey, script, toggle) — refresh overview
    Q_EMIT screenLayoutChanged();
}

// Parses the daemon's running-windows JSON payload into a QVariantList of
// {windowClass, appName, caption} maps ready for QML consumption. The
// synchronous getRunningWindows() predecessor was removed in Phase 6 of
// refactor/dbus-performance; only onRunningWindowsAvailable calls this now.
static QVariantList parseRunningWindowsJson(const QString& json)
{
    if (json.isEmpty()) {
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        return {};
    }

    QVariantList result;
    const QJsonArray array = doc.array();
    result.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        QVariantMap item;
        item[QStringLiteral("windowClass")] = obj[QLatin1String("windowClass")].toString();
        item[QStringLiteral("appName")] = obj[QLatin1String("appName")].toString();
        item[QStringLiteral("caption")] = obj[QLatin1String("caption")].toString();
        result.append(item);
    }
    return result;
}

void SettingsController::requestRunningWindows()
{
    // Fire-and-forget: the daemon emits runningWindowsRequested to the
    // KWin effect, which answers via provideRunningWindows, which the
    // daemon fans out on runningWindowsAvailable — caught by our
    // onRunningWindowsAvailable slot. The UI thread never blocks.
    //
    // Start (or restart) the client-side timeout guard. Repeated calls
    // coalesce — the most recent deadline wins, matching the fire-and-
    // forget semantics on the daemon side.
    m_runningWindowsTimeout.start();
    DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Settings),
                           QStringLiteral("requestRunningWindows"));
}

void SettingsController::onRunningWindowsAvailable(const QString& json)
{
    // Reply arrived — stop the timeout timer so a stale runningWindowsTimedOut()
    // doesn't fire after we've already served fresh data.
    m_runningWindowsTimeout.stop();
    m_cachedRunningWindows = parseRunningWindowsJson(json);
    Q_EMIT runningWindowsAvailable(m_cachedRunningWindows);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Config export/import
// ═══════════════════════════════════════════════════════════════════════════════

bool SettingsController::exportAllSettings(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return false;
    }
    // Flush current in-memory settings to disk so the exported file reflects
    // the actual current state, not the last-saved snapshot.
    m_settings.save();
    const QString configPath = PlasmaZones::ConfigDefaults::configFilePath();
    if (!QFile::exists(configPath)) {
        qCWarning(PlasmaZones::lcCore) << "Config file not found:" << configPath;
        return false;
    }
    // Remove destination if it exists (QFile::copy won't overwrite)
    if (QFile::exists(filePath)) {
        QFile::remove(filePath);
    }
    bool ok = QFile::copy(configPath, filePath);
    if (!ok) {
        qCWarning(PlasmaZones::lcCore) << "Failed to export settings to:" << filePath;
    }
    return ok;
}

bool SettingsController::importAllSettings(const QString& filePath)
{
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        return false;
    }

    const QString configPath = PlasmaZones::ConfigDefaults::configFilePath();

    // Detect if the imported file is legacy INI format (not JSON).
    // If so, run the migration converter to produce a JSON file.
    bool isLegacyIni = false;
    {
        QFile f(filePath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            // Read enough bytes to find the first non-whitespace character.
            // JSON files start with '{' (or '[' for arrays, though config is always an object).
            // Skip UTF-8 BOM (EF BB BF) if present — trimmed() only strips ASCII whitespace.
            QByteArray head = f.peek(256).trimmed();
            if (head.size() >= 3 && static_cast<unsigned char>(head.at(0)) == 0xEF
                && static_cast<unsigned char>(head.at(1)) == 0xBB && static_cast<unsigned char>(head.at(2)) == 0xBF) {
                head = head.mid(3).trimmed();
            }
            isLegacyIni = !head.isEmpty() && head.at(0) != '{';
        }
    }

    // Backup current config
    const QString backupPath = configPath + QStringLiteral(".bak");
    if (QFile::exists(backupPath)) {
        QFile::remove(backupPath);
    }
    if (QFile::exists(configPath) && !QFile::copy(configPath, backupPath)) {
        qCWarning(PlasmaZones::lcCore) << "Failed to backup config to:" << backupPath;
        return false;
    }

    bool ok = false;
    if (isLegacyIni) {
        // Convert INI to JSON in-place using the migration module
        if (QFile::exists(configPath)) {
            QFile::remove(configPath);
        }
        ok = PlasmaZones::ConfigMigration::migrateIniToJson(filePath, configPath);
        if (!ok) {
            qCWarning(PlasmaZones::lcCore) << "Failed to convert legacy INI file:" << filePath;
        }
    } else {
        // Validate JSON before overwriting current config
        QFile importFile(filePath);
        if (!importFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qCWarning(PlasmaZones::lcCore) << "Failed to open import file:" << filePath;
            ok = false;
        } else {
            QJsonParseError parseErr;
            QJsonDocument importDoc = QJsonDocument::fromJson(importFile.readAll(), &parseErr);
            if (parseErr.error != QJsonParseError::NoError || !importDoc.isObject()) {
                qCWarning(PlasmaZones::lcCore) << "Invalid JSON in import file:" << filePath << parseErr.errorString();
                ok = false;
            } else {
                // Valid JSON — copy to config path
                if (QFile::exists(configPath)) {
                    QFile::remove(configPath);
                }
                ok = QFile::copy(filePath, configPath);
                if (!ok) {
                    qCWarning(PlasmaZones::lcCore) << "Failed to import settings from:" << filePath;
                }
            }
        }
    }

    if (!ok) {
        // Restore backup on failure
        if (QFile::exists(backupPath)) {
            QFile::remove(configPath);
            QFile::rename(backupPath, configPath);
        }
    } else {
        // Clean up backup on success
        QFile::remove(backupPath);
        // Wrap the in-memory reload so property NOTIFY signals don't mark
        // pages dirty — the imported config is already on disk.
        m_loading = true;
        m_settings.load();
        m_loading = false;
        DaemonDBus::notifyReload();
        setNeedsSave(false);
    }
    return ok;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Screen state query
// ═══════════════════════════════════════════════════════════════════════════════

QVariantList SettingsController::getScreenStates() const
{
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                QStringLiteral("getScreenStates"));
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty())
        return {};

    const QString json = reply.arguments().at(0).toString();
    if (json.isEmpty())
        return {};

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray())
        return {};

    QVariantList result;
    for (const QJsonValue& value : doc.array()) {
        if (value.isObject())
            result.append(value.toObject().toVariantMap());
    }
    return result;
}

bool SettingsController::hasStagedAssignment(const QString& screenName, int virtualDesktop,
                                             const QString& activityId) const
{
    return m_staging.stagedAssignmentFor(screenName, virtualDesktop, activityId) != nullptr;
}

QVariantMap SettingsController::getStagedAssignment(const QString& screenName, int virtualDesktop,
                                                    const QString& activityId) const
{
    auto* s = m_staging.stagedAssignmentFor(screenName, virtualDesktop, activityId);
    if (!s)
        return {};
    QVariantMap map;
    if (s->snappingLayoutId.has_value())
        map[QStringLiteral("layoutId")] = *s->snappingLayoutId;
    if (s->tilingAlgorithmId.has_value()) {
        const QString& val = *s->tilingAlgorithmId;
        map[QStringLiteral("algorithmId")] =
            PhosphorLayout::LayoutId::isAutotile(val) ? PhosphorLayout::LayoutId::extractAlgorithmId(val) : val;
    }
    // Explicit mode takes priority (stageAssignmentEntry path)
    if (s->stagedMode.has_value()) {
        map[QStringLiteral("mode")] = *s->stagedMode;
    } else {
        // Infer mode from which fields are staged (per-field path)
        if (s->tilingAlgorithmId.has_value() && !s->tilingAlgorithmId->isEmpty())
            map[QStringLiteral("mode")] = 1;
        else if (s->snappingLayoutId.has_value() && !s->snappingLayoutId->isEmpty())
            map[QStringLiteral("mode")] = 0;
    }
    return map;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Algorithm helpers
// ═══════════════════════════════════════════════════════════════════════════════

// All bodies moved to AlgorithmService; SettingsController::* methods below
// are 1-line Q_INVOKABLE forwarders so QML's entry points stay stable.

QVariantList SettingsController::availableAlgorithms() const
{
    return m_algorithmService->availableAlgorithms();
}

QVariantList SettingsController::generateAlgorithmPreview(const QString& algorithmId, int windowCount,
                                                          double splitRatio, int masterCount) const
{
    return m_algorithmService->generateAlgorithmPreview(algorithmId, windowCount, splitRatio, masterCount);
}

QVariantList SettingsController::generateAlgorithmDefaultPreview(const QString& algorithmId) const
{
    return m_algorithmService->generateAlgorithmDefaultPreview(algorithmId);
}

void SettingsController::openAlgorithmsFolder()
{
    m_algorithmService->openAlgorithmsFolder();
}

bool SettingsController::importAlgorithm(const QString& filePath)
{
    return m_algorithmService->importAlgorithm(filePath);
}

QString SettingsController::algorithmIdFromLayoutId(const QString& layoutId)
{
    return PhosphorLayout::LayoutId::isAutotile(layoutId) ? PhosphorLayout::LayoutId::extractAlgorithmId(layoutId)
                                                          : layoutId;
}

void SettingsController::openAlgorithm(const QString& algorithmId)
{
    m_algorithmService->openAlgorithm(algorithmId);
}

void SettingsController::openLayoutFile(const QString& layoutId)
{
    m_algorithmService->openLayoutFile(layoutId);
}

bool SettingsController::deleteAlgorithm(const QString& algorithmId)
{
    return m_algorithmService->deleteAlgorithm(algorithmId);
}

bool SettingsController::duplicateAlgorithm(const QString& algorithmId)
{
    return m_algorithmService->duplicateAlgorithm(algorithmId);
}

bool SettingsController::exportAlgorithm(const QString& algorithmId, const QString& destPath)
{
    return m_algorithmService->exportAlgorithm(algorithmId, destPath);
}

QString SettingsController::createNewAlgorithm(const QString& name, const QString& baseTemplate,
                                               bool supportsMasterCount, bool supportsSplitRatio,
                                               bool producesOverlappingZones, bool supportsMemory)
{
    return m_algorithmService->createNewAlgorithm(name, baseTemplate, supportsMasterCount, supportsSplitRatio,
                                                  producesOverlappingZones, supportsMemory);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-screen autotile overrides
// ═══════════════════════════════════════════════════════════════════════════════

QVariantMap SettingsController::getPerScreenAutotileSettings(const QString& screenName) const
{
    return m_settings.getPerScreenAutotileSettings(screenName);
}

void SettingsController::setPerScreenAutotileSetting(const QString& screenName, const QString& key,
                                                     const QVariant& value)
{
    m_settings.setPerScreenAutotileSetting(screenName, key, value);
    setNeedsSave(true);
}

void SettingsController::clearPerScreenAutotileSettings(const QString& screenName)
{
    m_settings.clearPerScreenAutotileSettings(screenName);
    setNeedsSave(true);
}

bool SettingsController::hasPerScreenAutotileSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenAutotileSettings(screenName);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-screen snapping overrides
// ═══════════════════════════════════════════════════════════════════════════════

QVariantMap SettingsController::getPerScreenSnappingSettings(const QString& screenName) const
{
    return m_settings.getPerScreenSnappingSettings(screenName);
}

void SettingsController::setPerScreenSnappingSetting(const QString& screenName, const QString& key,
                                                     const QVariant& value)
{
    m_settings.setPerScreenSnappingSetting(screenName, key, value);
    setNeedsSave(true);
}

void SettingsController::clearPerScreenSnappingSettings(const QString& screenName)
{
    m_settings.clearPerScreenSnappingSettings(screenName);
    setNeedsSave(true);
}

bool SettingsController::hasPerScreenSnappingSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenSnappingSettings(screenName);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-screen zone selector overrides
// ═══════════════════════════════════════════════════════════════════════════════

QVariantMap SettingsController::getPerScreenZoneSelectorSettings(const QString& screenName) const
{
    return m_settings.getPerScreenZoneSelectorSettings(screenName);
}

void SettingsController::setPerScreenZoneSelectorSetting(const QString& screenName, const QString& key,
                                                         const QVariant& value)
{
    m_settings.setPerScreenZoneSelectorSetting(screenName, key, value);
    setNeedsSave(true);
}

void SettingsController::clearPerScreenZoneSelectorSettings(const QString& screenName)
{
    m_settings.clearPerScreenZoneSelectorSettings(screenName);
    setNeedsSave(true);
}

bool SettingsController::hasPerScreenZoneSelectorSettings(const QString& screenName) const
{
    return m_settings.hasPerScreenZoneSelectorSettings(screenName);
}

void SettingsController::saveAppRulesToDaemon(const QString& layoutId, const QVariantList& rules)
{
    // Get the current layout JSON, update the appRules field, and send back via updateLayout
    QDBusMessage getReply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                   QStringLiteral("getLayout"), {layoutId});
    if (getReply.type() != QDBusMessage::ReplyMessage || getReply.arguments().isEmpty())
        return;

    QJsonDocument doc = QJsonDocument::fromJson(getReply.arguments().first().toString().toUtf8());
    if (doc.isNull() || !doc.isObject())
        return;

    QJsonObject obj = doc.object();
    QJsonArray rulesArray;
    for (const auto& ruleVar : rules) {
        QVariantMap ruleMap = ruleVar.toMap();
        QJsonObject ruleObj;
        ruleObj[QLatin1String("pattern")] = ruleMap[QStringLiteral("pattern")].toString();
        ruleObj[QLatin1String("zoneNumber")] = ruleMap[QStringLiteral("zoneNumber")].toInt();
        if (ruleMap.contains(QStringLiteral("targetScreen")))
            ruleObj[QLatin1String("targetScreen")] = ruleMap[QStringLiteral("targetScreen")].toString();
        rulesArray.append(ruleObj);
    }
    obj[QLatin1String("appRules")] = rulesArray;

    QString updatedJson = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                           QStringLiteral("updateLayout"), {updatedJson});
    scheduleLayoutLoad();
}

QVariantMap SettingsController::loadWindowGeometry() const
{
    QSettings settings;
    QVariantMap geo;
    int w = settings.value(QStringLiteral("width"), 0).toInt();
    int h = settings.value(QStringLiteral("height"), 0).toInt();
    int x = settings.value(QStringLiteral("x")).toInt();
    int y = settings.value(QStringLiteral("y")).toInt();
    bool hasPosition = settings.contains(QStringLiteral("x"));

    // Validate against available screen geometry
    if (w > 0 && h > 0) {
        QRect virtualGeo;
        for (auto* screen : QGuiApplication::screens())
            virtualGeo = virtualGeo.united(screen->availableGeometry());
        if (!virtualGeo.isEmpty()) {
            w = qMin(w, virtualGeo.width());
            h = qMin(h, virtualGeo.height());
            // Check if center of saved window is on any screen
            if (hasPosition && !virtualGeo.contains(QPoint(x + w / 2, y + h / 2))) {
                hasPosition = false; // off-screen, let WM place it
            }
        }
    }

    geo[QStringLiteral("width")] = w;
    geo[QStringLiteral("height")] = h;
    geo[QStringLiteral("x")] = x;
    geo[QStringLiteral("y")] = y;
    geo[QStringLiteral("hasPosition")] = hasPosition;
    return geo;
}

void SettingsController::saveWindowGeometry(int x, int y, int width, int height)
{
    QSettings settings;
    settings.setValue(QStringLiteral("x"), x);
    settings.setValue(QStringLiteral("y"), y);
    settings.setValue(QStringLiteral("width"), width);
    settings.setValue(QStringLiteral("height"), height);
}

// ═══════════════════════════════════════════════════════════════════════════════
// KZones Import — thin wrappers around kzonesimporter.{h,cpp}
// ═══════════════════════════════════════════════════════════════════════════════

bool SettingsController::hasKZonesConfig()
{
    return KZonesImporter::hasKZonesConfig();
}

int SettingsController::importFromKZones()
{
    const auto result = KZonesImporter::importFromKwinrc();
    if (result.imported > 0) {
        m_pendingSelectLayoutId = result.pendingSelectLayoutId;
        scheduleLayoutLoad();
    }
    Q_EMIT kzonesImportFinished(result.imported, result.message);
    return result.imported;
}

int SettingsController::importFromKZonesFile(const QString& filePath)
{
    const auto result = KZonesImporter::importFromFile(filePath);
    if (result.imported > 0) {
        m_pendingSelectLayoutId = result.pendingSelectLayoutId;
        scheduleLayoutLoad();
    }
    Q_EMIT kzonesImportFinished(result.imported, result.message);
    return result.imported;
}

// ── Virtual screen configuration ──────────────────────────────────────────

QStringList SettingsController::getPhysicalScreens() const
{
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Screen),
                                                QStringLiteral("getPhysicalScreens"));
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        return reply.arguments().first().toStringList();
    }
    return {};
}

QVariantList SettingsController::getVirtualScreenConfig(const QString& physicalScreenId) const
{
    QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Screen),
                                                QStringLiteral("getVirtualScreenConfig"), {physicalScreenId});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString json = reply.arguments().first().toString();
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        if (doc.isObject()) {
            QJsonObject root = doc.object();
            QJsonArray screensArr = root.value(QLatin1String("screens")).toArray();
            QVariantList result;
            for (const auto& entry : screensArr) {
                QJsonObject screenObj = entry.toObject();
                QJsonObject regionObj = screenObj.value(QLatin1String("region")).toObject();
                QVariantMap screen;
                screen[QStringLiteral("displayName")] = screenObj.value(QLatin1String("displayName")).toString();
                screen[QStringLiteral("x")] = regionObj.value(::PhosphorZones::ZoneJsonKeys::X).toDouble();
                screen[QStringLiteral("y")] = regionObj.value(::PhosphorZones::ZoneJsonKeys::Y).toDouble();
                screen[QStringLiteral("width")] = regionObj.value(::PhosphorZones::ZoneJsonKeys::Width).toDouble();
                screen[QStringLiteral("height")] = regionObj.value(::PhosphorZones::ZoneJsonKeys::Height).toDouble();
                screen[QStringLiteral("index")] = screenObj.value(QLatin1String("index")).toInt();
                result.append(screen);
            }
            return result;
        }
    }
    return {};
}

void SettingsController::applyVirtualScreenConfig(const QString& physicalScreenId, const QVariantList& screens)
{
    QJsonObject root;
    root[QLatin1String("physicalScreenId")] = physicalScreenId;

    QJsonArray screensArr;
    for (int i = 0; i < screens.size(); ++i) {
        Phosphor::Screens::VirtualScreenDef def =
            VirtualScreenUtils::variantMapToVirtualScreenDef(screens[i].toMap(), physicalScreenId, i);
        if (!def.isValid()) {
            qCWarning(lcConfig) << "Skipping invalid virtual screen def for" << physicalScreenId << "index" << i
                                << "region:" << def.region;
            continue;
        }
        QJsonObject screenObj;
        screenObj[QLatin1String("index")] = def.index;
        screenObj[QLatin1String("displayName")] = def.displayName;
        screenObj[QLatin1String("region")] = QJsonObject{{::PhosphorZones::ZoneJsonKeys::X, def.region.x()},
                                                         {::PhosphorZones::ZoneJsonKeys::Y, def.region.y()},
                                                         {::PhosphorZones::ZoneJsonKeys::Width, def.region.width()},
                                                         {::PhosphorZones::ZoneJsonKeys::Height, def.region.height()}};
        screensArr.append(screenObj);
    }
    root[QLatin1String("screens")] = screensArr;

    QString json = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Screen),
                           QStringLiteral("setVirtualScreenConfig"), {physicalScreenId, json});
}

void SettingsController::removeVirtualScreenConfig(const QString& physicalScreenId)
{
    applyVirtualScreenConfig(physicalScreenId, {});
}

void SettingsController::stageVirtualScreenConfig(const QString& physicalScreenId, const QVariantList& screens)
{
    m_staging.stageVirtualScreenConfig(physicalScreenId, screens);
    setNeedsSave(true);
}

void SettingsController::stageVirtualScreenRemoval(const QString& physicalScreenId)
{
    m_staging.stageVirtualScreenRemoval(physicalScreenId);
    setNeedsSave(true);
}

bool SettingsController::hasUnsavedVirtualScreenConfig(const QString& physicalScreenId) const
{
    return m_staging.hasUnsavedVirtualScreenConfig(physicalScreenId);
}

QVariantList SettingsController::getStagedVirtualScreenConfig(const QString& physicalScreenId) const
{
    return m_staging.stagedVirtualScreenConfig(physicalScreenId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Ordering helpers
// ═══════════════════════════════════════════════════════════════════════════════

// Shared helper: apply custom order to a set of items, appending unordered items alphabetically
static QVariantList applyCustomOrder(const QStringList& customOrder, const QHash<QString, QVariantMap>& itemMap)
{
    QVariantList result;
    QSet<QString> added;

    // First: items in custom order (skip stale IDs)
    for (const QString& id : customOrder) {
        if (itemMap.contains(id)) {
            result.append(itemMap.value(id));
            added.insert(id);
        }
    }

    // Then: remaining items in default order (name-alphabetical)
    QVector<QPair<QString, QVariantMap>> remaining;
    for (auto it = itemMap.cbegin(); it != itemMap.cend(); ++it) {
        if (!added.contains(it.key())) {
            remaining.append({it.key(), it.value()});
        }
    }
    std::sort(remaining.begin(), remaining.end(), [](const auto& a, const auto& b) {
        return a.second.value(QStringLiteral("name"))
                   .toString()
                   .compare(b.second.value(QStringLiteral("name")).toString(), Qt::CaseInsensitive)
            < 0;
    });
    for (const auto& pair : remaining) {
        result.append(pair.second);
    }

    return result;
}

QStringList SettingsController::effectiveSnappingOrder() const
{
    return m_stagedSnappingOrder.value_or(m_settings.snappingLayoutOrder());
}

QStringList SettingsController::effectiveTilingOrder() const
{
    return m_stagedTilingOrder.value_or(m_settings.tilingAlgorithmOrder());
}

bool SettingsController::hasCustomSnappingOrder() const
{
    return !effectiveSnappingOrder().isEmpty();
}

bool SettingsController::hasCustomTilingOrder() const
{
    return !effectiveTilingOrder().isEmpty();
}

QVariantList SettingsController::resolvedSnappingOrder() const
{
    QHash<QString, QVariantMap> layoutMap;
    for (const QVariant& v : m_layouts) {
        QVariantMap map = v.toMap();
        QString id = map.value(QStringLiteral("id")).toString();
        if (!id.isEmpty() && !map.value(QStringLiteral("isAutotile"), false).toBool()) {
            layoutMap.insert(id, map);
        }
    }
    return applyCustomOrder(effectiveSnappingOrder(), layoutMap);
}

QVariantList SettingsController::resolvedTilingOrder() const
{
    QHash<QString, QVariantMap> algoMap;
    for (const QVariant& v : availableAlgorithms()) {
        QVariantMap map = v.toMap();
        QString id = map.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            algoMap.insert(id, map);
        }
    }
    return applyCustomOrder(effectiveTilingOrder(), algoMap);
}

// Shared helper: move an item within a resolved order list and stage the result
static bool moveOrderedItem(const QVariantList& resolved, int fromIndex, int toIndex,
                            std::optional<QStringList>& staged)
{
    if (fromIndex < 0 || fromIndex >= resolved.size() || toIndex < 0 || toIndex >= resolved.size()
        || fromIndex == toIndex) {
        return false;
    }

    QStringList ids;
    ids.reserve(resolved.size());
    for (const QVariant& v : resolved) {
        ids.append(v.toMap().value(QStringLiteral("id")).toString());
    }
    ids.move(fromIndex, toIndex);
    staged = ids;
    return true;
}

void SettingsController::moveSnappingLayout(int fromIndex, int toIndex)
{
    if (moveOrderedItem(resolvedSnappingOrder(), fromIndex, toIndex, m_stagedSnappingOrder)) {
        Q_EMIT stagedSnappingOrderChanged();
        setNeedsSave(true);
    }
}

void SettingsController::moveTilingAlgorithm(int fromIndex, int toIndex)
{
    if (moveOrderedItem(resolvedTilingOrder(), fromIndex, toIndex, m_stagedTilingOrder)) {
        Q_EMIT stagedTilingOrderChanged();
        setNeedsSave(true);
    }
}

void SettingsController::resetSnappingOrder()
{
    m_stagedSnappingOrder = QStringList{};
    Q_EMIT stagedSnappingOrderChanged();
    setNeedsSave(true);
}

void SettingsController::resetTilingOrder()
{
    m_stagedTilingOrder = QStringList{};
    Q_EMIT stagedTilingOrderChanged();
    setNeedsSave(true);
}

} // namespace PlasmaZones
