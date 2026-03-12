// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "assignmentmanager.h"
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include "dbusutils.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <KConfigGroup>
#include <KGlobalAccel>
#include <KSharedConfig>
#include "../../src/config/settings.h"
#include "../../src/core/constants.h"
#include "../../src/core/layout.h"
#include "../../src/core/logging.h"
#include "../../src/core/utils.h"
#include "../../src/autotile/AlgorithmRegistry.h"

namespace PlasmaZones {

AssignmentManager::AssignmentManager(Settings* settings, ScreenListProvider screenListProvider, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_screenListProvider(std::move(screenListProvider))
{
}

// ═══════════════════════════════════════════════════════════════════════════════
// Screen assignments (snapping)
// ═══════════════════════════════════════════════════════════════════════════════

void AssignmentManager::assignLayoutToScreen(const QString& screenName, const QString& layoutId)
{
    QString oldLayoutId = m_screenAssignments.value(screenName).toString();
    if (oldLayoutId != layoutId) {
        if (layoutId.isEmpty()) {
            m_screenAssignments.remove(screenName);
        } else {
            m_screenAssignments[screenName] = layoutId;
        }
        Q_EMIT screenAssignmentsChanged();
        Q_EMIT needsSave();
    }
}

void AssignmentManager::clearScreenAssignment(const QString& screenName)
{
    if (m_screenAssignments.contains(screenName)) {
        m_screenAssignments.remove(screenName);
        Q_EMIT screenAssignmentsChanged();
        Q_EMIT needsSave();
    }
}

QString AssignmentManager::getLayoutForScreen(const QString& screenName) const
{
    return m_screenAssignments.value(screenName).toString();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tiling screen assignments
// ═══════════════════════════════════════════════════════════════════════════════

void AssignmentManager::assignTilingLayoutToScreen(const QString& screenName, const QString& layoutId)
{
    QString oldLayoutId = m_tilingScreenAssignments.value(screenName).toString();
    if (oldLayoutId != layoutId) {
        if (layoutId.isEmpty()) {
            m_tilingScreenAssignments.remove(screenName);
        } else {
            m_tilingScreenAssignments[screenName] = layoutId;
        }
        Q_EMIT tilingScreenAssignmentsChanged();
        Q_EMIT needsSave();
    }
}

void AssignmentManager::clearTilingScreenAssignment(const QString& screenName)
{
    if (m_tilingScreenAssignments.contains(screenName)) {
        m_tilingScreenAssignments.remove(screenName);
        Q_EMIT tilingScreenAssignmentsChanged();
        Q_EMIT needsSave();
    }
}

QString AssignmentManager::getTilingLayoutForScreen(const QString& screenName) const
{
    return m_tilingScreenAssignments.value(screenName).toString();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-desktop screen assignments (daemon-backed with pending cache)
// ═══════════════════════════════════════════════════════════════════════════════

void AssignmentManager::assignLayoutToScreenDesktop(const QString& screenName, int virtualDesktop,
                                                    const QString& layoutId)
{
    if (screenName.isEmpty()) {
        qCWarning(lcKcm) << "Cannot assign layout - empty screen name";
        return;
    }
    QString screenId = Utils::screenIdForName(screenName);
    QString key = QStringLiteral("%1|%2").arg(screenId).arg(virtualDesktop);

    if (layoutId.isEmpty()) {
        m_pendingDesktopAssignments.remove(key);
        m_clearedDesktopAssignments.insert(key);
    } else {
        m_pendingDesktopAssignments[key] = layoutId;
        m_clearedDesktopAssignments.remove(key);
    }

    if (virtualDesktop == 0) {
        if (layoutId.isEmpty()) {
            m_screenAssignments.remove(screenName);
        } else {
            m_screenAssignments[screenName] = layoutId;
        }
    }

    Q_EMIT screenAssignmentsChanged();
    Q_EMIT needsSave();
}

void AssignmentManager::clearScreenDesktopAssignment(const QString& screenName, int virtualDesktop)
{
    if (screenName.isEmpty()) {
        qCWarning(lcKcm) << "Cannot clear assignment - empty screen name";
        return;
    }
    QString screenId = Utils::screenIdForName(screenName);
    QString key = QStringLiteral("%1|%2").arg(screenId).arg(virtualDesktop);
    m_pendingDesktopAssignments.remove(key);
    m_clearedDesktopAssignments.insert(key);

    if (virtualDesktop == 0 && m_screenAssignments.contains(screenName)) {
        m_screenAssignments.remove(screenName);
    }

    Q_EMIT screenAssignmentsChanged();
    Q_EMIT needsSave();
}

QString AssignmentManager::getLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName)).arg(virtualDesktop);
    if (m_pendingDesktopAssignments.contains(key)) {
        return m_pendingDesktopAssignments.value(key);
    }
    if (m_clearedDesktopAssignments.contains(key)) {
        return QString();
    }
    QDBusMessage reply = KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                                             QStringLiteral("getLayoutForScreenDesktop"), {screenName, virtualDesktop});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        return reply.arguments().first().toString();
    }
    return QString();
}

