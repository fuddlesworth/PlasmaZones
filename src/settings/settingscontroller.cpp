// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingscontroller.h"
#include "../core/constants.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include "../core/virtualscreen.h"
#include "dbusutils.h"

#include "../autotile/AlgorithmRegistry.h"
#include "../autotile/TilingAlgorithm.h"
#include "../autotile/TilingState.h"
#include "../autotile/algorithms/ScriptedAlgorithm.h"
#include "../autotile/algorithms/ScriptedAlgorithmLoader.h"

#include "../config/configdefaults.h"
#include "../pz_i18n.h"

#include <QDBusMessage>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDesktopServices>
#include <QDate>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <memory>

namespace PlasmaZones {

namespace {

QString userAlgorithmsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/plasmazones/algorithms/");
}

QString findUniqueAlgorithmPath(const QString& dir, const QString& baseName)
{
    QString path = dir + baseName + QStringLiteral(".js");
    if (!QFile::exists(path))
        return path;
    for (int i = 1; i <= 999; ++i) {
        path = dir + baseName + QStringLiteral("-") + QString::number(i) + QStringLiteral(".js");
        if (!QFile::exists(path))
            return path;
    }
    return QString();
}
/// Convert a QVariantMap (from QML virtual screen editor) to a VirtualScreenDef.
/// Used by both the save path (staging → KConfig) and the D-Bus apply path.
VirtualScreenDef variantMapToVirtualScreenDef(const QVariantMap& map, const QString& physicalScreenId, int index)
{
    VirtualScreenDef def;
    def.physicalScreenId = physicalScreenId;
    def.index = index;
    def.displayName = map.value(QStringLiteral("displayName")).toString();
    def.region = QRectF(map.value(QStringLiteral("x")).toDouble(), map.value(QStringLiteral("y")).toDouble(),
                        map.value(QStringLiteral("width")).toDouble(), map.value(QStringLiteral("height")).toDouble());
    def.id = VirtualScreenId::make(physicalScreenId, index);
    return def;
}
} // anonymous namespace

SettingsController::~SettingsController()
{
    // Unregister D-Bus service so a stale name doesn't linger if the
    // QDBusConnection outlives this object.
    auto bus = QDBusConnection::sessionBus();
    bus.unregisterObject(DBus::SettingsApp::ObjectPath);
    bus.unregisterService(DBus::SettingsApp::ServiceName);

    // Disconnect all pending algorithm registration watchers — AlgorithmRegistry
    // is a singleton that outlives this object, so dangling connections would fire
    // into a destroyed SettingsController.
    for (auto it = m_algorithmWatchers.begin(); it != m_algorithmWatchers.end(); ++it) {
        const auto& connPtr = it.value();
        if (connPtr && *connPtr)
            disconnect(*connPtr);
    }
    m_algorithmWatchers.clear();
}

SettingsController::SettingsController(QObject* parent)
    : QObject(parent)
    , m_screenHelper(&m_settings, this)
{
    // Translate rendering backend display names once at construction
    for (const auto& name : PlasmaZones::ConfigDefaults::renderingBackendDisplayNames())
        m_renderingBackendDisplayNames.append(PzI18n::tr(name.toUtf8().constData()));

    // Snapshot current backend so the QML "restart required" message survives page recreation
    m_startupRenderingBackend = m_settings.renderingBackend();

    // Load scripted algorithms so they appear in the algorithm dropdown.
    // The daemon also creates its own ScriptedAlgorithmLoader — the KCM runs
    // in a separate process, so both need an independent loader to populate
    // the shared AlgorithmRegistry singleton within their respective processes.
    auto* scriptLoader = new ScriptedAlgorithmLoader(this);
    scriptLoader->scanAndRegister();

    // When scripted algorithms change (hot-reload), notify UI consumers.
    // Emit both signals: availableAlgorithmsChanged for algorithm-specific
    // listeners, and layoutsChanged so LayoutComboBox rebuilds its model
    // (the layouts list includes autotile entries from the registry).
    connect(scriptLoader, &ScriptedAlgorithmLoader::algorithmsChanged, this,
            &SettingsController::availableAlgorithmsChanged);
    connect(scriptLoader, &ScriptedAlgorithmLoader::algorithmsChanged, this, &SettingsController::layoutsChanged);

    // Listen for external settings changes from the daemon
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::Settings), QStringLiteral("settingsChanged"), this,
                                          SLOT(onExternalSettingsChanged()));

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

    // Editor and fill-on-drop settings lack Q_PROPERTY on Settings, so the
    // meta-object loop above misses their NOTIFY signals.  Connect explicitly.
    connect(&m_settings, &Settings::editorDuplicateShortcutChanged, this,
            &SettingsController::onSettingsPropertyChanged);
    connect(&m_settings, &Settings::editorSplitHorizontalShortcutChanged, this,
            &SettingsController::onSettingsPropertyChanged);
    connect(&m_settings, &Settings::editorSplitVerticalShortcutChanged, this,
            &SettingsController::onSettingsPropertyChanged);
    connect(&m_settings, &Settings::editorFillShortcutChanged, this, &SettingsController::onSettingsPropertyChanged);
    connect(&m_settings, &Settings::editorGridSnappingEnabledChanged, this,
            &SettingsController::onSettingsPropertyChanged);
    connect(&m_settings, &Settings::editorEdgeSnappingEnabledChanged, this,
            &SettingsController::onSettingsPropertyChanged);
    connect(&m_settings, &Settings::editorSnapIntervalXChanged, this, &SettingsController::onSettingsPropertyChanged);
    connect(&m_settings, &Settings::editorSnapIntervalYChanged, this, &SettingsController::onSettingsPropertyChanged);
    connect(&m_settings, &Settings::editorSnapOverrideModifierChanged, this,
            &SettingsController::onSettingsPropertyChanged);
    connect(&m_settings, &Settings::fillOnDropEnabledChanged, this, &SettingsController::onSettingsPropertyChanged);
    connect(&m_settings, &Settings::fillOnDropModifierChanged, this, &SettingsController::onSettingsPropertyChanged);

    // Screen helper signals
    m_screenHelper.connectToDaemonSignals();
    m_screenHelper.refreshScreens();
    connect(&m_screenHelper, &ScreenHelper::screensChanged, this, &SettingsController::screensChanged);
    connect(&m_screenHelper, &ScreenHelper::needsSave, this, [this]() {
        setNeedsSave(true);
    });

    // Layout load timer (debounce)
    m_layoutLoadTimer.setSingleShot(true);
    m_layoutLoadTimer.setInterval(50);
    connect(&m_layoutLoadTimer, &QTimer::timeout, this, &SettingsController::loadLayoutsAsync);

    // Connect layout D-Bus signals for live updates
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::LayoutManager), QStringLiteral("layoutCreated"),
                                          this, SLOT(loadLayoutsAsync()));
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::LayoutManager), QStringLiteral("layoutDeleted"),
                                          this, SLOT(loadLayoutsAsync()));
    // layoutChanged fires when a layout is modified (editor saves, zone changes, rename)
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::LayoutManager), QStringLiteral("layoutChanged"),
                                          this, SLOT(loadLayoutsAsync()));
    // layoutListChanged fires when the layout list changes (editor, import, system layout reload)
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::LayoutManager), QStringLiteral("layoutListChanged"),
                                          this, SLOT(loadLayoutsAsync()));
    // screenLayoutChanged(QString,QString,int) fires when assignments change (hotkeys, scripts, toggle)
    QDBusConnection::sessionBus().connect(
        QString(DBus::ServiceName), QString(DBus::ObjectPath), QString(DBus::Interface::LayoutManager),
        QStringLiteral("screenLayoutChanged"), this, SLOT(onScreenLayoutChanged(QString, QString, int)));
    // quickLayoutSlotsChanged fires when quick layout slots are modified externally
    QDBusConnection::sessionBus().connect(
        QString(DBus::ServiceName), QString(DBus::ObjectPath), QString(DBus::Interface::LayoutManager),
        QStringLiteral("quickLayoutSlotsChanged"), this, SIGNAL(quickLayoutSlotsChanged()));

    // Connect virtual desktop / activity D-Bus signals for reactive updates
    QDBusConnection::sessionBus().connect(
        QString(DBus::ServiceName), QString(DBus::ObjectPath), QString(DBus::Interface::LayoutManager),
        QStringLiteral("virtualDesktopCountChanged"), this, SLOT(onVirtualDesktopsChanged()));
    QDBusConnection::sessionBus().connect(
        QString(DBus::ServiceName), QString(DBus::ObjectPath), QString(DBus::Interface::LayoutManager),
        QStringLiteral("virtualDesktopNamesChanged"), this, SLOT(onVirtualDesktopsChanged()));
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::LayoutManager), QStringLiteral("activitiesChanged"),
                                          this, SLOT(onActivitiesChanged()));
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::LayoutManager),
                                          QStringLiteral("currentActivityChanged"), this, SLOT(onActivitiesChanged()));

    // Connect editor settings signals from Settings to SettingsController for QML forwarding
    connect(&m_settings, &Settings::editorDuplicateShortcutChanged, this,
            &SettingsController::editorDuplicateShortcutChanged);
    connect(&m_settings, &Settings::editorSplitHorizontalShortcutChanged, this,
            &SettingsController::editorSplitHorizontalShortcutChanged);
    connect(&m_settings, &Settings::editorSplitVerticalShortcutChanged, this,
            &SettingsController::editorSplitVerticalShortcutChanged);
    connect(&m_settings, &Settings::editorFillShortcutChanged, this, &SettingsController::editorFillShortcutChanged);
    connect(&m_settings, &Settings::editorGridSnappingEnabledChanged, this,
            &SettingsController::editorGridSnappingEnabledChanged);
    connect(&m_settings, &Settings::editorEdgeSnappingEnabledChanged, this,
            &SettingsController::editorEdgeSnappingEnabledChanged);
    connect(&m_settings, &Settings::editorSnapIntervalXChanged, this, &SettingsController::editorSnapIntervalXChanged);
    connect(&m_settings, &Settings::editorSnapIntervalYChanged, this, &SettingsController::editorSnapIntervalYChanged);
    connect(&m_settings, &Settings::editorSnapOverrideModifierChanged, this,
            &SettingsController::editorSnapOverrideModifierChanged);
    connect(&m_settings, &Settings::fillOnDropEnabledChanged, this, &SettingsController::fillOnDropEnabledChanged);
    connect(&m_settings, &Settings::fillOnDropModifierChanged, this, &SettingsController::fillOnDropModifierChanged);
    // Forward lock state changes (from Settings::load() after external D-Bus settingsChanged)
    connect(&m_settings, &Settings::lockedScreensChanged, this, &SettingsController::lockedScreensChanged);

    // Load dismissed update version from app-local settings
    {
        QSettings appSettings(QStringLiteral("plasmazones"), QStringLiteral("settings-window"));
        m_dismissedUpdateVersion = appSettings.value(QStringLiteral("dismissedUpdateVersion")).toString();
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
        QSettings appSettings(QStringLiteral("plasmazones"), QStringLiteral("settings-window"));
        appSettings.setValue(QStringLiteral("dismissedUpdateVersion"), version);
        Q_EMIT dismissedUpdateVersionChanged();
    }
}

void SettingsController::dismissUpdate()
{
    setDismissedUpdateVersion(m_updateChecker.latestVersion());
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
        QStringLiteral("apprules"),
        QStringLiteral("exclusions"),
        QStringLiteral("editor"),
        QStringLiteral("general"),
        QStringLiteral("about"),
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
        // Capture dirty state BEFORE emitting, because the QML Loader
        // reacts synchronously to activePageChanged — new page creation
        // may trigger NOTIFY signals that set needsSave before we return.
        const bool wasDirty = m_needsSave;
        m_loading = true;
        m_activePage = resolved;
        Q_EMIT activePageChanged();
        m_loading = false;
        // Restore the dirty state that existed before page navigation.
        // Any NOTIFY signals that fired during page creation were suppressed
        // by m_loading and should not mark the settings as dirty.
        setNeedsSave(wasDirty);
    }
}

