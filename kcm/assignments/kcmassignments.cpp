// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kcmassignments.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QTimer>
#include "../common/dbusutils.h"
#include "../common/screenhelper.h"
#include "../common/screenprovider.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <KPluginFactory>
#include "../../src/config/settings.h"
#include "../../src/core/constants.h"
#include "../../src/core/utils.h"
#include "../common/assignmentmanager.h"
#include "../common/layoutmanager.h"

K_PLUGIN_CLASS_WITH_JSON(PlasmaZones::KCMAssignments, "kcm_plasmazones_assignments.json")

namespace PlasmaZones {

KCMAssignments::KCMAssignments(QObject* parent, const KPluginMetaData& data)
    : KQuickConfigModule(parent, data)
{
    m_settings = new Settings(this);
    setButtons(Apply | Default);

    m_screenHelper = std::make_unique<ScreenHelper>(m_settings, this);
    connect(m_screenHelper.get(), &ScreenHelper::screensChanged, this, &KCMAssignments::screensChanged);
    connect(m_screenHelper.get(), &ScreenHelper::disabledMonitorsChanged, this,
            &KCMAssignments::disabledMonitorsChanged);
    connect(m_screenHelper.get(), &ScreenHelper::needsSave, this, [this]() {
        setNeedsSave(true);
    });

    // Create layout manager (for combo boxes)
    m_layoutManager = std::make_unique<LayoutManager>(
        m_settings,
        [this]() -> QString {
            auto scr = m_screenHelper->screens();
            if (!scr.isEmpty())
                return scr.first().toMap().value(QStringLiteral("name")).toString();
            return QString();
        },
        this);
    connect(m_layoutManager.get(), &LayoutManager::layoutsChanged, this, &KCMAssignments::layoutsChanged);

    // Create assignment manager
    m_assignmentManager = std::make_unique<AssignmentManager>(
        m_settings,
        [this]() {
            return m_screenHelper->screens();
        },
        this);

    // Forward AssignmentManager signals
    connect(m_assignmentManager.get(), &AssignmentManager::screenAssignmentsChanged, this,
            &KCMAssignments::screenAssignmentsChanged);
    connect(m_assignmentManager.get(), &AssignmentManager::tilingScreenAssignmentsChanged, this,
            &KCMAssignments::tilingScreenAssignmentsChanged);
    connect(m_assignmentManager.get(), &AssignmentManager::tilingActivityAssignmentsChanged, this,
            &KCMAssignments::tilingActivityAssignmentsChanged);
    connect(m_assignmentManager.get(), &AssignmentManager::tilingDesktopAssignmentsChanged, this,
            &KCMAssignments::tilingDesktopAssignmentsChanged);
    connect(m_assignmentManager.get(), &AssignmentManager::assignmentViewModeChanged, this,
            &KCMAssignments::assignmentViewModeChanged);
    connect(m_assignmentManager.get(), &AssignmentManager::quickLayoutSlotsChanged, this,
            &KCMAssignments::quickLayoutSlotsChanged);
    connect(m_assignmentManager.get(), &AssignmentManager::tilingQuickLayoutSlotsChanged, this,
            &KCMAssignments::tilingQuickLayoutSlotsChanged);
    connect(m_assignmentManager.get(), &AssignmentManager::activityAssignmentsChanged, this,
            &KCMAssignments::activityAssignmentsChanged);
    connect(m_assignmentManager.get(), &AssignmentManager::appRulesRefreshed, this, &KCMAssignments::appRulesRefreshed);
    connect(m_assignmentManager.get(), &AssignmentManager::needsSave, this, [this]() {
        setNeedsSave(true);
    });
    connect(m_assignmentManager.get(), &AssignmentManager::refreshScreensRequested, this,
            &KCMAssignments::refreshScreens);

    m_screenHelper->refreshScreens();
    refreshVirtualDesktops();
    refreshActivities();

    // Listen for layout changes from the daemon
    m_layoutManager->connectToDaemonSignals();

    // Listen for screen changes
    m_screenHelper->connectToDaemonSignals();

    // Listen for screen layout assignment changes (routed to AssignmentManager)
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::LayoutManager),
                                          QStringLiteral("screenLayoutChanged"), m_assignmentManager.get(),
                                          SLOT(onScreenLayoutChanged(QString, QString, int)));

    // Listen for quick layout slot changes
    QDBusConnection::sessionBus().connect(
        QString(DBus::ServiceName), QString(DBus::ObjectPath), QString(DBus::Interface::LayoutManager),
        QStringLiteral("quickLayoutSlotsChanged"), m_assignmentManager.get(), SLOT(onQuickLayoutSlotsChanged()));

    // Listen for virtual desktop count changes
    QDBusConnection::sessionBus().connect(
        QString(DBus::ServiceName), QString(DBus::ObjectPath), QString(DBus::Interface::LayoutManager),
        QStringLiteral("virtualDesktopCountChanged"), this, SLOT(refreshVirtualDesktops()));

    // Listen for KDE Activities changes
    QDBusConnection::sessionBus().connect(
        QString(DBus::ServiceName), QString(DBus::ObjectPath), QString(DBus::Interface::LayoutManager),
        QStringLiteral("currentActivityChanged"), this, SLOT(onCurrentActivityChanged(QString)));
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::LayoutManager), QStringLiteral("activitiesChanged"),
                                          this, SLOT(onActivitiesChanged()));

    // Reload when another sub-KCM or process saves settings
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::Settings), QStringLiteral("settingsChanged"), this,
                                          SLOT(onExternalSettingsChanged()));
}