bool AssignmentManager::hasExplicitAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName)).arg(virtualDesktop);
    if (m_pendingDesktopAssignments.contains(key))
        return true;
    if (m_clearedDesktopAssignments.contains(key))
        return false;
    QDBusMessage reply =
        KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                            QStringLiteral("hasExplicitAssignmentForScreenDesktop"), {screenName, virtualDesktop});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        return reply.arguments().first().toBool();
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tiling per-desktop screen assignments
// ═══════════════════════════════════════════════════════════════════════════════

void AssignmentManager::assignTilingLayoutToScreenDesktop(const QString& screenName, int virtualDesktop,
                                                          const QString& layoutId)
{
    if (screenName.isEmpty()) {
        qCWarning(lcKcm) << "Cannot assign tiling layout - empty screen name";
        return;
    }
    if (virtualDesktop < 1) {
        qCWarning(lcKcm) << "Cannot assign tiling layout - invalid desktop number:" << virtualDesktop;
        return;
    }
    QString key = QStringLiteral("%1|%2").arg(screenName).arg(virtualDesktop);
    if (layoutId.isEmpty()) {
        m_tilingDesktopAssignments.remove(key);
    } else {
        m_tilingDesktopAssignments[key] = layoutId;
    }
    m_tilingDesktopAssignmentsDirty = true;
    Q_EMIT tilingDesktopAssignmentsChanged();
    Q_EMIT needsSave();
}

void AssignmentManager::clearTilingScreenDesktopAssignment(const QString& screenName, int virtualDesktop)
{
    assignTilingLayoutToScreenDesktop(screenName, virtualDesktop, QString());
}

QString AssignmentManager::getTilingLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    QString key = QStringLiteral("%1|%2").arg(screenName).arg(virtualDesktop);
    return m_tilingDesktopAssignments.value(key);
}

bool AssignmentManager::hasExplicitTilingAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    QString key = QStringLiteral("%1|%2").arg(screenName).arg(virtualDesktop);
    return m_tilingDesktopAssignments.contains(key);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-activity screen assignments (daemon-backed with pending cache)
// ═══════════════════════════════════════════════════════════════════════════════

void AssignmentManager::assignLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                                     const QString& layoutId)
{
    if (screenName.isEmpty() || activityId.isEmpty()) {
        qCWarning(lcKcm) << "Cannot assign layout - empty screen name or activity ID";
        return;
    }
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName), activityId);
    if (layoutId.isEmpty()) {
        m_pendingActivityAssignments.remove(key);
        m_clearedActivityAssignments.insert(key);
    } else {
        m_pendingActivityAssignments[key] = layoutId;
        m_clearedActivityAssignments.remove(key);
    }
    Q_EMIT activityAssignmentsChanged();
    Q_EMIT screenAssignmentsChanged();
    Q_EMIT needsSave();
}

void AssignmentManager::clearScreenActivityAssignment(const QString& screenName, const QString& activityId)
{
    assignLayoutToScreenActivity(screenName, activityId, QString());
}

QString AssignmentManager::getLayoutForScreenActivity(const QString& screenName, const QString& activityId) const
{
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName), activityId);
    if (m_pendingActivityAssignments.contains(key)) {
        return m_pendingActivityAssignments.value(key);
    }
    if (m_clearedActivityAssignments.contains(key)) {
        return QString();
    }
    QDBusMessage reply = KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                                             QStringLiteral("getLayoutForScreenActivity"), {screenName, activityId});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        return reply.arguments().first().toString();
    }
    return QString();
}

bool AssignmentManager::hasExplicitAssignmentForScreenActivity(const QString& screenName,
                                                               const QString& activityId) const
{
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName), activityId);
    if (m_pendingActivityAssignments.contains(key))
        return true;
    if (m_clearedActivityAssignments.contains(key))
        return false;
    QDBusMessage reply =
        KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager),
                            QStringLiteral("hasExplicitAssignmentForScreenActivity"), {screenName, activityId});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        return reply.arguments().first().toBool();
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tiling per-activity screen assignments
// ═══════════════════════════════════════════════════════════════════════════════

void AssignmentManager::assignTilingLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                                           const QString& layoutId)
{
    if (screenName.isEmpty() || activityId.isEmpty()) {
        qCWarning(lcKcm) << "Cannot assign tiling layout - empty screen name or activity ID";
        return;
    }
    QString key = QStringLiteral("%1|%2").arg(screenName, activityId);
    if (layoutId.isEmpty()) {
        m_tilingActivityAssignments.remove(key);
    } else {
        m_tilingActivityAssignments[key] = layoutId;
    }
    m_tilingActivityAssignmentsDirty = true;
    Q_EMIT tilingActivityAssignmentsChanged();
    Q_EMIT needsSave();
}