bool SettingsController::registerDBusService()
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(DBus::SettingsApp::ServiceName)) {
        return false;
    }
    // ExportScriptableSlots exposes all Q_SCRIPTABLE methods on this object (raise + setActivePage).
    // Adding new Q_SCRIPTABLE slots will automatically expose them on D-Bus.
    bus.registerObject(DBus::SettingsApp::ObjectPath, this, QDBusConnection::ExportScriptableSlots);
    return true;
}

void SettingsController::raise()
{
    const auto windows = QGuiApplication::allWindows();
    for (auto* w : windows) {
        if (w->type() != Qt::Window)
            continue;
        w->show();
        w->raise();
        w->requestActivate();
        break; // Only raise the primary application window
    }
}

void SettingsController::load()
{
    m_loading = true;
    m_settings.load();
    m_screenHelper.refreshScreens();
    scheduleLayoutLoad();
    m_stagedAssignments.clear();
    m_stagedQuickSlots.clear();
    m_stagedTilingQuickSlots.clear();
    m_stagedVirtualScreenConfigs.clear();
    m_loading = false;
    setNeedsSave(false);
}

void SettingsController::save()
{
    m_saving = true;

    // Flush staged tiling quick layout slots to config BEFORE save
    // so the daemon sees them when it reparses
    if (!m_stagedTilingQuickSlots.isEmpty()) {
        for (auto it = m_stagedTilingQuickSlots.constBegin(); it != m_stagedTilingQuickSlots.constEnd(); ++it) {
            m_settings.writeTilingQuickLayoutSlot(it.key(), it.value());
        }
        m_stagedTilingQuickSlots.clear();
    }

    // Persist staged virtual screen configurations to m_settings BEFORE save
    // so they are written to KConfig on disk, then flush to daemon via D-Bus.
    if (!m_stagedVirtualScreenConfigs.isEmpty()) {
        for (auto it = m_stagedVirtualScreenConfigs.constBegin(); it != m_stagedVirtualScreenConfigs.constEnd(); ++it) {
            VirtualScreenConfig vsConfig;
            vsConfig.physicalScreenId = it.key();
            if (!it.value().isEmpty()) {
                for (int i = 0; i < it.value().size(); ++i) {
                    VirtualScreenDef def = variantMapToVirtualScreenDef(it.value()[i].toMap(), it.key(), i);
                    if (!def.isValid()) {
                        qCWarning(lcConfig) << "Skipping invalid virtual screen def for" << it.key() << "index" << i
                                            << "region:" << def.region;
                        continue;
                    }
                    vsConfig.screens.append(def);
                }
            }
            m_settings.setVirtualScreenConfig(it.key(), vsConfig);
        }
    }

    // Save main settings (includes editor settings + VS configs persisted above)
    m_settings.save();

    // Flush staged virtual screen configurations to daemon via D-Bus BEFORE notifyReload
    // so that virtual screen IDs exist when assignments referencing them are processed.
    if (!m_stagedVirtualScreenConfigs.isEmpty()) {
        for (auto it = m_stagedVirtualScreenConfigs.constBegin(); it != m_stagedVirtualScreenConfigs.constEnd(); ++it) {
            if (it.value().isEmpty()) {
                removeVirtualScreenConfig(it.key());
            } else {
                applyVirtualScreenConfig(it.key(), it.value());
            }
        }
        m_stagedVirtualScreenConfigs.clear();
    }

    // Notify daemon to reload KConfig settings (before D-Bus assignment mutations)
    DaemonDBus::notifyReload();

    // Flush staged quick layout slots to daemon (D-Bus mutations, after reload)
    for (auto it = m_stagedQuickSlots.constBegin(); it != m_stagedQuickSlots.constEnd(); ++it) {
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("setQuickLayoutSlot"),
                               {it.key(), it.value()});
    }
    m_stagedQuickSlots.clear();

    // Flush staged assignment changes to daemon (same batch protocol as KCM).
    // This must happen AFTER notifyReload so the reload doesn't overwrite
    // the assignment changes.
    if (!m_stagedAssignments.isEmpty()) {
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("setSaveBatchMode"), {true});
        flushStagedAssignments();
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("applyAssignmentChanges"));
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("setSaveBatchMode"), {false});
    }

    // Safe to clear immediately: notifyReload() is synchronous, so the daemon
    // has already processed the reload and emitted settingsChanged before we
    // reach this line. No deferred reset needed.
    m_saving = false;

    setNeedsSave(false);
}

void SettingsController::defaults()
{
    // reset() deletes all config groups, syncs to disk, then calls load()
    // internally -- no separate load() needed.
    // reset() deletes the [Editor] group and reloads defaults internally
    m_settings.reset();

    m_stagedAssignments.clear();
    m_stagedQuickSlots.clear();
    m_stagedTilingQuickSlots.clear();
    m_stagedVirtualScreenConfigs.clear();

    // Notify daemon to reload — reset() wrote defaults to disk
    DaemonDBus::notifyReload();

    setNeedsSave(true);
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
    if (m_needsSave != needs) {
        m_needsSave = needs;
        Q_EMIT needsSaveChanged();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Layout management (D-Bus to daemon, no KCM LayoutManager class needed)
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::scheduleLayoutLoad()
{
    m_layoutLoadTimer.start();
}

void SettingsController::loadLayoutsAsync()
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                       QString(DBus::Interface::LayoutManager), QStringLiteral("getLayoutList"));

    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();

        QVariantList newLayouts;
        QDBusPendingReply<QStringList> reply = *w;

        if (!reply.isError()) {
            const QStringList layoutJsonList = reply.value();
            for (const QString& layoutJson : layoutJsonList) {
                QJsonDocument doc = QJsonDocument::fromJson(layoutJson.toUtf8());
                if (!doc.isNull() && doc.isObject()) {
                    newLayouts.append(doc.object().toVariantMap());
                }
            }
        } else {
            qCWarning(lcCore) << "Failed to load layouts:" << reply.error().message();
        }

        // Sort: manual layouts first, then autotile, each alphabetical
        std::sort(newLayouts.begin(), newLayouts.end(), [](const QVariant& a, const QVariant& b) {
            const QVariantMap mapA = a.toMap();
            const QVariantMap mapB = b.toMap();
            const bool aIsAutotile = mapA.value(QStringLiteral("isAutotile")).toBool();
            const bool bIsAutotile = mapB.value(QStringLiteral("isAutotile")).toBool();
            if (aIsAutotile != bIsAutotile)
                return !aIsAutotile;
            return mapA.value(QStringLiteral("name")).toString().toLower()
                < mapB.value(QStringLiteral("name")).toString().toLower();
        });

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

void SettingsController::createNewLayout()
{
    createNewLayout(QStringLiteral("New Layout"), QStringLiteral("custom"), -1, true);
}

bool SettingsController::createNewLayout(const QString& name, const QString& type, int aspectRatioClass,
                                         bool openInEditor)
{
    QString sanitizedName = name.trimmed();
    if (sanitizedName.isEmpty())
        sanitizedName = QStringLiteral("New Layout");

    const QString layoutType = type.isEmpty() ? QStringLiteral("custom") : type;

    QDBusMessage reply = DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("createLayout"),
                                                {sanitizedName, layoutType});

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString newLayoutId = reply.arguments().first().toString();
        if (!newLayoutId.isEmpty()) {
            if (aspectRatioClass >= 0) {
                QDBusMessage arReply = DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                                                              QStringLiteral("setLayoutAspectRatioClass"),
                                                              {newLayoutId, aspectRatioClass});
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
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("deleteLayout"), {layoutId});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "deleteLayout failed:" << reply.errorMessage();
        Q_EMIT layoutOperationFailed(PzI18n::tr("Could not delete layout: %1").arg(reply.errorMessage()));
    }
    scheduleLayoutLoad();
}

void SettingsController::duplicateLayout(const QString& layoutId)
{
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("duplicateLayout"), {layoutId});
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

void SettingsController::editLayout(const QString& layoutId)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                                      QString(DBus::Interface::LayoutManager),
                                                      QStringLiteral("openEditorForLayoutOnScreen"));
    msg << layoutId << QString();
    QDBusConnection::sessionBus().asyncCall(msg);
}

void SettingsController::editLayoutOnScreen(const QString& layoutId, const QString& screenId)
{
    if (layoutId.isEmpty() || screenId.isEmpty())
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                                      QString(DBus::Interface::LayoutManager),
                                                      QStringLiteral("openEditorForLayoutOnScreen"));
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
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("importLayout"), {filePath});
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
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                       QString(DBus::Interface::LayoutManager), QStringLiteral("exportLayout"));
    msg << layoutId << filePath;
    QDBusConnection::sessionBus().asyncCall(msg);
}

void SettingsController::setLayoutHidden(const QString& layoutId, bool hidden)
{
    if (layoutId.isEmpty())
        return;
    DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("setLayoutHidden"),
                           {layoutId, hidden});
    scheduleLayoutLoad();
}

void SettingsController::setLayoutAutoAssign(const QString& layoutId, bool enabled)
{
    if (layoutId.isEmpty())
        return;
    DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("setLayoutAutoAssign"),
                           {layoutId, enabled});
    scheduleLayoutLoad();
}

void SettingsController::setLayoutAspectRatio(const QString& layoutId, int aspectRatioClass)
{
    if (layoutId.isEmpty())
        return;
    DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("setLayoutAspectRatioClass"),
                           {layoutId, aspectRatioClass});
    scheduleLayoutLoad();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Assignment helpers (D-Bus to daemon LayoutManager)
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
// Assignment staging helpers
// ═══════════════════════════════════════════════════════════════════════════════

QString SettingsController::assignmentCacheKey(const QString& screen, int desktop, const QString& activity)
{
    // Resolve connector names to EDID-based screen IDs so cache keys
    // match regardless of whether the caller passes "DP-3" or the full ID
    QString resolved = Utils::screenIdForName(screen);
    return resolved + QChar(0x1F) + QString::number(desktop) + QChar(0x1F) + activity;
}

SettingsController::StagedAssignment& SettingsController::stagedEntry(const QString& screen, int desktop,
                                                                      const QString& activity)
{
    const QString key = assignmentCacheKey(screen, desktop, activity);
    auto it = m_stagedAssignments.find(key);
    if (it == m_stagedAssignments.end()) {
        StagedAssignment entry;
        entry.screenId = Utils::screenIdForName(screen);
        entry.virtualDesktop = desktop;
        entry.activityId = activity;
        it = m_stagedAssignments.insert(key, entry);
    }
    return *it;
}

const SettingsController::StagedAssignment* SettingsController::stagedEntryConst(const QString& screen, int desktop,
                                                                                 const QString& activity) const
{
    const QString key = assignmentCacheKey(screen, desktop, activity);
    auto it = m_stagedAssignments.constFind(key);
    return it != m_stagedAssignments.constEnd() ? &(*it) : nullptr;
}