KCMAssignments::~KCMAssignments() = default;

// ── Load / Save / Defaults ───────────────────────────────────────────────

void KCMAssignments::load()
{
    KQuickConfigModule::load();
    m_settings->load();
    m_assignmentManager->load();
    m_layoutManager->loadSync();
    m_screenHelper->refreshScreens();
    refreshVirtualDesktops();
    refreshActivities();
    emitAllChanged();
    setNeedsSave(false);
}

void KCMAssignments::save()
{
    m_saving = true;
    m_layoutManager->setSaveInProgress(true);

    m_settings->save();

    QStringList failedOperations;
    m_assignmentManager->save(failedOperations);

    if (!failedOperations.isEmpty()) {
        qWarning() << "Failed operations during save:" << failedOperations;
    }

    KCMDBus::notifyReload();

    m_layoutManager->setSaveInProgress(false);
    m_layoutManager->loadSync();

    KQuickConfigModule::save();
    setNeedsSave(false);
    QTimer::singleShot(0, this, [this]() {
        m_saving = false;
    });
}

void KCMAssignments::onExternalSettingsChanged()
{
    if (!m_saving) {
        load();
    }
}

void KCMAssignments::defaults()
{
    KQuickConfigModule::defaults();
    m_assignmentManager->resetToDefaults();
    setNeedsSave(true);
}

// ── Assignment view mode ─────────────────────────────────────────────────

int KCMAssignments::assignmentViewMode() const
{
    return m_assignmentManager->assignmentViewMode();
}

void KCMAssignments::setAssignmentViewMode(int mode)
{
    m_assignmentManager->setAssignmentViewMode(mode);
}

// ── Layout list ──────────────────────────────────────────────────────────

QVariantList KCMAssignments::layouts() const
{
    return m_layoutManager->layouts();
}

QString KCMAssignments::defaultLayoutId() const
{
    return m_settings->defaultLayoutId();
}

bool KCMAssignments::autotileEnabled() const
{
    return m_settings->autotileEnabled();
}

QString KCMAssignments::autotileAlgorithm() const
{
    return m_settings->autotileAlgorithm();
}

// ── Screens ──────────────────────────────────────────────────────────────

QVariantList KCMAssignments::screens() const
{
    return m_screenHelper->screens();
}

// ── Virtual desktops ─────────────────────────────────────────────────────

int KCMAssignments::virtualDesktopCount() const
{
    return m_virtualDesktopCount;
}

QStringList KCMAssignments::virtualDesktopNames() const
{
    return m_virtualDesktopNames;
}

// ── KDE Activities ───────────────────────────────────────────────────────

bool KCMAssignments::activitiesAvailable() const
{
    return m_activitiesAvailable;
}