void AssignmentManager::clearTilingScreenActivityAssignment(const QString& screenName, const QString& activityId)
{
    assignTilingLayoutToScreenActivity(screenName, activityId, QString());
}

QString AssignmentManager::getTilingLayoutForScreenActivity(const QString& screenName, const QString& activityId) const
{
    QString key = QStringLiteral("%1|%2").arg(screenName, activityId);
    return m_tilingActivityAssignments.value(key);
}

bool AssignmentManager::hasExplicitTilingAssignmentForScreenActivity(const QString& screenName,
                                                                     const QString& activityId) const
{
    QString key = QStringLiteral("%1|%2").arg(screenName, activityId);
    return m_tilingActivityAssignments.contains(key);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Quick layout slots
// ═══════════════════════════════════════════════════════════════════════════════

QString AssignmentManager::getQuickLayoutSlot(int slotNumber) const
{
    if (slotNumber < 1 || slotNumber > 9)
        return QString();
    return m_quickLayoutSlots.value(slotNumber, QString());
}

void AssignmentManager::setQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    if (slotNumber < 1 || slotNumber > 9)
        return;
    QString oldLayoutId = m_quickLayoutSlots.value(slotNumber, QString());
    if (oldLayoutId != layoutId) {
        if (layoutId.isEmpty()) {
            m_quickLayoutSlots.remove(slotNumber);
        } else {
            m_quickLayoutSlots[slotNumber] = layoutId;
        }
        Q_EMIT quickLayoutSlotsChanged();
        Q_EMIT needsSave();
    }
}

QString AssignmentManager::getQuickLayoutShortcut(int slotNumber) const
{
    if (slotNumber < 1 || slotNumber > 9)
        return QString();
    const QString componentName = QStringLiteral("plasmazonesd");
    const QString actionId = QStringLiteral("quick_layout_%1").arg(slotNumber);
    QList<QKeySequence> shortcuts = KGlobalAccel::self()->globalShortcut(componentName, actionId);
    if (!shortcuts.isEmpty() && !shortcuts.first().isEmpty()) {
        return shortcuts.first().toString(QKeySequence::NativeText);
    }
    return QString();
}

QString AssignmentManager::getTilingQuickLayoutSlot(int slotNumber) const
{
    return m_tilingQuickLayoutSlots.value(slotNumber, QString());
}