void SettingsController::flushStagedAssignments()
{
    qCInfo(PlasmaZones::lcCore) << "flushStagedAssignments: count=" << m_stagedAssignments.size();
    for (auto it = m_stagedAssignments.constBegin(); it != m_stagedAssignments.constEnd(); ++it) {
        const auto& s = it.value();
        const bool isActivity = !s.activityId.isEmpty();
        const bool isDesktop = s.virtualDesktop > 0;
        qCInfo(PlasmaZones::lcCore)
            << "  flush: screen=" << s.screenId << "fullCleared=" << s.fullCleared
            << "mode=" << (s.stagedMode.has_value() ? QString::number(*s.stagedMode) : QStringLiteral("(none)"))
            << "snapping=" << (s.snappingLayoutId.has_value() ? *s.snappingLayoutId : QStringLiteral("(none)"))
            << "tiling=" << (s.tilingAlgorithmId.has_value() ? *s.tilingAlgorithmId : QStringLiteral("(none)"));

        // Full clear — clear the entire entry for this context
        if (s.fullCleared) {
            if (isActivity)
                DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                                       QStringLiteral("clearAssignmentForScreenActivity"), {s.screenId, s.activityId});
            else if (isDesktop)
                DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                                       QStringLiteral("clearAssignmentForScreenDesktop"),
                                       {s.screenId, s.virtualDesktop});
            else
                DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("clearAssignment"),
                                       {s.screenId});
            continue;
        }

        // Explicit mode staging (overview page) — uses setAssignmentEntry for the
        // exact (screen, desktop, activity) context. This is the only D-Bus method
        // that targets a full context triple, matching the KCM batch save path.
        if (s.stagedMode.has_value()) {
            const int mode = *s.stagedMode;
            const QString snapping = s.snappingLayoutId.value_or(QString());
            const QString tiling = s.tilingAlgorithmId.has_value()
                ? (LayoutId::isAutotile(*s.tilingAlgorithmId) ? LayoutId::extractAlgorithmId(*s.tilingAlgorithmId)
                                                              : *s.tilingAlgorithmId)
                : QString();
            DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("setAssignmentEntry"),
                                   {s.screenId, s.virtualDesktop, s.activityId, mode, snapping, tiling});
            continue;
        }

        // Snapping layout assignment (per-field path — assignment pages)
        if (s.snappingLayoutId.has_value() && !s.snappingLayoutId->isEmpty()) {
            QDBusMessage reply;
            if (isActivity)
                reply = DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                                               QStringLiteral("assignLayoutToScreenActivity"),
                                               {s.screenId, s.activityId, *s.snappingLayoutId});
            else if (isDesktop)
                reply = DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                                               QStringLiteral("assignLayoutToScreenDesktop"),
                                               {s.screenId, s.virtualDesktop, *s.snappingLayoutId});
            else
                reply =
                    DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                                           QStringLiteral("assignLayoutToScreen"), {s.screenId, *s.snappingLayoutId});
            if (reply.type() == QDBusMessage::ErrorMessage)
                qCWarning(PlasmaZones::lcCore) << "  assignLayout FAILED:" << reply.errorMessage();
        }

        // Tiling algorithm assignment
        if (s.tilingAlgorithmId.has_value() && !s.tilingAlgorithmId->isEmpty()) {
            const QString algoId = LayoutId::isAutotile(*s.tilingAlgorithmId)
                ? LayoutId::extractAlgorithmId(*s.tilingAlgorithmId)
                : *s.tilingAlgorithmId;
            DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("setAssignmentEntry"),
                                   {s.screenId, s.virtualDesktop, s.activityId, 1, QString(), algoId});
        }

        // Tiling-only clear — clearing the algorithm reverts to snapping mode (mode=0).
        // The daemon clamps mode via qBound(0, mode, 1).
        if (s.tilingAlgorithmId.has_value() && s.tilingAlgorithmId->isEmpty() && !s.fullCleared) {
            DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("setAssignmentEntry"),
                                   {s.screenId, s.virtualDesktop, s.activityId, 0, QString(), QString()});
        }
    }
    m_stagedAssignments.clear();
}

bool SettingsController::stagedSnappingLayout(const QString& screen, int desktop, const QString& activity,
                                              QString& out) const
{
    auto* s = stagedEntryConst(screen, desktop, activity);
    if (!s)
        return false;
    if (s->fullCleared && !s->snappingLayoutId.has_value()) {
        out = QString();
        return true;
    }
    if (s->snappingLayoutId.has_value()) {
        out = *s->snappingLayoutId;
        return true;
    }
    return false;
}

bool SettingsController::stagedTilingLayout(const QString& screen, int desktop, const QString& activity,
                                            QString& out) const
{
    auto* s = stagedEntryConst(screen, desktop, activity);
    if (!s)
        return false;
    if (s->fullCleared && !s->tilingAlgorithmId.has_value()) {
        out = QString();
        return true;
    }
    if (s->tilingAlgorithmId.has_value()) {
        const QString& val = *s->tilingAlgorithmId;
        if (val.isEmpty()) {
            out = QString();
        } else {
            out = LayoutId::isAutotile(val) ? val : LayoutId::makeAutotileId(val);
        }
        return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Assignment mutations (staged — flushed to daemon on save)
// ═══════════════════════════════════════════════════════════════════════════════

// ── Staged mutation helpers ──────────────────────────────────────────────────

void SettingsController::stageSnapping(const QString& screen, int desktop, const QString& activity,
                                       const QString& layoutId)
{
    auto& e = stagedEntry(screen, desktop, activity);
    e.fullCleared = false;
    e.stagedMode = std::nullopt;
    e.snappingLayoutId = layoutId;
    e.tilingAlgorithmId = std::nullopt; // Clear opposite to prevent mode conflict on flush
    setNeedsSave(true);
}

void SettingsController::stageTiling(const QString& screen, int desktop, const QString& activity,
                                     const QString& layoutId)
{
    auto& e = stagedEntry(screen, desktop, activity);
    e.fullCleared = false;
    e.stagedMode = std::nullopt;
    e.tilingAlgorithmId = layoutId;
    e.snappingLayoutId = std::nullopt; // Clear opposite to prevent mode conflict on flush
    setNeedsSave(true);
}

void SettingsController::stageFullClear(const QString& screen, int desktop, const QString& activity)
{
    auto& e = stagedEntry(screen, desktop, activity);
    e.fullCleared = true;
    e.stagedMode = std::nullopt;
    e.snappingLayoutId = std::nullopt;
    e.tilingAlgorithmId = std::nullopt;
    setNeedsSave(true);
}

void SettingsController::stageTilingClear(const QString& screen, int desktop, const QString& activity)
{
    auto& e = stagedEntry(screen, desktop, activity);
    e.tilingAlgorithmId = QString(); // empty = cleared
    setNeedsSave(true);
}

// ── Snapping assignment delegates ───────────────────────────────────────────

void SettingsController::assignLayoutToScreen(const QString& screenName, const QString& layoutId)
{
    stageSnapping(screenName, 0, QString(), layoutId);
}

void SettingsController::assignLayoutToScreenDesktop(const QString& screenName, int virtualDesktop,
                                                     const QString& layoutId)
{
    stageSnapping(screenName, virtualDesktop, QString(), layoutId);
}

void SettingsController::assignLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                                      const QString& layoutId)
{
    stageSnapping(screenName, 0, activityId, layoutId);
}

// ── Tiling assignment delegates ─────────────────────────────────────────────

void SettingsController::assignTilingLayoutToScreen(const QString& screenName, const QString& layoutId)
{
    stageTiling(screenName, 0, QString(), layoutId);
}

void SettingsController::assignTilingLayoutToScreenDesktop(const QString& screenName, int virtualDesktop,
                                                           const QString& layoutId)
{
    stageTiling(screenName, virtualDesktop, QString(), layoutId);
}

void SettingsController::assignTilingLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                                            const QString& layoutId)
{
    stageTiling(screenName, 0, activityId, layoutId);
}

// ── Full-clear assignment delegates ─────────────────────────────────────────

void SettingsController::clearScreenAssignment(const QString& screenName)
{
    stageFullClear(screenName, 0, QString());
}

void SettingsController::clearTilingScreenAssignment(const QString& screenName)
{
    stageTilingClear(screenName, 0, QString());
}

void SettingsController::clearScreenDesktopAssignment(const QString& screenName, int virtualDesktop)
{
    stageFullClear(screenName, virtualDesktop, QString());
}

void SettingsController::clearScreenActivityAssignment(const QString& screenName, const QString& activityId)
{
    stageFullClear(screenName, 0, activityId);
}

// ── Tiling-only clear delegates ─────────────────────────────────────────────

void SettingsController::clearTilingScreenDesktopAssignment(const QString& screenName, int virtualDesktop)
{
    stageTilingClear(screenName, virtualDesktop, QString());
}

void SettingsController::clearTilingScreenActivityAssignment(const QString& screenName, const QString& activityId)
{
    stageTilingClear(screenName, 0, activityId);
}

// ── Atomic mode+layout staging (overview page) ─────────────────────────────

void SettingsController::stageAssignmentEntry(const QString& screenName, int virtualDesktop, const QString& activityId,
                                              int mode, const QString& snappingLayoutId,
                                              const QString& tilingAlgorithmId)
{
    auto& e = stagedEntry(screenName, virtualDesktop, activityId);
    e.fullCleared = false;
    e.stagedMode = mode;
    e.snappingLayoutId = snappingLayoutId.isEmpty() ? std::nullopt : std::optional<QString>(snappingLayoutId);
    e.tilingAlgorithmId = tilingAlgorithmId.isEmpty() ? std::nullopt : std::optional<QString>(tilingAlgorithmId);
    setNeedsSave(true);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Assignment query helpers (check staged state, then fall back to D-Bus)
// ═══════════════════════════════════════════════════════════════════════════════

QString SettingsController::getLayoutForScreen(const QString& screenName) const
{
    QString staged;
    if (stagedSnappingLayout(screenName, 0, QString(), staged))
        return staged;
    QDBusMessage reply = DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                                                QStringLiteral("getLayoutForScreen"), {screenName});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

QString SettingsController::getTilingLayoutForScreen(const QString& screenName) const
{
    QString staged;
    if (stagedTilingLayout(screenName, 0, QString(), staged))
        return staged;
    QDBusMessage reply = DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                                                QStringLiteral("getTilingAlgorithmForScreenDesktop"), {screenName, 0});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

QString SettingsController::getLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    QString staged;
    if (stagedSnappingLayout(screenName, virtualDesktop, QString(), staged))
        return staged;
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getLayoutForScreenDesktop"),
                               {screenName, virtualDesktop});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

QString SettingsController::getSnappingLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    QString staged;
    if (stagedSnappingLayout(screenName, virtualDesktop, QString(), staged))
        return staged;
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                               QStringLiteral("getSnappingLayoutForScreenDesktop"), {screenName, virtualDesktop});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

bool SettingsController::hasExplicitAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    QString snap, tile;
    bool hasSnap = stagedSnappingLayout(screenName, virtualDesktop, QString(), snap);
    bool hasTile = stagedTilingLayout(screenName, virtualDesktop, QString(), tile);
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
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                               QStringLiteral("hasExplicitAssignmentForScreenDesktop"), {screenName, virtualDesktop});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toBool();
    return false;
}

QString SettingsController::getTilingLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    QString staged;
    if (stagedTilingLayout(screenName, virtualDesktop, QString(), staged))
        return staged;
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                               QStringLiteral("getTilingAlgorithmForScreenDesktop"), {screenName, virtualDesktop});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString algo = reply.arguments().first().toString();
        if (!algo.isEmpty())
            return LayoutId::makeAutotileId(algo);
    }
    return {};
}

bool SettingsController::hasExplicitTilingAssignmentForScreenDesktop(const QString& screenName,
                                                                     int virtualDesktop) const
{
    QString staged;
    if (stagedTilingLayout(screenName, virtualDesktop, QString(), staged))
        return !staged.isEmpty();
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                               QStringLiteral("getTilingAlgorithmForScreenDesktop"), {screenName, virtualDesktop});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return !reply.arguments().first().toString().isEmpty();
    return false;
}

QString SettingsController::getLayoutForScreenActivity(const QString& screenName, const QString& activityId) const
{
    QString staged;
    if (stagedSnappingLayout(screenName, 0, activityId, staged))
        return staged;
    QDBusMessage reply = DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                                                QStringLiteral("getLayoutForScreenActivity"), {screenName, activityId});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

QString SettingsController::getSnappingLayoutForScreenActivity(const QString& screenName,
                                                               const QString& activityId) const
{
    QString staged;
    if (stagedSnappingLayout(screenName, 0, activityId, staged))
        return staged;
    QDBusMessage reply = DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                                                QStringLiteral("getLayoutForScreenActivity"), {screenName, activityId});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString layoutId = reply.arguments().first().toString();
        if (!layoutId.isEmpty() && !LayoutId::isAutotile(layoutId))
            return layoutId;
    }
    return {};
}