QVariantList KCMAssignments::activities() const
{
    return m_activities;
}

QString KCMAssignments::currentActivity() const
{
    return m_currentActivity;
}

// ── Disabled monitors ────────────────────────────────────────────────────

QStringList KCMAssignments::disabledMonitors() const
{
    return m_settings->disabledMonitors();
}

// ── Screen assignments (snapping) — delegated to AssignmentManager ──────

void KCMAssignments::assignLayoutToScreen(const QString& screenName, const QString& layoutId)
{
    m_assignmentManager->assignLayoutToScreen(screenName, layoutId);
}

void KCMAssignments::clearScreenAssignment(const QString& screenName)
{
    m_assignmentManager->clearScreenAssignment(screenName);
}

QString KCMAssignments::getLayoutForScreen(const QString& screenName) const
{
    return m_assignmentManager->getLayoutForScreen(screenName);
}

// ── Tiling screen assignments ────────────────────────────────────────────

void KCMAssignments::assignTilingLayoutToScreen(const QString& screenName, const QString& layoutId)
{
    m_assignmentManager->assignTilingLayoutToScreen(screenName, layoutId);
}

void KCMAssignments::clearTilingScreenAssignment(const QString& screenName)
{
    m_assignmentManager->clearTilingScreenAssignment(screenName);
}

QString KCMAssignments::getTilingLayoutForScreen(const QString& screenName) const
{
    return m_assignmentManager->getTilingLayoutForScreen(screenName);
}

// ── Per-desktop screen assignments (snapping) ────────────────────────────

void KCMAssignments::assignLayoutToScreenDesktop(const QString& screenName, int virtualDesktop, const QString& layoutId)
{
    m_assignmentManager->assignLayoutToScreenDesktop(screenName, virtualDesktop, layoutId);
}

void KCMAssignments::clearScreenDesktopAssignment(const QString& screenName, int virtualDesktop)
{
    m_assignmentManager->clearScreenDesktopAssignment(screenName, virtualDesktop);
}

QString KCMAssignments::getLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    return m_assignmentManager->getLayoutForScreenDesktop(screenName, virtualDesktop);
}

QString KCMAssignments::getSnappingLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    return m_assignmentManager->getSnappingLayoutForScreenDesktop(screenName, virtualDesktop);
}

bool KCMAssignments::hasExplicitAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    return m_assignmentManager->hasExplicitAssignmentForScreenDesktop(screenName, virtualDesktop);
}

// ── Tiling per-desktop screen assignments ────────────────────────────────

void KCMAssignments::assignTilingLayoutToScreenDesktop(const QString& screenName, int virtualDesktop,
                                                       const QString& layoutId)
{
    m_assignmentManager->assignTilingLayoutToScreenDesktop(screenName, virtualDesktop, layoutId);
}

void KCMAssignments::clearTilingScreenDesktopAssignment(const QString& screenName, int virtualDesktop)
{
    m_assignmentManager->clearTilingScreenDesktopAssignment(screenName, virtualDesktop);
}

QString KCMAssignments::getTilingLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    return m_assignmentManager->getTilingLayoutForScreenDesktop(screenName, virtualDesktop);
}

bool KCMAssignments::hasExplicitTilingAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    return m_assignmentManager->hasExplicitTilingAssignmentForScreenDesktop(screenName, virtualDesktop);
}

// ── Per-activity screen assignments (snapping) ───────────────────────────

void KCMAssignments::assignLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                                  const QString& layoutId)
{
    m_assignmentManager->assignLayoutToScreenActivity(screenName, activityId, layoutId);
}

void KCMAssignments::clearScreenActivityAssignment(const QString& screenName, const QString& activityId)
{
    m_assignmentManager->clearScreenActivityAssignment(screenName, activityId);
}

QString KCMAssignments::getLayoutForScreenActivity(const QString& screenName, const QString& activityId) const
{
    return m_assignmentManager->getLayoutForScreenActivity(screenName, activityId);
}

QString KCMAssignments::getSnappingLayoutForScreenActivity(const QString& screenName, const QString& activityId) const
{
    return m_assignmentManager->getSnappingLayoutForScreenActivity(screenName, activityId);
}