void AssignmentManager::setTilingQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    if (slotNumber < 1 || slotNumber > 9)
        return;
    if (m_tilingQuickLayoutSlots.value(slotNumber) != layoutId) {
        if (layoutId.isEmpty()) {
            m_tilingQuickLayoutSlots.remove(slotNumber);
        } else {
            m_tilingQuickLayoutSlots[slotNumber] = layoutId;
        }
        Q_EMIT tilingQuickLayoutSlotsChanged();
        Q_EMIT needsSave();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Assignment view mode
// ═══════════════════════════════════════════════════════════════════════════════

void AssignmentManager::setAssignmentViewMode(int mode)
{
    int clamped = qBound(0, mode, 1);
    if (m_assignmentViewMode != clamped) {
        m_assignmentViewMode = clamped;
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        KConfigGroup general = config->group(QStringLiteral("General"));
        general.writeEntry(QStringLiteral("AssignmentViewMode"), m_assignmentViewMode);
        config->sync();
        Q_EMIT assignmentViewModeChanged();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// App-to-zone rules
// ═══════════════════════════════════════════════════════════════════════════════

QVariantList AssignmentManager::getAppRulesForLayout(const QString& layoutId) const
{
    if (m_pendingAppRules.contains(layoutId)) {
        return m_pendingAppRules.value(layoutId);
    }
    QDBusMessage reply =
        KCMDBus::callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getLayout"), {layoutId});
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
        QString ts = ruleObj[QLatin1String("targetScreen")].toString();
        if (!ts.isEmpty()) {
            rule[QStringLiteral("targetScreen")] = ts;
        }
        result.append(rule);
    }
    return result;
}

void AssignmentManager::setAppRulesForLayout(const QString& layoutId, const QVariantList& rules)
{
    m_pendingAppRules[layoutId] = rules;
    Q_EMIT needsSave();
}

void AssignmentManager::addAppRuleToLayout(const QString& layoutId, const QString& pattern, int zoneNumber,
                                           const QString& targetScreen)
{
    QString trimmed = pattern.trimmed();
    if (trimmed.isEmpty() || zoneNumber < 1)
        return;

    QVariantList rules = getAppRulesForLayout(layoutId);
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
    if (!targetScreen.isEmpty()) {
        newRule[QStringLiteral("targetScreen")] = targetScreen;
    }
    rules.append(newRule);
    setAppRulesForLayout(layoutId, rules);
}

void AssignmentManager::removeAppRuleFromLayout(const QString& layoutId, int index)
{
    QVariantList rules = getAppRulesForLayout(layoutId);
    if (index < 0 || index >= rules.size())
        return;
    rules.removeAt(index);
    setAppRulesForLayout(layoutId, rules);
}

// ═══════════════════════════════════════════════════════════════════════════════
// D-Bus event handlers
// ═══════════════════════════════════════════════════════════════════════════════

void AssignmentManager::onScreenLayoutChanged(const QString& screenName, const QString& layoutId)
{
    if (screenName.isEmpty())
        return;

    QString connectorName;
    QScreen* screen = Utils::findScreenByIdOrName(screenName);
    if (screen) {
        connectorName = screen->name();
    } else {
        connectorName = screenName;
    }

    const bool isAutotile = layoutId.startsWith(QLatin1String("autotile:"));

    if (layoutId.isEmpty()) {
        m_tilingScreenAssignments.remove(connectorName);
        Q_EMIT tilingScreenAssignmentsChanged();
    } else if (isAutotile) {
        m_tilingScreenAssignments[connectorName] = layoutId;
        Q_EMIT tilingScreenAssignmentsChanged();
    } else {
        m_screenAssignments[connectorName] = layoutId;
        Q_EMIT screenAssignmentsChanged();
    }

    Q_EMIT refreshScreensRequested();
}

void AssignmentManager::onQuickLayoutSlotsChanged()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                                      QString(DBus::Interface::LayoutManager),
                                                      QStringLiteral("getAllQuickLayoutSlots"));

    QDBusPendingCall pendingCall = QDBusConnection::sessionBus().asyncCall(msg);
    auto* watcher = new QDBusPendingCallWatcher(pendingCall, this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QVariantMap> reply = *w;
        if (reply.isError()) {
            qCWarning(lcKcm) << "Failed to get quick layout slots:" << reply.error().message();
            return;
        }

        m_quickLayoutSlots.clear();
        QVariantMap slots = reply.value();
        for (auto it = slots.begin(); it != slots.end(); ++it) {
            bool ok;
            int slotNum = it.key().toInt(&ok);
            if (ok && slotNum >= 1 && slotNum <= 9) {
                QString layoutId = it.value().toString();
                if (!layoutId.isEmpty()) {
                    m_quickLayoutSlots[slotNum] = layoutId;
                }
            }
        }
        Q_EMIT quickLayoutSlotsChanged();
    });
}

// ═══════════════════════════════════════════════════════════════════════════════
// Autotile ID sync
// ═══════════════════════════════════════════════════════════════════════════════

QString AssignmentManager::resolveAutotileAlgorithm(const QString& key, bool isScreenAssignment) const
{
    QString screenName;
    if (isScreenAssignment) {
        screenName = key;
    } else {
        const int sepIdx = key.lastIndexOf(QLatin1Char('|'));
        if (sepIdx <= 0) {
            qCWarning(lcKcm) << "resolveAutotileAlgorithm: malformed composite key" << key;
            return {};
        }
        screenName = key.left(sepIdx);
    }
    const QVariantMap perScreen = m_settings->getPerScreenAutotileSettings(screenName);
    QString algo = perScreen.value(QLatin1String("Algorithm")).toString();
    if (algo.isEmpty())
        algo = m_settings->autotileAlgorithm();
    if (algo.isEmpty())
        algo = AlgorithmRegistry::defaultAlgorithmId();
    return LayoutId::makeAutotileId(algo);
}

bool AssignmentManager::syncAutotileAssignmentIds(QVariantMap& assignments, bool isScreenAssignment)
{
    bool changed = false;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        if (!LayoutId::isAutotile(it.value().toString()))
            continue;
        const QString resolved = resolveAutotileAlgorithm(it.key(), isScreenAssignment);
        if (resolved.isEmpty())
            continue;
        if (it.value().toString() != resolved) {
            it.value() = resolved;
            changed = true;
        }
    }
    return changed;
}

bool AssignmentManager::syncAutotileAssignmentIds(QMap<QString, QString>& assignments, bool isScreenAssignment)
{
    bool changed = false;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        if (!LayoutId::isAutotile(it.value()))
            continue;
        const QString resolved = resolveAutotileAlgorithm(it.key(), isScreenAssignment);
        if (resolved.isEmpty())
            continue;
        if (it.value() != resolved) {
            it.value() = resolved;
            changed = true;
        }
    }
    return changed;
}

} // namespace PlasmaZones
