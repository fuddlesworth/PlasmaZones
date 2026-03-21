// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingscontroller.h"
#include "../core/constants.h"
#include "../core/logging.h"
#include "../config/configbackend_qsettings.h"
#include "../../kcm/common/dbusutils.h"

#include "../autotile/AlgorithmRegistry.h"
#include "../autotile/TilingAlgorithm.h"
#include "../autotile/TilingState.h"

#include "../config/configdefaults.h"

#include <QDBusMessage>
#include <QFile>
#include <QFontDatabase>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDesktopServices>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace PlasmaZones {

SettingsController::SettingsController(QObject* parent)
    : QObject(parent)
    , m_screenHelper(&m_settings, this)
{
    // Listen for external settings changes from the daemon
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::Settings), QStringLiteral("settingsChanged"), this,
                                          SLOT(onExternalSettingsChanged()));

    // Forward daemon running state changes
    connect(&m_daemonController, &DaemonController::runningChanged, this, &SettingsController::daemonRunningChanged);

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
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::LayoutManager), QStringLiteral("layoutUpdated"),
                                          this, SLOT(loadLayoutsAsync()));

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

    // Create editor config backend
    m_editorConfig = createDefaultConfigBackend();

    // Initial loads
    loadEditorSettings();
    scheduleLayoutLoad();
    refreshVirtualDesktops();
    refreshActivities();
}

void SettingsController::setActivePage(const QString& page)
{
    if (m_activePage != page) {
        m_activePage = page;
        Q_EMIT activePageChanged();
    }
}

void SettingsController::load()
{
    m_settings.load();
    loadEditorSettings();
    m_screenHelper.refreshScreens();
    scheduleLayoutLoad();
    setNeedsSave(false);
}

void SettingsController::save()
{
    m_saving = true;

    // Save main settings
    m_settings.save();

    // Save editor settings
    saveEditorSettings();

    // Notify daemon to reload settings (synchronous to avoid race)
    KCMDBus::notifyReload();

    // Safe to clear immediately: notifyReload() is synchronous, so the daemon
    // has already processed the reload and emitted settingsChanged before we
    // reach this line. No deferred reset needed.
    m_saving = false;

    setNeedsSave(false);
}

void SettingsController::defaults()
{
    m_settings.reset();
    m_settings.load();

    resetEditorDefaults();

    setNeedsSave(true);
}

void SettingsController::launchEditor()
{
    QProcess::startDetached(QStringLiteral("plasmazones-editor"), {});
}

void SettingsController::onSettingsPropertyChanged()
{
    if (!m_saving) {
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
    });
}

void SettingsController::createNewLayout()
{
    QDBusMessage reply = KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("createLayout"),
                                             {QStringLiteral("New Layout"), QStringLiteral("custom")});

    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        QString newLayoutId = reply.arguments().first().toString();
        if (!newLayoutId.isEmpty()) {
            editLayout(newLayoutId);
        }
    }
    scheduleLayoutLoad();
}

void SettingsController::deleteLayout(const QString& layoutId)
{
    QDBusMessage reply =
        KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("deleteLayout"), {layoutId});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcCore) << "deleteLayout failed:" << reply.errorMessage();
    }
    scheduleLayoutLoad();
}

void SettingsController::duplicateLayout(const QString& layoutId)
{
    KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("duplicateLayout"), {layoutId});
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
// Assignment helpers (D-Bus to daemon LayoutManager)
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::assignLayoutToScreen(const QString& screenName, const QString& layoutId)
{
    KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("assignLayoutByIdToScreen"),
                        {layoutId, screenName, 0, QString()});
    KCMDBus::notifyReload();
}

void SettingsController::clearScreenAssignment(const QString& screenName)
{
    KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("clearAssignment"),
                        {screenName, 0, QString()});
    KCMDBus::notifyReload();
}

void SettingsController::assignTilingLayoutToScreen(const QString& screenName, const QString& layoutId)
{
    // Use setAssignmentEntry with mode=1 (Autotile) and the algorithm extracted
    // from the layoutId, matching KCM's AssignmentManager behavior.
    const QString algoId = LayoutId::extractAlgorithmId(layoutId);
    KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("setAssignmentEntry"),
                        {screenName, 0, QString(), 1 /* Autotile */, QString() /* preserve snapping */, algoId});
    KCMDBus::notifyReload();
}