bool KCMAssignments::hasExplicitAssignmentForScreenActivity(const QString& screenName, const QString& activityId) const
{
    return m_assignmentManager->hasExplicitAssignmentForScreenActivity(screenName, activityId);
}

// ── Tiling per-activity screen assignments ───────────────────────────────

void KCMAssignments::assignTilingLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                                        const QString& layoutId)
{
    m_assignmentManager->assignTilingLayoutToScreenActivity(screenName, activityId, layoutId);
}

void KCMAssignments::clearTilingScreenActivityAssignment(const QString& screenName, const QString& activityId)
{
    m_assignmentManager->clearTilingScreenActivityAssignment(screenName, activityId);
}

QString KCMAssignments::getTilingLayoutForScreenActivity(const QString& screenName, const QString& activityId) const
{
    return m_assignmentManager->getTilingLayoutForScreenActivity(screenName, activityId);
}

bool KCMAssignments::hasExplicitTilingAssignmentForScreenActivity(const QString& screenName,
                                                                  const QString& activityId) const
{
    return m_assignmentManager->hasExplicitTilingAssignmentForScreenActivity(screenName, activityId);
}

// ── Monitor disable ──────────────────────────────────────────────────────

bool KCMAssignments::isMonitorDisabled(const QString& screenName) const
{
    return m_screenHelper->isMonitorDisabled(screenName);
}

void KCMAssignments::setMonitorDisabled(const QString& screenName, bool disabled)
{
    m_screenHelper->setMonitorDisabled(screenName, disabled);
}

// ── Quick layout slots ───────────────────────────────────────────────────

QString KCMAssignments::getQuickLayoutSlot(int slotNumber) const
{
    return m_assignmentManager->getQuickLayoutSlot(slotNumber);
}

void KCMAssignments::setQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    m_assignmentManager->setQuickLayoutSlot(slotNumber, layoutId);
}

QString KCMAssignments::getQuickLayoutShortcut(int slotNumber) const
{
    return m_assignmentManager->getQuickLayoutShortcut(slotNumber);
}

QString KCMAssignments::getTilingQuickLayoutSlot(int slotNumber) const
{
    return m_assignmentManager->getTilingQuickLayoutSlot(slotNumber);
}

void KCMAssignments::setTilingQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    m_assignmentManager->setTilingQuickLayoutSlot(slotNumber, layoutId);
}

// ── App-to-zone rules ────────────────────────────────────────────────────

QVariantList KCMAssignments::getAppRulesForLayout(const QString& layoutId) const
{
    return m_assignmentManager->getAppRulesForLayout(layoutId);
}

void KCMAssignments::setAppRulesForLayout(const QString& layoutId, const QVariantList& rules)
{
    m_assignmentManager->setAppRulesForLayout(layoutId, rules);
}

void KCMAssignments::addAppRuleToLayout(const QString& layoutId, const QString& pattern, int zoneNumber,
                                        const QString& targetScreen)
{
    m_assignmentManager->addAppRuleToLayout(layoutId, pattern, zoneNumber, targetScreen);
}

void KCMAssignments::removeAppRuleFromLayout(const QString& layoutId, int index)
{
    m_assignmentManager->removeAppRuleFromLayout(layoutId, index);
}

// ── Running windows ──────────────────────────────────────────────────

QVariantList KCMAssignments::getRunningWindows() const
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

// ── Refresh helpers ──────────────────────────────────────────────────────

void KCMAssignments::refreshScreens()
{
    m_screenHelper->refreshScreens();
}

void KCMAssignments::refreshVirtualDesktops()
{
    int newCount = 1;
    QStringList newNames;

    QDBusMessage countReply =
        KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getVirtualDesktopCount"));
    if (countReply.type() == QDBusMessage::ReplyMessage && !countReply.arguments().isEmpty()) {
        newCount = countReply.arguments().first().toInt();
        if (newCount < 1)
            newCount = 1;
    }

    QDBusMessage namesReply =
        KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getVirtualDesktopNames"));
    if (namesReply.type() == QDBusMessage::ReplyMessage && !namesReply.arguments().isEmpty()) {
        newNames = namesReply.arguments().first().toStringList();
    }

    if (newNames.isEmpty()) {
        for (int i = 1; i <= newCount; ++i)
            newNames.append(QStringLiteral("Desktop %1").arg(i));
    }

    if (m_virtualDesktopCount != newCount) {
        m_virtualDesktopCount = newCount;
        Q_EMIT virtualDesktopCountChanged();
    }
    if (m_virtualDesktopNames != newNames) {
        m_virtualDesktopNames = newNames;
        Q_EMIT virtualDesktopNamesChanged();
    }
}