bool SettingsController::hasExplicitAssignmentForScreenActivity(const QString& screenName,
                                                                const QString& activityId) const
{
    QString snap, tile;
    bool hasSnap = stagedSnappingLayout(screenName, 0, activityId, snap);
    bool hasTile = stagedTilingLayout(screenName, 0, activityId, tile);
    if (hasSnap || hasTile) {
        if (!snap.isEmpty() || !tile.isEmpty())
            return true;
        if (hasSnap && hasTile)
            return false;
    }
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                               QStringLiteral("hasExplicitAssignmentForScreenActivity"), {screenName, activityId});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toBool();
    return false;
}

QString SettingsController::getTilingLayoutForScreenActivity(const QString& screenName, const QString& activityId) const
{
    QString staged;
    if (stagedTilingLayout(screenName, 0, activityId, staged))
        return staged;
    QDBusMessage reply = DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                                                QStringLiteral("getLayoutForScreenActivity"), {screenName, activityId});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString layoutId = reply.arguments().first().toString();
        if (LayoutId::isAutotile(layoutId))
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
    // Return staged value if the user changed it before Apply
    auto it = m_stagedQuickSlots.constFind(slotNumber);
    if (it != m_stagedQuickSlots.constEnd())
        return it.value();
    QDBusMessage reply = DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                                                QStringLiteral("getQuickLayoutSlot"), {slotNumber});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

void SettingsController::setQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    if (slotNumber < 1 || slotNumber > 9)
        return;
    m_stagedQuickSlots[slotNumber] = layoutId;
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
    // Return staged value if the user changed it before Apply
    auto it = m_stagedTilingQuickSlots.constFind(slotNumber);
    if (it != m_stagedTilingQuickSlots.constEnd())
        return it.value();
    return m_settings.readTilingQuickLayoutSlot(slotNumber);
}

void SettingsController::setTilingQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    if (slotNumber < 1 || slotNumber > 9)
        return;
    m_stagedTilingQuickSlots[slotNumber] = layoutId;
    setNeedsSave(true);
}

// ═══════════════════════════════════════════════════════════════════════════════
// App-to-zone rules (D-Bus to daemon, reading from layout JSON)
// ═══════════════════════════════════════════════════════════════════════════════