void SettingsController::clearTilingScreenAssignment(const QString& screenName)
{
    KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("clearAssignment"),
                        {screenName, 0, QString()});
    KCMDBus::notifyReload();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Assignment query helpers (D-Bus to daemon)
// ═══════════════════════════════════════════════════════════════════════════════

QString SettingsController::getLayoutForScreen(const QString& screenName) const
{
    QDBusMessage reply = KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                                             QStringLiteral("getLayoutForScreen"), {screenName});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

QString SettingsController::getTilingLayoutForScreen(const QString& screenName) const
{
    QDBusMessage reply = KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                                             QStringLiteral("getTilingAlgorithmForScreenDesktop"), {screenName, 0});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().toString();
    return {};
}

// ═══════════════════════════════════════════════════════════════════════════════
// Assignment lock helpers
// ═══════════════════════════════════════════════════════════════════════════════

static QString lockKey(const QString& screenName, int mode)
{
    return QString::number(mode) + QStringLiteral(":") + screenName;
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

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual desktops / activities (D-Bus queries to daemon)
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::refreshVirtualDesktops()
{
    QDBusMessage countReply =
        KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getVirtualDesktopCount"));
    if (countReply.type() == QDBusMessage::ReplyMessage && !countReply.arguments().isEmpty()) {
        m_virtualDesktopCount = countReply.arguments().first().toInt();
    }

    QDBusMessage namesReply =
        KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getVirtualDesktopNames"));
    if (namesReply.type() == QDBusMessage::ReplyMessage && !namesReply.arguments().isEmpty()) {
        m_virtualDesktopNames = namesReply.arguments().first().toStringList();
    }
}

void SettingsController::refreshActivities()
{
    QDBusMessage availReply =
        KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("isActivitiesAvailable"));
    if (availReply.type() == QDBusMessage::ReplyMessage && !availReply.arguments().isEmpty()) {
        m_activitiesAvailable = availReply.arguments().first().toBool();
    }

    if (m_activitiesAvailable) {
        QDBusMessage infoReply =
            KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getAllActivitiesInfo"));
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
            KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getCurrentActivity"));
        if (currentReply.type() == QDBusMessage::ReplyMessage && !currentReply.arguments().isEmpty()) {
            m_currentActivity = currentReply.arguments().first().toString();
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Editor settings (read/write via IConfigBackend for [Editor] group)
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::loadEditorSettings()
{
    if (!m_editorConfig)
        return;

    m_editorConfig->reparseConfiguration();
    auto group = m_editorConfig->group(QStringLiteral("Editor"));

    m_editorDuplicateShortcut = group->readString(QStringLiteral("EditorDuplicateShortcut"), QStringLiteral("Ctrl+D"));
    m_editorSplitHorizontalShortcut =
        group->readString(QStringLiteral("EditorSplitHorizontalShortcut"), QStringLiteral("Ctrl+Shift+H"));
    m_editorSplitVerticalShortcut =
        group->readString(QStringLiteral("EditorSplitVerticalShortcut"), QStringLiteral("Ctrl+Alt+V"));
    m_editorFillShortcut = group->readString(QStringLiteral("EditorFillShortcut"), QStringLiteral("Ctrl+Shift+F"));
    m_editorGridSnappingEnabled = group->readBool(QStringLiteral("GridSnappingEnabled"), true);
    m_editorEdgeSnappingEnabled = group->readBool(QStringLiteral("EdgeSnappingEnabled"), true);

    double intervalX = group->readDouble(QStringLiteral("SnapIntervalX"), -1.0);
    if (intervalX < 0.0)
        intervalX = group->readDouble(QStringLiteral("SnapInterval"), 0.05);
    m_editorSnapIntervalX = intervalX;

    double intervalY = group->readDouble(QStringLiteral("SnapIntervalY"), -1.0);
    if (intervalY < 0.0)
        intervalY = group->readDouble(QStringLiteral("SnapInterval"), 0.05);
    m_editorSnapIntervalY = intervalY;

    Q_EMIT editorDuplicateShortcutChanged();
    Q_EMIT editorSplitHorizontalShortcutChanged();
    Q_EMIT editorSplitVerticalShortcutChanged();
    Q_EMIT editorFillShortcutChanged();
    Q_EMIT editorGridSnappingEnabledChanged();
    Q_EMIT editorEdgeSnappingEnabledChanged();
    m_editorSnapOverrideModifier = group->readInt(QStringLiteral("SnapOverrideModifier"), 1);
    m_fillOnDropEnabled = group->readBool(QStringLiteral("FillOnDropEnabled"), true);
    m_fillOnDropModifier = group->readInt(QStringLiteral("FillOnDropModifier"), 2);

    Q_EMIT editorSnapIntervalXChanged();
    Q_EMIT editorSnapIntervalYChanged();
    Q_EMIT editorSnapOverrideModifierChanged();
    Q_EMIT fillOnDropEnabledChanged();
    Q_EMIT fillOnDropModifierChanged();
}

void SettingsController::saveEditorSettings()
{
    if (!m_editorConfig)
        return;

    auto group = m_editorConfig->group(QStringLiteral("Editor"));

    group->writeString(QStringLiteral("EditorDuplicateShortcut"), m_editorDuplicateShortcut);
    group->writeString(QStringLiteral("EditorSplitHorizontalShortcut"), m_editorSplitHorizontalShortcut);
    group->writeString(QStringLiteral("EditorSplitVerticalShortcut"), m_editorSplitVerticalShortcut);
    group->writeString(QStringLiteral("EditorFillShortcut"), m_editorFillShortcut);
    group->writeBool(QStringLiteral("GridSnappingEnabled"), m_editorGridSnappingEnabled);
    group->writeBool(QStringLiteral("EdgeSnappingEnabled"), m_editorEdgeSnappingEnabled);
    group->writeDouble(QStringLiteral("SnapIntervalX"), m_editorSnapIntervalX);
    group->writeDouble(QStringLiteral("SnapIntervalY"), m_editorSnapIntervalY);
    group->writeInt(QStringLiteral("SnapOverrideModifier"), m_editorSnapOverrideModifier);
    group->writeBool(QStringLiteral("FillOnDropEnabled"), m_fillOnDropEnabled);
    group->writeInt(QStringLiteral("FillOnDropModifier"), m_fillOnDropModifier);

    m_editorConfig->sync();
}

// Editor getters

QString SettingsController::editorDuplicateShortcut() const
{
    return m_editorDuplicateShortcut;
}
QString SettingsController::editorSplitHorizontalShortcut() const
{
    return m_editorSplitHorizontalShortcut;
}
QString SettingsController::editorSplitVerticalShortcut() const
{
    return m_editorSplitVerticalShortcut;
}
QString SettingsController::editorFillShortcut() const
{
    return m_editorFillShortcut;
}
bool SettingsController::editorGridSnappingEnabled() const
{
    return m_editorGridSnappingEnabled;
}
bool SettingsController::editorEdgeSnappingEnabled() const
{
    return m_editorEdgeSnappingEnabled;
}
qreal SettingsController::editorSnapIntervalX() const
{
    return m_editorSnapIntervalX;
}
qreal SettingsController::editorSnapIntervalY() const
{
    return m_editorSnapIntervalY;
}

// Editor setters

void SettingsController::setEditorDuplicateShortcut(const QString& shortcut)
{
    if (m_editorDuplicateShortcut != shortcut) {
        m_editorDuplicateShortcut = shortcut;
        Q_EMIT editorDuplicateShortcutChanged();
        setNeedsSave(true);
    }
}

void SettingsController::setEditorSplitHorizontalShortcut(const QString& shortcut)
{
    if (m_editorSplitHorizontalShortcut != shortcut) {
        m_editorSplitHorizontalShortcut = shortcut;
        Q_EMIT editorSplitHorizontalShortcutChanged();
        setNeedsSave(true);
    }
}

void SettingsController::setEditorSplitVerticalShortcut(const QString& shortcut)
{
    if (m_editorSplitVerticalShortcut != shortcut) {
        m_editorSplitVerticalShortcut = shortcut;
        Q_EMIT editorSplitVerticalShortcutChanged();
        setNeedsSave(true);
    }
}

void SettingsController::setEditorFillShortcut(const QString& shortcut)
{
    if (m_editorFillShortcut != shortcut) {
        m_editorFillShortcut = shortcut;
        Q_EMIT editorFillShortcutChanged();
        setNeedsSave(true);
    }
}

void SettingsController::setEditorGridSnappingEnabled(bool enabled)
{
    if (m_editorGridSnappingEnabled != enabled) {
        m_editorGridSnappingEnabled = enabled;
        Q_EMIT editorGridSnappingEnabledChanged();
        setNeedsSave(true);
    }
}

void SettingsController::setEditorEdgeSnappingEnabled(bool enabled)
{
    if (m_editorEdgeSnappingEnabled != enabled) {
        m_editorEdgeSnappingEnabled = enabled;
        Q_EMIT editorEdgeSnappingEnabledChanged();
        setNeedsSave(true);
    }
}

void SettingsController::setEditorSnapIntervalX(qreal interval)
{
    interval = qBound(0.01, interval, 1.0);
    if (!qFuzzyCompare(m_editorSnapIntervalX, interval)) {
        m_editorSnapIntervalX = interval;
        Q_EMIT editorSnapIntervalXChanged();
        setNeedsSave(true);
    }
}

void SettingsController::setEditorSnapIntervalY(qreal interval)
{
    interval = qBound(0.01, interval, 1.0);
    if (!qFuzzyCompare(m_editorSnapIntervalY, interval)) {
        m_editorSnapIntervalY = interval;
        Q_EMIT editorSnapIntervalYChanged();
        setNeedsSave(true);
    }
}

int SettingsController::editorSnapOverrideModifier() const
{
    return m_editorSnapOverrideModifier;
}
bool SettingsController::fillOnDropEnabled() const
{
    return m_fillOnDropEnabled;
}
int SettingsController::fillOnDropModifier() const
{
    return m_fillOnDropModifier;
}

void SettingsController::setEditorSnapOverrideModifier(int mod)
{
    if (m_editorSnapOverrideModifier != mod) {
        m_editorSnapOverrideModifier = mod;
        Q_EMIT editorSnapOverrideModifierChanged();
        setNeedsSave(true);
    }
}

void SettingsController::setFillOnDropEnabled(bool enabled)
{
    if (m_fillOnDropEnabled != enabled) {
        m_fillOnDropEnabled = enabled;
        Q_EMIT fillOnDropEnabledChanged();
        setNeedsSave(true);
    }
}

void SettingsController::setFillOnDropModifier(int mod)
{
    if (m_fillOnDropModifier != mod) {
        m_fillOnDropModifier = mod;
        Q_EMIT fillOnDropModifierChanged();
        setNeedsSave(true);
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Virtual desktop / activity D-Bus signal handlers
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsController::onVirtualDesktopsChanged()
{
    refreshVirtualDesktops();
    Q_EMIT virtualDesktopsChanged();
}

void SettingsController::onActivitiesChanged()
{
    refreshActivities();
    Q_EMIT activitiesChanged();
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
        converted[QStringLiteral("modifier")] =
            ModifierUtils::dragModifierToBitmask(map.value(QStringLiteral("modifier"), 0).toInt());
        converted[QStringLiteral("mouseButton")] = map.value(QStringLiteral("mouseButton"), 0);
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
        stored[QStringLiteral("modifier")] =
            ModifierUtils::bitmaskToDragModifier(map.value(QStringLiteral("modifier"), 0).toInt());
        stored[QStringLiteral("mouseButton")] = map.value(QStringLiteral("mouseButton"), 0);
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
        if (t.toMap().value(QStringLiteral("modifier"), 0).toInt() == alwaysActive) {
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
        trigger[QStringLiteral("modifier")] = static_cast<int>(DragModifier::AlwaysActive);
        trigger[QStringLiteral("mouseButton")] = 0;
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
    QDBusMessage reply = KCMDBus::callDaemon(QString(DBus::Interface::Settings), QStringLiteral("getRunningWindows"));
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
            algoMap[QStringLiteral("defaultMaxWindows")] = algo->defaultMaxWindows();
            algorithms.append(algoMap);
        }
    }
    return algorithms;
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
    QVector<QRect> zones = algo->calculateZones({count, previewRect, &state, 0, {}});

    return AlgorithmRegistry::zonesToRelativeGeometry(zones, previewRect);
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

void SettingsController::resetEditorDefaults()
{
    setEditorDuplicateShortcut(QStringLiteral("Ctrl+D"));
    setEditorSplitHorizontalShortcut(QStringLiteral("Ctrl+Shift+H"));
    setEditorSplitVerticalShortcut(QStringLiteral("Ctrl+Alt+V"));
    setEditorFillShortcut(QStringLiteral("Ctrl+Shift+F"));
    setEditorGridSnappingEnabled(true);
    setEditorEdgeSnappingEnabled(true);
    setEditorSnapIntervalX(0.05);
    setEditorSnapIntervalY(0.05);
    setEditorSnapOverrideModifier(1);
    setFillOnDropEnabled(true);
    setFillOnDropModifier(2);
}

} // namespace PlasmaZones