void KCMAssignments::refreshActivities()
{
    bool wasAvailable = m_activitiesAvailable;
    QVariantList oldActivities = m_activities;
    QString oldCurrentActivity = m_currentActivity;

    QDBusMessage availReply =
        KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("isActivitiesAvailable"));
    if (availReply.type() == QDBusMessage::ReplyMessage && !availReply.arguments().isEmpty()) {
        m_activitiesAvailable = availReply.arguments().first().toBool();
    } else {
        m_activitiesAvailable = false;
    }

    if (m_activitiesAvailable) {
        QDBusMessage activitiesReply =
            KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getAllActivitiesInfo"));
        if (activitiesReply.type() == QDBusMessage::ReplyMessage && !activitiesReply.arguments().isEmpty()) {
            QString jsonStr = activitiesReply.arguments().first().toString();
            QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
            if (doc.isArray()) {
                m_activities.clear();
                const QJsonArray arr = doc.array();
                for (const QJsonValue& val : arr) {
                    QJsonObject obj = val.toObject();
                    QVariantMap activity;
                    activity[QStringLiteral("id")] = obj[QLatin1String("id")].toString();
                    activity[QStringLiteral("name")] = obj[QLatin1String("name")].toString();
                    activity[QStringLiteral("icon")] = obj[QLatin1String("icon")].toString();
                    m_activities.append(activity);
                }
            }
        }

        QDBusMessage currentReply =
            KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getCurrentActivity"));
        if (currentReply.type() == QDBusMessage::ReplyMessage && !currentReply.arguments().isEmpty()) {
            m_currentActivity = currentReply.arguments().first().toString();
        }
    } else {
        m_activities.clear();
        m_currentActivity.clear();
    }

    if (wasAvailable != m_activitiesAvailable)
        Q_EMIT activitiesAvailableChanged();
    if (oldActivities != m_activities)
        Q_EMIT activitiesChanged();
    if (oldCurrentActivity != m_currentActivity)
        Q_EMIT currentActivityChanged();
}

void KCMAssignments::onCurrentActivityChanged(const QString& activityId)
{
    if (m_currentActivity != activityId) {
        m_currentActivity = activityId;
        Q_EMIT currentActivityChanged();
    }
}

void KCMAssignments::onActivitiesChanged()
{
    refreshActivities();
}

// ── Helpers ──────────────────────────────────────────────────────────────

void KCMAssignments::emitAllChanged()
{
    Q_EMIT assignmentViewModeChanged();
    Q_EMIT layoutsChanged();
    Q_EMIT defaultLayoutIdChanged();
    Q_EMIT autotileEnabledChanged();
    Q_EMIT autotileAlgorithmChanged();
    Q_EMIT screensChanged();
    Q_EMIT virtualDesktopCountChanged();
    Q_EMIT virtualDesktopNamesChanged();
    Q_EMIT activitiesAvailableChanged();
    Q_EMIT activitiesChanged();
    Q_EMIT currentActivityChanged();
    Q_EMIT disabledMonitorsChanged();
    Q_EMIT screenAssignmentsChanged();
    Q_EMIT tilingScreenAssignmentsChanged();
    Q_EMIT tilingActivityAssignmentsChanged();
    Q_EMIT tilingDesktopAssignmentsChanged();
    Q_EMIT quickLayoutSlotsChanged();
    Q_EMIT tilingQuickLayoutSlotsChanged();
    Q_EMIT activityAssignmentsChanged();
    Q_EMIT appRulesRefreshed();
}

} // namespace PlasmaZones

#include "kcmassignments.moc"