QVariantList SettingsController::getAppRulesForLayout(const QString& layoutId) const
{
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getLayout"), {layoutId});
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
    QString resolved = Utils::screenIdForName(screenName);
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

bool SettingsController::isMonitorDisabled(const QString& screenName) const
{
    return m_screenHelper.isMonitorDisabled(screenName);
}

void SettingsController::setMonitorDisabled(const QString& screenName, bool disabled)
{
    m_screenHelper.setMonitorDisabled(screenName, disabled);
}

bool SettingsController::isDesktopDisabled(const QString& screenName, int desktop) const
{
    return m_settings.isDesktopDisabled(screenName, desktop);
}

void SettingsController::setDesktopDisabled(const QString& screenName, int desktop, bool disabled)
{
    QString key = screenName + QLatin1Char('/') + QString::number(desktop);
    QStringList entries = m_settings.disabledDesktops();
    if (disabled && !entries.contains(key)) {
        entries.append(key);
        m_settings.setDisabledDesktops(entries);
        setNeedsSave(true);
        Q_EMIT disabledDesktopsChanged();
    } else if (!disabled && entries.removeAll(key) > 0) {
        m_settings.setDisabledDesktops(entries);
        setNeedsSave(true);
        Q_EMIT disabledDesktopsChanged();
    }
}

bool SettingsController::isActivityDisabled(const QString& screenName, const QString& activityId) const
{
    return m_settings.isActivityDisabled(screenName, activityId);
}

void SettingsController::setActivityDisabled(const QString& screenName, const QString& activityId, bool disabled)
{
    QString key = screenName + QLatin1Char('/') + activityId;
    QStringList entries = m_settings.disabledActivities();
    if (disabled && !entries.contains(key)) {
        entries.append(key);
        m_settings.setDisabledActivities(entries);
        setNeedsSave(true);
        Q_EMIT disabledActivitiesChanged();
    } else if (!disabled && entries.removeAll(key) > 0) {
        m_settings.setDisabledActivities(entries);
        setNeedsSave(true);
        Q_EMIT disabledActivitiesChanged();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual desktops / activities (D-Bus queries to daemon)
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::refreshVirtualDesktops()
{
    QDBusMessage countReply =
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getVirtualDesktopCount"));
    if (countReply.type() == QDBusMessage::ReplyMessage && !countReply.arguments().isEmpty()) {
        m_virtualDesktopCount = countReply.arguments().first().toInt();
    }

    QDBusMessage namesReply =
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getVirtualDesktopNames"));
    if (namesReply.type() == QDBusMessage::ReplyMessage && !namesReply.arguments().isEmpty()) {
        m_virtualDesktopNames = namesReply.arguments().first().toStringList();
    }
}

void SettingsController::refreshActivities()
{
    QDBusMessage availReply =
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("isActivitiesAvailable"));
    if (availReply.type() == QDBusMessage::ReplyMessage && !availReply.arguments().isEmpty()) {
        m_activitiesAvailable = availReply.arguments().first().toBool();
    }

    if (m_activitiesAvailable) {
        QDBusMessage infoReply =
            DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getAllActivitiesInfo"));
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

        QDBusMessage currentReply =
            DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getCurrentActivity"));
        if (currentReply.type() == QDBusMessage::ReplyMessage && !currentReply.arguments().isEmpty()) {
            m_currentActivity = currentReply.arguments().first().toString();
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Editor settings (delegated to Settings class for [Editor] group)
// ═══════════════════════════════════════════════════════════════════════════════

// Editor getters — delegate to m_settings

QString SettingsController::editorDuplicateShortcut() const
{
    return m_settings.editorDuplicateShortcut();
}
QString SettingsController::editorSplitHorizontalShortcut() const
{
    return m_settings.editorSplitHorizontalShortcut();
}
QString SettingsController::editorSplitVerticalShortcut() const
{
    return m_settings.editorSplitVerticalShortcut();
}
QString SettingsController::editorFillShortcut() const
{
    return m_settings.editorFillShortcut();
}
bool SettingsController::editorGridSnappingEnabled() const
{
    return m_settings.editorGridSnappingEnabled();
}
bool SettingsController::editorEdgeSnappingEnabled() const
{
    return m_settings.editorEdgeSnappingEnabled();
}
qreal SettingsController::editorSnapIntervalX() const
{
    return m_settings.editorSnapIntervalX();
}
qreal SettingsController::editorSnapIntervalY() const
{
    return m_settings.editorSnapIntervalY();
}
int SettingsController::editorSnapOverrideModifier() const
{
    return m_settings.editorSnapOverrideModifier();
}
bool SettingsController::fillOnDropEnabled() const
{
    return m_settings.fillOnDropEnabled();
}
int SettingsController::fillOnDropModifier() const
{
    return m_settings.fillOnDropModifier();
}

// Editor setters — write to m_settings + mark dirty

// Editor setters — delegate to Settings. The meta-object NOTIFY connection
// in the constructor handles setNeedsSave(true) when the value actually changes.
void SettingsController::setEditorDuplicateShortcut(const QString& s)
{
    m_settings.setEditorDuplicateShortcut(s);
}
void SettingsController::setEditorSplitHorizontalShortcut(const QString& s)
{
    m_settings.setEditorSplitHorizontalShortcut(s);
}
void SettingsController::setEditorSplitVerticalShortcut(const QString& s)
{
    m_settings.setEditorSplitVerticalShortcut(s);
}
void SettingsController::setEditorFillShortcut(const QString& s)
{
    m_settings.setEditorFillShortcut(s);
}
void SettingsController::setEditorGridSnappingEnabled(bool v)
{
    m_settings.setEditorGridSnappingEnabled(v);
}
void SettingsController::setEditorEdgeSnappingEnabled(bool v)
{
    m_settings.setEditorEdgeSnappingEnabled(v);
}
void SettingsController::setEditorSnapIntervalX(qreal v)
{
    m_settings.setEditorSnapIntervalX(v);
}
void SettingsController::setEditorSnapIntervalY(qreal v)
{
    m_settings.setEditorSnapIntervalY(v);
}
void SettingsController::setEditorSnapOverrideModifier(int v)
{
    m_settings.setEditorSnapOverrideModifier(v);
}
void SettingsController::setFillOnDropEnabled(bool v)
{
    m_settings.setFillOnDropEnabled(v);
}
void SettingsController::setFillOnDropModifier(int v)
{
    m_settings.setFillOnDropModifier(v);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual desktop / activity D-Bus signal handlers
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::onVirtualDesktopsChanged()
{
    refreshVirtualDesktops();

    // Prune disabled-desktop entries that reference desktops beyond the new count.
    // See start.cpp comment re: mid-range renumbering limitation.
    QStringList disabled = m_settings.disabledDesktops();
    if (pruneDisabledDesktopEntries(disabled, m_virtualDesktopCount)) {
        m_settings.setDisabledDesktops(disabled);
        setNeedsSave(true);
        Q_EMIT disabledDesktopsChanged();
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
        QStringList disabledActs = m_settings.disabledActivities();
        if (pruneDisabledActivityEntries(disabledActs, validIds)) {
            m_settings.setDisabledActivities(disabledActs);
            setNeedsSave(true);
            Q_EMIT disabledActivitiesChanged();
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

// ═══════════════════════════════════════════════════════════════════════════════
// Trigger conversion helpers (same logic as KCMSnapping)
// ═══════════════════════════════════════════════════════════════════════════════

QVariantList SettingsController::convertTriggersForQml(const QVariantList& triggers)
{
    QVariantList result;
    for (const auto& t : triggers) {
        auto map = t.toMap();
        QVariantMap converted;
        converted[ConfigDefaults::triggerModifierField()] =
            ModifierUtils::dragModifierToBitmask(map.value(ConfigDefaults::triggerModifierField(), 0).toInt());
        converted[ConfigDefaults::triggerMouseButtonField()] = map.value(ConfigDefaults::triggerMouseButtonField(), 0);
        result.append(converted);
    }
    return result;
}

QVariantList SettingsController::convertTriggersForStorage(const QVariantList& triggers)
{
    QVariantList result;
    for (const auto& t : triggers) {
        auto map = t.toMap();
        QVariantMap stored;
        stored[ConfigDefaults::triggerModifierField()] =
            ModifierUtils::bitmaskToDragModifier(map.value(ConfigDefaults::triggerModifierField(), 0).toInt());
        stored[ConfigDefaults::triggerMouseButtonField()] = map.value(ConfigDefaults::triggerMouseButtonField(), 0);
        result.append(stored);
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Trigger getters
// ═══════════════════════════════════════════════════════════════════════════════

bool SettingsController::alwaysActivateOnDrag() const
{
    const int alwaysActive = static_cast<int>(DragModifier::AlwaysActive);
    const auto triggers = m_settings.dragActivationTriggers();
    for (const auto& t : triggers) {
        if (t.toMap().value(ConfigDefaults::triggerModifierField(), 0).toInt() == alwaysActive) {
            return true;
        }
    }
    return false;
}

QVariantList SettingsController::dragActivationTriggers() const
{
    return convertTriggersForQml(m_settings.dragActivationTriggers());
}

QVariantList SettingsController::defaultDragActivationTriggers() const
{
    return convertTriggersForQml(ConfigDefaults::dragActivationTriggers());
}

QVariantList SettingsController::zoneSpanTriggers() const
{
    return convertTriggersForQml(m_settings.zoneSpanTriggers());
}

QVariantList SettingsController::defaultZoneSpanTriggers() const
{
    return convertTriggersForQml(ConfigDefaults::zoneSpanTriggers());
}

QVariantList SettingsController::snapAssistTriggers() const
{
    return convertTriggersForQml(m_settings.snapAssistTriggers());
}

QVariantList SettingsController::defaultSnapAssistTriggers() const
{
    return convertTriggersForQml(ConfigDefaults::snapAssistTriggers());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Trigger setters
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::setDragActivationTriggers(const QVariantList& triggers)
{
    const bool wasAlwaysActive = alwaysActivateOnDrag();
    const QVariantList converted = convertTriggersForStorage(triggers);
    if (m_settings.dragActivationTriggers() != converted) {
        m_settings.setDragActivationTriggers(converted);
        Q_EMIT dragActivationTriggersChanged();
        if (alwaysActivateOnDrag() != wasAlwaysActive) {
            Q_EMIT alwaysActivateOnDragChanged();
        }
        setNeedsSave(true);
    }
}

void SettingsController::setAlwaysActivateOnDrag(bool enabled)
{
    if (alwaysActivateOnDrag() == enabled) {
        return;
    }
    if (enabled) {
        // Single AlwaysActive trigger -- written directly in storage format (DragModifier enum)
        QVariantMap trigger;
        trigger[ConfigDefaults::triggerModifierField()] = static_cast<int>(DragModifier::AlwaysActive);
        trigger[ConfigDefaults::triggerMouseButtonField()] = 0;
        m_settings.setDragActivationTriggers({trigger});
    } else {
        // Revert to default triggers
        m_settings.setDragActivationTriggers(ConfigDefaults::dragActivationTriggers());
    }
    Q_EMIT alwaysActivateOnDragChanged();
    Q_EMIT dragActivationTriggersChanged();
    setNeedsSave(true);
}

void SettingsController::setZoneSpanTriggers(const QVariantList& triggers)
{
    const QVariantList converted = convertTriggersForStorage(triggers);
    if (m_settings.zoneSpanTriggers() != converted) {
        m_settings.setZoneSpanTriggers(converted);
        Q_EMIT zoneSpanTriggersChanged();
        setNeedsSave(true);
    }
}

void SettingsController::setSnapAssistTriggers(const QVariantList& triggers)
{
    QVariantList converted = convertTriggersForStorage(triggers);
    if (m_settings.snapAssistTriggers() != converted) {
        m_settings.setSnapAssistTriggers(converted);
        Q_EMIT snapAssistTriggersChanged();
        setNeedsSave(true);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cava detection
// ═══════════════════════════════════════════════════════════════════════════════

bool SettingsController::cavaAvailable() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("cava")).isEmpty();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Color import
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::loadColorsFromPywal()
{
    QString pywalPath = QDir::homePath() + QStringLiteral("/.cache/wal/colors.json");
    if (!QFile::exists(pywalPath)) {
        Q_EMIT colorImportError(
            tr("Pywal colors not found. Run 'wal' to generate colors first.\n\nExpected file: %1").arg(pywalPath));
        return;
    }

    QString error = m_settings.loadColorsFromFile(pywalPath);
    if (!error.isEmpty()) {
        Q_EMIT colorImportError(error);
        return;
    }

    Q_EMIT colorImportSuccess();
    setNeedsSave(true);
}

void SettingsController::loadColorsFromFile(const QString& filePath)
{
    QString error = m_settings.loadColorsFromFile(filePath);
    if (!error.isEmpty()) {
        Q_EMIT colorImportError(error);
        return;
    }

    Q_EMIT colorImportSuccess();
    setNeedsSave(true);
}

QVariantList SettingsController::getRunningWindows() const
{
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(DBus::Interface::Settings), QStringLiteral("getRunningWindows"));
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
        return {};
    }

    QString json = reply.arguments().at(0).toString();
    if (json.isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        return {};
    }

    QVariantList result;
    const QJsonArray array = doc.array();
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }
        QJsonObject obj = value.toObject();
        QVariantMap item;
        item[QStringLiteral("windowClass")] = obj[QLatin1String("windowClass")].toString();
        item[QStringLiteral("appName")] = obj[QLatin1String("appName")].toString();
        item[QStringLiteral("caption")] = obj[QLatin1String("caption")].toString();
        result.append(item);
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Config export/import
// ═══════════════════════════════════════════════════════════════════════════════

bool SettingsController::exportAllSettings(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return false;
    }
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
    // Backup current config
    QString backupPath = configPath + QStringLiteral(".bak");
    if (QFile::exists(backupPath)) {
        QFile::remove(backupPath);
    }
    if (!QFile::copy(configPath, backupPath)) {
        qCWarning(PlasmaZones::lcCore) << "Failed to backup config to:" << backupPath;
        return false;
    }
    // Replace with imported file
    if (QFile::exists(configPath)) {
        QFile::remove(configPath);
    }
    bool ok = QFile::copy(filePath, configPath);
    if (ok) {
        m_settings.load();
        DaemonDBus::notifyReload();
        Q_EMIT needsSaveChanged();
    } else {
        qCWarning(PlasmaZones::lcCore) << "Failed to import settings from:" << filePath;
    }
    return ok;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Screen state query
// ═══════════════════════════════════════════════════════════════════════════════

QVariantList SettingsController::getScreenStates() const
{
    QDBusMessage reply =
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getScreenStates"));
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
    return stagedEntryConst(screenName, virtualDesktop, activityId) != nullptr;
}

QVariantMap SettingsController::getStagedAssignment(const QString& screenName, int virtualDesktop,
                                                    const QString& activityId) const
{
    auto* s = stagedEntryConst(screenName, virtualDesktop, activityId);
    if (!s)
        return {};
    QVariantMap map;
    if (s->snappingLayoutId.has_value())
        map[QStringLiteral("layoutId")] = *s->snappingLayoutId;
    if (s->tilingAlgorithmId.has_value()) {
        const QString& val = *s->tilingAlgorithmId;
        // Strip "autotile:" prefix if present
        if (val.startsWith(QLatin1String("autotile:")))
            map[QStringLiteral("algorithmId")] = val.mid(9);
        else
            map[QStringLiteral("algorithmId")] = val;
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

QVariantList SettingsController::availableAlgorithms() const
{
    QVariantList algorithms;
    auto* registry = AlgorithmRegistry::instance();
    for (const QString& id : registry->availableAlgorithms()) {
        TilingAlgorithm* algo = registry->algorithm(id);
        if (algo) {
            QVariantMap algoMap;
            algoMap[QStringLiteral("id")] = id;
            algoMap[QStringLiteral("name")] = algo->name();
            algoMap[QStringLiteral("description")] = algo->description();
            algoMap[QLatin1String("defaultMaxWindows")] = algo->defaultMaxWindows();
            algoMap[QLatin1String("supportsSplitRatio")] = algo->supportsSplitRatio();
            algoMap[QLatin1String("supportsMasterCount")] = algo->supportsMasterCount();
            algoMap[QLatin1String("defaultSplitRatio")] = algo->defaultSplitRatio();
            algoMap[QLatin1String("producesOverlappingZones")] = algo->producesOverlappingZones();
            algoMap[QLatin1String("zoneNumberDisplay")] = algo->zoneNumberDisplay();
            algoMap[QLatin1String("centerLayout")] = algo->centerLayout();

            // Expose whether this algorithm declares custom parameters.
            // The full definitions are retrieved via customParamsForAlgorithm().
            algoMap[QLatin1String("supportsCustomParams")] = algo->supportsCustomParams();

            algorithms.append(algoMap);
        }
    }
    return algorithms;
}

QVariantList SettingsController::customParamsForAlgorithm(const QString& algorithmId) const
{
    auto* registry = AlgorithmRegistry::instance();
    TilingAlgorithm* algo = registry->algorithm(algorithmId);
    if (!algo || !algo->supportsCustomParams()) {
        return {};
    }

    const QVariantMap savedCustom = savedCustomParams(algorithmId);
    const QVariantList defs = algo->customParamDefList();

    QVariantList result;
    for (const auto& defVar : defs) {
        QVariantMap paramMap = defVar.toMap();
        const QString name = paramMap.value(QLatin1String("name")).toString();
        // Current value: saved value if exists, else default
        if (savedCustom.contains(name)) {
            paramMap[QLatin1String("value")] = savedCustom.value(name);
        } else {
            paramMap[QLatin1String("value")] = paramMap.value(QLatin1String("defaultValue"));
        }
        result.append(paramMap);
    }
    return result;
}

void SettingsController::setCustomParam(const QString& algorithmId, const QString& paramName, const QVariant& value)
{
    if (algorithmId.isEmpty() || paramName.isEmpty()) {
        return;
    }

    // Validate paramName exists in the algorithm's declared custom params
    auto* registry = AlgorithmRegistry::instance();
    TilingAlgorithm* algo = registry->algorithm(algorithmId);
    if (!algo || !algo->supportsCustomParams()) {
        return;
    }
    const QVariantList defs = algo->customParamDefList();
    auto defIt = std::find_if(defs.cbegin(), defs.cend(), [&paramName](const QVariant& v) {
        return v.toMap().value(QLatin1String("name")).toString() == paramName;
    });
    if (defIt == defs.cend()) {
        qCWarning(lcCore) << "setCustomParam: unknown param" << paramName << "for algorithm" << algorithmId;
        return;
    }
    const QVariantMap defMap = defIt->toMap();
    const QString defType = defMap.value(QLatin1String("type")).toString();

    // Coerce value to the declared type so QML callers can't persist wrong types
    QVariant coerced = value;
    if (defType == QLatin1String("number")) {
        bool ok = false;
        const qreal num = value.toDouble(&ok);
        if (!ok) {
            qCWarning(lcCore) << "setCustomParam: value" << value << "is not a valid number for" << paramName;
            return;
        }
        const qreal minVal = defMap.value(QLatin1String("minValue")).toDouble();
        const qreal maxVal = defMap.value(QLatin1String("maxValue")).toDouble();
        coerced = std::clamp(num, minVal, maxVal);
    } else if (defType == QLatin1String("bool")) {
        coerced = value.toBool();
    } else if (defType == QLatin1String("enum")) {
        const QString str = value.toString();
        const QStringList options = defMap.value(QLatin1String("enumOptions")).toStringList();
        if (!options.contains(str)) {
            qCWarning(lcCore) << "setCustomParam: value" << str << "not in enum options for" << paramName
                              << "(valid:" << options << ")";
            return;
        }
        coerced = str;
    } else {
        qCWarning(lcCore) << "setCustomParam: unknown param type" << defType << "for" << paramName;
        return;
    }

    QVariantMap perAlgo = m_settings.autotilePerAlgorithmSettings();
    QVariantMap algoEntry = perAlgo.value(algorithmId).toMap();
    QVariantMap customParams = algoEntry.value(PerAlgoKeys::CustomParams).toMap();
    customParams[paramName] = coerced;
    algoEntry[PerAlgoKeys::CustomParams] = customParams;

    // Preserve existing splitRatio/masterCount if not already in the entry
    if (!algoEntry.contains(PerAlgoKeys::SplitRatio)) {
        algoEntry[PerAlgoKeys::SplitRatio] = algo->defaultSplitRatio();
    }
    if (!algoEntry.contains(PerAlgoKeys::MasterCount)) {
        algoEntry[PerAlgoKeys::MasterCount] = ConfigDefaults::autotileMasterCount();
    }

    perAlgo[algorithmId] = algoEntry;
    m_settings.setAutotilePerAlgorithmSettings(perAlgo);
    Q_EMIT customParamChanged(algorithmId, paramName);
}

QVariantMap SettingsController::savedCustomParams(const QString& algorithmId) const
{
    const QVariantMap perAlgo = m_settings.autotilePerAlgorithmSettings();
    const QVariant algoEntry = perAlgo.value(algorithmId);
    if (algoEntry.isValid()) {
        const QVariant customVar = algoEntry.toMap().value(PerAlgoKeys::CustomParams);
        if (customVar.isValid()) {
            return customVar.toMap();
        }
    }
    return {};
}

QVariantList SettingsController::generateAlgorithmPreview(const QString& algorithmId, int windowCount,
                                                          double splitRatio, int masterCount) const
{
    auto* registry = AlgorithmRegistry::instance();
    TilingAlgorithm* algo = registry->algorithm(algorithmId);
    if (!algo) {
        return {};
    }

    const int previewSize = 1000;
    const QRect previewRect(0, 0, previewSize, previewSize);

    TilingState state(QStringLiteral("preview"));
    state.setMasterCount(masterCount);
    state.setSplitRatio(splitRatio);

    const int count = qMax(1, windowCount);
    TilingParams params = TilingParams::forPreview(count, previewRect, &state);

    // Include saved custom params so preview reflects user configuration
    params.customParams = savedCustomParams(algorithmId);

    QVector<QRect> zones = algo->calculateZones(params);

    return AlgorithmRegistry::zonesToRelativeGeometry(zones, previewRect);
}

void SettingsController::openAlgorithmsFolder()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/plasmazones/algorithms");
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

bool SettingsController::importAlgorithm(const QString& filePath)
{
    if (filePath.isEmpty())
        return false;

    const QFileInfo source(filePath);
    if (!source.exists() || !source.isFile())
        return false;

    const QString destDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/plasmazones/algorithms");
    QDir dir(destDir);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    const QString destPath = destDir + QLatin1Char('/') + source.fileName();

    // Remove existing file so QFile::copy succeeds (it won't overwrite)
    if (QFile::exists(destPath)) {
        QFile::remove(destPath);
    }

    const bool ok = QFile::copy(filePath, destPath);
    // ScriptedAlgorithmLoader's QFileSystemWatcher will pick up the new file automatically
    return ok;
}

QString SettingsController::algorithmIdFromLayoutId(const QString& layoutId)
{
    const QLatin1String prefix("autotile:");
    if (layoutId.startsWith(prefix))
        return layoutId.mid(prefix.size());
    return layoutId;
}

QString SettingsController::scriptedFilePath(const QString& algorithmId) const
{
    if (algorithmId.isEmpty())
        return QString();
    auto* registry = AlgorithmRegistry::instance();
    TilingAlgorithm* algo = registry->algorithm(algorithmId);
    if (!algo)
        return QString();
    auto* scripted = qobject_cast<ScriptedAlgorithm*>(algo);
    if (!scripted)
        return QString();
    const QString path = scripted->filePath();
    if (path.isEmpty() || !QFile::exists(path))
        return QString();
    return path;
}

void SettingsController::cancelAlgorithmWatcher(const QString& expectedId)
{
    auto it = m_algorithmWatchers.find(expectedId);
    if (it != m_algorithmWatchers.end()) {
        const auto& connPtr = it.value();
        if (connPtr && *connPtr)
            disconnect(*connPtr);
        m_algorithmWatchers.erase(it);
    }
}

void SettingsController::watchForAlgorithmRegistration(const QString& expectedId)
{
    // Cancel any existing watcher for this ID to prevent stacking
    cancelAlgorithmWatcher(expectedId);

    auto* registry = AlgorithmRegistry::instance();
    auto conn = std::make_shared<QMetaObject::Connection>();
    m_algorithmWatchers[expectedId] = conn;
    *conn = connect(registry, &AlgorithmRegistry::algorithmRegistered, this,
                    [this, expectedId](const QString& registeredId) {
                        if (registeredId == expectedId) {
                            auto it = m_algorithmWatchers.find(expectedId);
                            if (it != m_algorithmWatchers.end()) {
                                disconnect(*it.value());
                                m_algorithmWatchers.erase(it);
                            }
                            Q_EMIT algorithmCreated(expectedId);
                        }
                    });
    // The context object (this) ensures the lambda is not invoked if SettingsController
    // is destroyed before the timer fires — QTimer::singleShot with a context guarantees this.
    QTimer::singleShot(10000, this, [this, expectedId]() {
        auto it = m_algorithmWatchers.find(expectedId);
        if (it != m_algorithmWatchers.end()) {
            const auto& connPtr = it.value();
            if (connPtr && *connPtr)
                disconnect(*connPtr);
            m_algorithmWatchers.erase(it);
            qCWarning(lcCore) << "Algorithm registration timed out for:" << expectedId;
            Q_EMIT algorithmOperationFailed(
                PzI18n::tr("Algorithm was created but not picked up by the registry. "
                           "Try refreshing or restarting the application."));
        }
    });
}

void SettingsController::openAlgorithm(const QString& algorithmId)
{
    // Try registry first (works for already-registered algorithms)
    const QString registryPath = scriptedFilePath(algorithmId);
    if (!registryPath.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(registryPath));
        return;
    }

    // Fallback: try user algorithms dir directly (works right after creation
    // before the registry has picked up the file via QFileSystemWatcher).
    // Uses algorithmId as filename — valid for createNewAlgorithm (returns filename)
    // and duplicateAlgorithm (watches for the filename-based ID).
    const QString userPath = userAlgorithmsDir() + algorithmId + QStringLiteral(".js");
    if (QFile::exists(userPath)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(userPath));
        return;
    }

    qCWarning(lcCore) << "Cannot open algorithm — file not found for:" << algorithmId
                      << "(checked registry and user dir:" << userAlgorithmsDir() << ")";
}

void SettingsController::openLayoutFile(const QString& layoutId)
{
    if (layoutId.isEmpty())
        return;
    // Layout files use UUID without braces as the filename
    const QUuid uuid(layoutId);
    if (uuid.isNull()) {
        qCDebug(lcCore) << "openLayoutFile: not a valid UUID layout ID:" << layoutId;
        return;
    }
    const QString bareId = uuid.toString(QUuid::WithoutBraces);
    const QString filename = bareId + QStringLiteral(".json");
    // Search user dir first, then all system dirs
    const QString located =
        QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/layouts/") + filename);
    if (located.isEmpty()) {
        qCWarning(lcCore) << "Layout file not found:" << filename;
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(located));
}

bool SettingsController::deleteAlgorithm(const QString& algorithmId)
{
    if (algorithmId.isEmpty()) {
        Q_EMIT algorithmOperationFailed(PzI18n::tr("Cannot delete algorithm — no algorithm selected."));
        return false;
    }

    auto* registry = AlgorithmRegistry::instance();
    TilingAlgorithm* algo = registry->algorithm(algorithmId);
    if (!algo || !algo->isUserScript()) {
        qCWarning(lcCore) << "Cannot delete algorithm — not a user script:" << algorithmId;
        Q_EMIT algorithmOperationFailed(PzI18n::tr("Only user-created algorithms can be deleted."));
        return false;
    }

    const QString filePath = scriptedFilePath(algorithmId);
    if (filePath.isEmpty()) {
        qCWarning(lcCore) << "Algorithm file not found for:" << algorithmId;
        Q_EMIT algorithmOperationFailed(PzI18n::tr("Algorithm file not found."));
        return false;
    }

    // Only allow deleting from the user algorithms directory (canonicalize to defeat symlinks).
    // If the user dir doesn't exist yet, canonicalFilePath() returns empty — guard against
    // that becoming "/" which would match any absolute path.
    const QString rawUserDir = QFileInfo(userAlgorithmsDir()).canonicalFilePath();
    const QString userDir = rawUserDir + QLatin1Char('/');
    const QString canonicalPath = QFileInfo(filePath).canonicalFilePath();
    if (rawUserDir.isEmpty() || canonicalPath.isEmpty() || !canonicalPath.startsWith(userDir)) {
        qCWarning(lcCore) << "Refusing to delete non-user algorithm file:" << filePath << "userDir=" << rawUserDir
                          << "canonical=" << canonicalPath;
        Q_EMIT algorithmOperationFailed(
            rawUserDir.isEmpty() ? PzI18n::tr("Cannot delete — user algorithms directory does not exist.")
                                 : PzI18n::tr("Cannot delete — file is outside the user algorithms directory."));
        return false;
    }

    // Cancel any pending registration watcher for this algorithm — otherwise
    // the 10s timeout fires algorithmOperationFailed for a deliberately deleted file.
    cancelAlgorithmWatcher(algorithmId);

    // Use the canonical path for removal to ensure we delete the actual file,
    // not a symlink pointing into the user dir.
    const bool ok = QFile::remove(canonicalPath);
    if (!ok) {
        qCWarning(lcCore) << "Failed to delete algorithm file:" << canonicalPath;
        Q_EMIT algorithmOperationFailed(PzI18n::tr("Could not delete algorithm file. Check file permissions."));
    }
    // QFileSystemWatcher will pick up the deletion and trigger a refresh
    return ok;
}

bool SettingsController::duplicateAlgorithm(const QString& algorithmId)
{
    const QString sourcePath = scriptedFilePath(algorithmId);
    if (sourcePath.isEmpty()) {
        Q_EMIT algorithmOperationFailed(PzI18n::tr("Cannot duplicate — algorithm file not found."));
        return false;
    }

    auto* registry = AlgorithmRegistry::instance();
    TilingAlgorithm* algo = registry->algorithm(algorithmId);
    if (!algo) {
        Q_EMIT algorithmOperationFailed(PzI18n::tr("Cannot duplicate — algorithm is no longer registered."));
        return false;
    }

    const QString destDir = userAlgorithmsDir();
    QDir dir(destDir);
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));

    // Generate unique filename: algorithmId-copy.js, algorithmId-copy-2.js, etc.
    const QString baseName = algorithmId + QStringLiteral("-copy");
    const QString destPath = findUniqueAlgorithmPath(destDir, baseName);
    if (destPath.isEmpty()) {
        qCWarning(lcCore) << "Could not find unique filename for duplicate:" << baseName;
        Q_EMIT algorithmOperationFailed(
            PzI18n::tr("Could not duplicate algorithm — too many copies exist. "
                       "Please rename or delete existing copies."));
        return false;
    }

    // Canonicalize source path to follow symlinks and ensure we read the actual file
    const QString canonicalSource = QFileInfo(sourcePath).canonicalFilePath();
    if (canonicalSource.isEmpty()) {
        Q_EMIT algorithmOperationFailed(PzI18n::tr("Cannot duplicate — could not resolve algorithm file path."));
        return false;
    }

    // Read source, update metadata, write copy
    QFile sourceFile(canonicalSource);
    if (!sourceFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Q_EMIT algorithmOperationFailed(PzI18n::tr("Could not read source algorithm file."));
        return false;
    }
    QString content = QString::fromUtf8(sourceFile.readAll());
    sourceFile.close();

    // Update @name and @builtinId in the copy — strip all existing " (Copy)" suffixes to avoid accumulation
    const QString newFilename = QFileInfo(destPath).completeBaseName();
    QString baseCopyName = algo->name();
    while (baseCopyName.endsWith(QLatin1String(" (Copy)")))
        baseCopyName.chop(7);
    QString newName = baseCopyName + QStringLiteral(" (Copy)");
    // Sanitize newlines to prevent annotation injection (parity with createNewAlgorithm)
    newName.replace(QLatin1Char('\n'), QLatin1Char(' '));
    newName.replace(QLatin1Char('\r'), QLatin1Char(' '));
    // Replace only the FIRST @name and @builtinId annotations — using replace(QRegularExpression)
    // would replace ALL matches, corrupting any matching patterns in the algorithm body.
    static const QRegularExpression nameRe(QStringLiteral("^// @name .+"), QRegularExpression::MultilineOption);
    static const QRegularExpression idRe(QStringLiteral("^// @builtinId .+"), QRegularExpression::MultilineOption);
    QRegularExpressionMatch nameMatch = nameRe.match(content);
    if (nameMatch.hasMatch())
        content.replace(nameMatch.capturedStart(), nameMatch.capturedLength(), QStringLiteral("// @name ") + newName);
    QRegularExpressionMatch idMatch = idRe.match(content);
    if (idMatch.hasMatch())
        content.replace(idMatch.capturedStart(), idMatch.capturedLength(),
                        QStringLiteral("// @builtinId ") + newFilename);

    QFile destFile(destPath);
    if (!destFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(lcCore) << "Failed to write duplicate algorithm file:" << destPath;
        Q_EMIT algorithmOperationFailed(
            PzI18n::tr("Could not write duplicate algorithm file. Check disk space and permissions."));
        return false;
    }
    const QByteArray encoded = content.toUtf8();
    const qint64 written = destFile.write(encoded);
    destFile.close();
    if (written < 0 || written != encoded.size()) {
        qCWarning(lcCore) << "Failed to write duplicate algorithm content:" << destPath << "written=" << written
                          << "expected=" << encoded.size();
        QFile::remove(destPath);
        Q_EMIT algorithmOperationFailed(
            PzI18n::tr("Could not write duplicate algorithm file. Check disk space and permissions."));
        return false;
    }

    // Watch for registry pickup and emit algorithmCreated (issue #2: duplicate didn't fire signal)
    watchForAlgorithmRegistration(newFilename);
    return true;
}

bool SettingsController::exportAlgorithm(const QString& algorithmId, const QString& destPath)
{
    if (destPath.isEmpty()) {
        Q_EMIT algorithmOperationFailed(PzI18n::tr("No export destination specified."));
        return false;
    }

    const QString sourcePath = scriptedFilePath(algorithmId);
    if (sourcePath.isEmpty()) {
        Q_EMIT algorithmOperationFailed(PzI18n::tr("Cannot export — algorithm file not found."));
        return false;
    }

    // Write to a temp file first, then rename — if copy fails the existing file is preserved
    const QString tmpPath = destPath + QStringLiteral(".tmp");
    if (QFile::exists(tmpPath))
        QFile::remove(tmpPath);
    if (!QFile::copy(sourcePath, tmpPath)) {
        Q_EMIT algorithmOperationFailed(PzI18n::tr("Could not copy algorithm file for export."));
        return false;
    }
    if (QFile::exists(destPath)) {
        if (!QFile::remove(destPath)) {
            QFile::remove(tmpPath);
            Q_EMIT algorithmOperationFailed(PzI18n::tr("Could not replace existing file at export destination."));
            return false;
        }
    }
    if (!QFile::rename(tmpPath, destPath)) {
        // rename() fails across filesystems — fall back to copy+remove
        if (!QFile::copy(tmpPath, destPath)) {
            QFile::remove(tmpPath);
            Q_EMIT algorithmOperationFailed(PzI18n::tr("Could not write to export destination."));
            return false;
        }
        if (!QFile::remove(tmpPath))
            qCWarning(lcCore) << "Failed to clean up temporary export file:" << tmpPath;
    }
    return true;
}

QString SettingsController::createNewAlgorithm(const QString& name, const QString& baseTemplate,
                                               bool supportsMasterCount, bool supportsSplitRatio,
                                               bool producesOverlappingZones, bool supportsMemory)
{
    // Sanitize name to a filename: lowercase, replace non-alphanumeric (except hyphens) with
    // hyphens, collapse multiple hyphens, strip leading/trailing hyphens
    QString filename = name.trimmed().toLower();
    static const QRegularExpression nonAlnum(QStringLiteral("[^a-z0-9-]"));
    filename.replace(nonAlnum, QStringLiteral("-"));
    static const QRegularExpression multiHyphen(QStringLiteral("-{2,}"));
    filename.replace(multiHyphen, QStringLiteral("-"));
    static const QRegularExpression leadTrailHyphen(QStringLiteral("^-|-$"));
    filename.replace(leadTrailHyphen, QString());
    if (filename.isEmpty())
        filename = QStringLiteral("untitled-algorithm");

    // Build destination path
    const QString destDir = userAlgorithmsDir();
    QDir dir(destDir);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    const QString destPath = findUniqueAlgorithmPath(destDir, filename);
    if (destPath.isEmpty()) {
        qCWarning(lcCore) << "Could not find unique filename for algorithm:" << filename << "— all 999 slots exhausted";
        Q_EMIT algorithmOperationFailed(
            PzI18n::tr("Could not create algorithm — too many files with the same name. "
                       "Please rename or delete existing algorithms."));
        return QString();
    }
    // Update filename to match the final path (may have -N suffix)
    filename = QFileInfo(destPath).completeBaseName();

    // Build JS content
    QString content;

    // SPDX header — use current year and a placeholder author
    const int currentYear = QDate::currentDate().year();
    content +=
        QStringLiteral("// SPDX-FileCopyrightText: ") + QString::number(currentYear) + QStringLiteral(" <your name>\n");
    content += QStringLiteral("// SPDX-License-Identifier: GPL-3.0-or-later\n\n");

    // Metadata annotations — strip newlines to prevent annotation injection
    QString sanitizedDisplayName = name.trimmed();
    sanitizedDisplayName.replace(QLatin1Char('\n'), QLatin1Char(' '));
    sanitizedDisplayName.replace(QLatin1Char('\r'), QLatin1Char(' '));
    content += QStringLiteral("// @name ") + sanitizedDisplayName + QStringLiteral("\n");
    content += QStringLiteral("// @builtinId ") + filename + QStringLiteral("\n");
    content += QStringLiteral("// @description Custom tiling algorithm\n");
    content += QStringLiteral("// @producesOverlappingZones ")
        + (producesOverlappingZones ? QStringLiteral("true") : QStringLiteral("false")) + QStringLiteral("\n");
    content += QStringLiteral("// @supportsMasterCount ")
        + (supportsMasterCount ? QStringLiteral("true") : QStringLiteral("false")) + QStringLiteral("\n");
    content += QStringLiteral("// @supportsSplitRatio ")
        + (supportsSplitRatio ? QStringLiteral("true") : QStringLiteral("false")) + QStringLiteral("\n");
    content += QStringLiteral("// @defaultSplitRatio 0.5\n");
    content += QStringLiteral("// @defaultMaxWindows 6\n");
    content += QStringLiteral("// @minimumWindows 1\n");
    content += QStringLiteral("// @zoneNumberDisplay all\n");
    content += QStringLiteral("// @supportsMemory ")
        + (supportsMemory ? QStringLiteral("true") : QStringLiteral("false")) + QStringLiteral("\n\n");

    // Try to read base template body from system algorithm dirs
    bool foundTemplate = false;
    if (baseTemplate != QLatin1String("blank") && !baseTemplate.isEmpty()) {
        const QString templateFile =
            QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                   QStringLiteral("plasmazones/algorithms/") + baseTemplate + QStringLiteral(".js"));

        if (!templateFile.isEmpty()) {
            QFile file(templateFile);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QString templateContent = QString::fromUtf8(file.readAll());
                file.close();

                // Skip the metadata header: SPDX lines, `// @annotation` lines,
                // `/* ... */` block comments before the first code line, and
                // surrounding blank lines. Stop as soon as we hit a non-metadata line
                // (including doc-comments like `/** ... */` or `// Helper:`) so the
                // template body's own documentation is preserved.
                const QStringList lines = templateContent.split(QLatin1Char('\n'));
                int bodyStart = 0;
                bool inBlockComment = false;
                for (int i = 0; i < lines.size(); ++i) {
                    const QString trimmed = lines[i].trimmed();
                    // Track /* ... */ block comments in the header region
                    if (inBlockComment) {
                        bodyStart = i + 1;
                        if (trimmed.contains(QLatin1String("*/")))
                            inBlockComment = false;
                        continue;
                    }
                    // SPDX headers
                    if (trimmed.startsWith(QLatin1String("// SPDX-"))) {
                        bodyStart = i + 1;
                        continue;
                    }
                    // Metadata annotations (// @name, // @builtinId, etc.)
                    if (trimmed.startsWith(QLatin1String("// @"))) {
                        bodyStart = i + 1;
                        continue;
                    }
                    // Block comment opening in the header — only skip if we haven't
                    // yet passed into the body (avoids stripping doc-comments after metadata).
                    // NOTE: this also matches /** JSDoc */ style comments. We assume bundled
                    // templates do not place JSDoc between metadata annotations.
                    if (trimmed.startsWith(QLatin1String("/*"))) {
                        bodyStart = i + 1;
                        if (!trimmed.contains(QLatin1String("*/")))
                            inBlockComment = true;
                        continue;
                    }
                    // Blank lines between metadata lines are part of the header
                    if (trimmed.isEmpty() && i == bodyStart) {
                        bodyStart = i + 1;
                        continue;
                    }
                    break;
                }

                // Append everything from bodyStart onwards
                if (bodyStart < lines.size()) {
                    for (int i = bodyStart; i < lines.size(); ++i) {
                        content += lines[i];
                        if (i < lines.size() - 1)
                            content += QLatin1Char('\n');
                    }
                    foundTemplate = true;
                }
            }
        }
    }

    if (!foundTemplate) {
        content += QStringLiteral(
            "/**\n"
            " * Custom tiling algorithm.\n"
            " *\n"
            " * @param {Object} params - Tiling parameters\n"
            " * @returns {Array<{x: number, y: number, width: number, height: number}>}\n"
            " */\n"
            "function calculateZones(params) {\n"
            "    if (params.windowCount <= 0) return [];\n"
            "    return fillArea(params.area, params.windowCount);\n"
            "}\n");
    }

    // Write the file
    QFile outFile(destPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(lcCore) << "Failed to write algorithm file:" << destPath;
        Q_EMIT algorithmOperationFailed(
            PzI18n::tr("Could not write algorithm file. Check disk space and permissions."));
        return QString();
    }
    const QByteArray encoded = content.toUtf8();
    const qint64 written = outFile.write(encoded);
    outFile.close();
    if (written < 0 || written != encoded.size()) {
        qCWarning(lcCore) << "Failed to write algorithm content:" << destPath << "written=" << written
                          << "expected=" << encoded.size();
        QFile::remove(destPath);
        Q_EMIT algorithmOperationFailed(
            PzI18n::tr("Could not write algorithm file. Check disk space and permissions."));
        return QString();
    }

    watchForAlgorithmRegistration(filename);
    return filename;
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
    QDBusMessage getReply =
        DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getLayout"), {layoutId});
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
    DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("updateLayout"), {updatedJson});
    scheduleLayoutLoad();
}

void SettingsController::resetEditorDefaults()
{
    m_settings.setEditorDuplicateShortcut(QStringLiteral("Ctrl+D"));
    m_settings.setEditorSplitHorizontalShortcut(QStringLiteral("Ctrl+Shift+H"));
    m_settings.setEditorSplitVerticalShortcut(QStringLiteral("Ctrl+Alt+V"));
    m_settings.setEditorFillShortcut(QStringLiteral("Ctrl+Shift+F"));
    m_settings.setEditorGridSnappingEnabled(true);
    m_settings.setEditorEdgeSnappingEnabled(true);
    m_settings.setEditorSnapIntervalX(0.05);
    m_settings.setEditorSnapIntervalY(0.05);
    m_settings.setEditorSnapOverrideModifier(static_cast<int>(Qt::ShiftModifier));
    m_settings.setFillOnDropEnabled(true);
    m_settings.setFillOnDropModifier(static_cast<int>(Qt::ControlModifier));
    setNeedsSave(true);
}

QVariantMap SettingsController::loadWindowGeometry() const
{
    QSettings settings(QStringLiteral("plasmazones"), QStringLiteral("settings-window"));
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
    QSettings settings(QStringLiteral("plasmazones"), QStringLiteral("settings-window"));
    settings.setValue(QStringLiteral("x"), x);
    settings.setValue(QStringLiteral("y"), y);
    settings.setValue(QStringLiteral("width"), width);
    settings.setValue(QStringLiteral("height"), height);
}

// ═══════════════════════════════════════════════════════════════════════════════
// KZones Import
// ═══════════════════════════════════════════════════════════════════════════════

bool SettingsController::hasKZonesConfig()
{
    const QString kwinrcPath =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kwinrc");
    QSettings kwinrc(kwinrcPath, QSettings::IniFormat);
    kwinrc.beginGroup(QStringLiteral("Script-kzones"));
    bool has = kwinrc.contains(QStringLiteral("layoutsJson"))
        && !kwinrc.value(QStringLiteral("layoutsJson")).toString().trimmed().isEmpty();
    kwinrc.endGroup();
    return has;
}

int SettingsController::importFromKZones()
{
    const QString kwinrcPath =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kwinrc");
    QSettings kwinrc(kwinrcPath, QSettings::IniFormat);
    kwinrc.beginGroup(QStringLiteral("Script-kzones"));
    QString jsonStr = kwinrc.value(QStringLiteral("layoutsJson")).toString();
    kwinrc.endGroup();

    if (jsonStr.isEmpty()) {
        Q_EMIT kzonesImportFinished(0, tr("No KZones configuration found in kwinrc"));
        return 0;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        Q_EMIT kzonesImportFinished(0, tr("Failed to parse KZones layoutsJson: %1").arg(parseError.errorString()));
        return 0;
    }

    int count = importKZonesLayouts(doc.array());
    if (count > 0) {
        scheduleLayoutLoad();
        Q_EMIT kzonesImportFinished(count, tr("Imported %n layout(s) from KZones", "", count));
    } else {
        Q_EMIT kzonesImportFinished(0, tr("No layouts found in KZones configuration"));
    }
    return count;
}

int SettingsController::importFromKZonesFile(const QString& filePath)
{
    if (filePath.isEmpty()) {
        Q_EMIT kzonesImportFinished(0, tr("No file path specified"));
        return 0;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        Q_EMIT kzonesImportFinished(0, tr("Could not open file: %1").arg(filePath));
        return 0;
    }

    QByteArray data = file.readAll();
    file.close();
    // Strip UTF-8 BOM if present (common in Windows-edited files)
    if (data.startsWith("\xEF\xBB\xBF"))
        data.remove(0, 3);

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        Q_EMIT kzonesImportFinished(0, tr("Failed to parse KZones JSON: %1").arg(parseError.errorString()));
        return 0;
    }

    QJsonArray array;
    if (doc.isArray()) {
        array = doc.array();
    } else if (doc.isObject()) {
        // Single layout object — wrap in array
        array.append(doc.object());
    } else {
        Q_EMIT kzonesImportFinished(0, tr("KZones file does not contain a JSON array or object"));
        return 0;
    }

    int count = importKZonesLayouts(array);
    if (count > 0) {
        scheduleLayoutLoad();
        Q_EMIT kzonesImportFinished(count, tr("Imported %n layout(s) from KZones file", "", count));
    } else {
        Q_EMIT kzonesImportFinished(0, tr("No valid layouts found in file"));
    }
    return count;
}

int SettingsController::importKZonesLayouts(const QJsonArray& kzonesArray)
{
    int imported = 0;

    for (const QJsonValue& layoutVal : kzonesArray) {
        if (!layoutVal.isObject())
            continue;

        QJsonObject kzLayout = layoutVal.toObject();

        // Build PlasmaZones layout JSON
        QJsonObject pzLayout;
        pzLayout[QLatin1String(JsonKeys::Id)] = QUuid::createUuid().toString(QUuid::WithBraces);
        pzLayout[QLatin1String(JsonKeys::Name)] =
            kzLayout[QStringLiteral("name")].toString(QStringLiteral("Imported Layout"));
        pzLayout[QLatin1String(JsonKeys::Description)] = QStringLiteral("Imported from KZones");
        pzLayout[QLatin1String(JsonKeys::IsBuiltIn)] = false;
        pzLayout[QLatin1String(JsonKeys::ShowZoneNumbers)] = true;

        int padding = kzLayout[QStringLiteral("padding")].toInt(0);
        if (padding > 0) {
            pzLayout[QLatin1String(JsonKeys::ZonePadding)] = padding;
        }

        // Convert zones — skip layouts with no zones
        const QJsonArray kzZones = kzLayout[QStringLiteral("zones")].toArray();
        if (kzZones.isEmpty())
            continue;

        QJsonArray pzZones;
        QJsonArray appRules;

        for (int i = 0; i < kzZones.size(); ++i) {
            const QJsonObject kzZone = kzZones[i].toObject();

            // Convert 0-100 percentage to 0.0-1.0, clamped to valid range
            double x = qBound(0.0, kzZone[QStringLiteral("x")].toDouble(0) / 100.0, 1.0);
            double y = qBound(0.0, kzZone[QStringLiteral("y")].toDouble(0) / 100.0, 1.0);
            double w = qBound(0.0, kzZone[QStringLiteral("width")].toDouble(50) / 100.0, 1.0);
            double h = qBound(0.0, kzZone[QStringLiteral("height")].toDouble(100) / 100.0, 1.0);

            // Skip zero-area zones
            if (w <= 0.0 || h <= 0.0)
                continue;

            // Use contiguous numbering (skipped zones don't leave gaps)
            int zoneNum = pzZones.size() + 1;

            QJsonObject pzZone;
            pzZone[QLatin1String(JsonKeys::Id)] = QUuid::createUuid().toString(QUuid::WithBraces);
            pzZone[QLatin1String(JsonKeys::ZoneNumber)] = zoneNum;
            pzZone[QLatin1String(JsonKeys::Name)] = QStringLiteral("Zone %1").arg(zoneNum);

            QJsonObject relGeo;
            relGeo[QLatin1String(JsonKeys::X)] = x;
            relGeo[QLatin1String(JsonKeys::Y)] = y;
            relGeo[QLatin1String(JsonKeys::Width)] = w;
            relGeo[QLatin1String(JsonKeys::Height)] = h;
            pzZone[QLatin1String(JsonKeys::RelativeGeometry)] = relGeo;

            pzZones.append(pzZone);

            // Collect per-zone applications into layout-level appRules
            const QJsonArray apps = kzZone[QStringLiteral("applications")].toArray();
            for (const QJsonValue& appVal : apps) {
                QString appClass = appVal.toString().trimmed();
                if (appClass.isEmpty())
                    continue;
                QJsonObject rule;
                rule[QLatin1String(JsonKeys::Pattern)] = appClass;
                rule[QLatin1String(JsonKeys::ZoneNumber)] = zoneNum;
                appRules.append(rule);
            }
        }

        pzLayout[QLatin1String(JsonKeys::Zones)] = pzZones;
        if (!appRules.isEmpty()) {
            pzLayout[QLatin1String(JsonKeys::AppRules)] = appRules;
        }

        // Send to daemon via createLayoutFromJson D-Bus method
        QString layoutJson = QString::fromUtf8(QJsonDocument(pzLayout).toJson(QJsonDocument::Compact));
        QDBusMessage reply = DaemonDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                                                    QStringLiteral("createLayoutFromJson"), {layoutJson});

        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            QString newId = reply.arguments().first().toString();
            if (!newId.isEmpty()) {
                ++imported;
                if (imported == 1) {
                    m_pendingSelectLayoutId = newId;
                }
            }
        }
    }

    return imported;
}

<<<<<<< HEAD
// ── Virtual screen configuration ──────────────────────────────────────────

QStringList SettingsController::getPhysicalScreens() const
{
    QDBusMessage reply = DaemonDBus::callDaemon(QString(DBus::Interface::Screen), QStringLiteral("getPhysicalScreens"));
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        return reply.arguments().first().toStringList();
    }
    return {};
}

QVariantList SettingsController::getVirtualScreenConfig(const QString& physicalScreenId) const
{
    QDBusMessage reply = DaemonDBus::callDaemon(QString(DBus::Interface::Screen),
                                                QStringLiteral("getVirtualScreenConfig"), {physicalScreenId});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString json = reply.arguments().first().toString();
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        if (doc.isObject()) {
            QJsonObject root = doc.object();
            QJsonArray screensArr = root.value(QStringLiteral("screens")).toArray();
            QVariantList result;
            for (const auto& entry : screensArr) {
                QJsonObject screenObj = entry.toObject();
                QJsonObject regionObj = screenObj.value(QLatin1String("region")).toObject();
                QVariantMap screen;
                screen[QStringLiteral("displayName")] = screenObj.value(QLatin1String("displayName")).toString();
                screen[QStringLiteral("x")] = regionObj.value(JsonKeys::X).toDouble();
                screen[QStringLiteral("y")] = regionObj.value(JsonKeys::Y).toDouble();
                screen[QStringLiteral("width")] = regionObj.value(JsonKeys::Width).toDouble();
                screen[QStringLiteral("height")] = regionObj.value(JsonKeys::Height).toDouble();
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
        VirtualScreenDef def = variantMapToVirtualScreenDef(screens[i].toMap(), physicalScreenId, i);
        QJsonObject screenObj;
        screenObj[QLatin1String("index")] = def.index;
        screenObj[QLatin1String("displayName")] = def.displayName;
        screenObj[QLatin1String("region")] = QJsonObject{{JsonKeys::X, def.region.x()},
                                                         {JsonKeys::Y, def.region.y()},
                                                         {JsonKeys::Width, def.region.width()},
                                                         {JsonKeys::Height, def.region.height()}};
        screensArr.append(screenObj);
    }
    root[QLatin1String("screens")] = screensArr;

    QString json = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    DaemonDBus::callDaemon(QString(DBus::Interface::Screen), QStringLiteral("setVirtualScreenConfig"),
                           {physicalScreenId, json});
}

void SettingsController::removeVirtualScreenConfig(const QString& physicalScreenId)
{
    applyVirtualScreenConfig(physicalScreenId, {});
}

void SettingsController::stageVirtualScreenConfig(const QString& physicalScreenId, const QVariantList& screens)
{
    m_stagedVirtualScreenConfigs.insert(physicalScreenId, screens);
    setNeedsSave(true);
}

void SettingsController::stageVirtualScreenRemoval(const QString& physicalScreenId)
{
    m_stagedVirtualScreenConfigs.insert(physicalScreenId, QVariantList()); // empty = remove
    setNeedsSave(true);
}

bool SettingsController::hasUnsavedVirtualScreenConfig(const QString& physicalScreenId) const
{
    return m_stagedVirtualScreenConfigs.contains(physicalScreenId);
}

QVariantList SettingsController::getStagedVirtualScreenConfig(const QString& physicalScreenId) const
{
    return m_stagedVirtualScreenConfigs.value(physicalScreenId);
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
    return applyCustomOrder(m_settings.snappingLayoutOrder(), layoutMap);
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
    return applyCustomOrder(m_settings.tilingAlgorithmOrder(), algoMap);
}

void SettingsController::moveSnappingLayout(int fromIndex, int toIndex)
{
    QVariantList ordered = resolvedSnappingOrder();
    if (fromIndex < 0 || fromIndex >= ordered.size() || toIndex < 0 || toIndex >= ordered.size()
        || fromIndex == toIndex) {
        return;
    }

    // Build the full ID list from the resolved order, then move
    QStringList ids;
    for (const QVariant& v : ordered) {
        ids.append(v.toMap().value(QStringLiteral("id")).toString());
    }
    ids.move(fromIndex, toIndex);
    m_settings.setSnappingLayoutOrder(ids);
    setNeedsSave(true);
}

void SettingsController::moveTilingAlgorithm(int fromIndex, int toIndex)
{
    QVariantList ordered = resolvedTilingOrder();
    if (fromIndex < 0 || fromIndex >= ordered.size() || toIndex < 0 || toIndex >= ordered.size()
        || fromIndex == toIndex) {
        return;
    }

    QStringList ids;
    for (const QVariant& v : ordered) {
        ids.append(v.toMap().value(QStringLiteral("id")).toString());
    }
    ids.move(fromIndex, toIndex);
    m_settings.setTilingAlgorithmOrder(ids);
    setNeedsSave(true);
}

void SettingsController::resetSnappingOrder()
{
    m_settings.setSnappingLayoutOrder({});
    setNeedsSave(true);
}

void SettingsController::resetTilingOrder()
{
    m_settings.setTilingAlgorithmOrder({});
    setNeedsSave(true);
}

} // namespace PlasmaZones
