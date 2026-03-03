// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "assignmentmanager.h"
#include "kcm_plasmazones.h"
#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <KConfigGroup>
#include <KGlobalAccel>
#include <KSharedConfig>
#include "../src/config/settings.h"
#include "../src/core/constants.h"
#include "../src/core/layout.h"
#include "../src/core/logging.h"
#include "../src/core/utils.h"
#include "../src/autotile/AlgorithmRegistry.h"

namespace PlasmaZones {

AssignmentManager::AssignmentManager(KCMPlasmaZones* kcm, Settings* settings, QObject* parent)
    : QObject(parent)
    , m_kcm(kcm)
    , m_settings(settings)
{
}

// ═══════════════════════════════════════════════════════════════════════════════
// D-Bus helper (local copy — avoid tight coupling to KCM's private API)
// ═══════════════════════════════════════════════════════════════════════════════

QDBusMessage AssignmentManager::callDaemon(const QString& interface, const QString& method,
                                            const QVariantList& args) const
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QString(DBus::ServiceName), QString(DBus::ObjectPath), interface, method);
    if (!args.isEmpty()) {
        msg.setArguments(args);
    }
    QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 5000);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcKcm) << "D-Bus call failed:" << interface << "::" << method << "-" << reply.errorName() << ":"
                         << reply.errorMessage();
    }
    return reply;
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
    QDBusMessage reply = callDaemon(QString(DBus::Interface::LayoutManager),
                                    QStringLiteral("getLayoutForScreenDesktop"), {screenName, virtualDesktop});
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
        return reply.arguments().first().toString();
    }
    return QString();
}

bool AssignmentManager::hasExplicitAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName)).arg(virtualDesktop);
    if (m_pendingDesktopAssignments.contains(key)) return true;
    if (m_clearedDesktopAssignments.contains(key)) return false;
    QDBusMessage reply = callDaemon(QString(DBus::Interface::LayoutManager),
                                    QStringLiteral("hasExplicitAssignmentForScreenDesktop"),
                                    {screenName, virtualDesktop});
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
    QDBusMessage reply = callDaemon(QString(DBus::Interface::LayoutManager),
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
    if (m_pendingActivityAssignments.contains(key)) return true;
    if (m_clearedActivityAssignments.contains(key)) return false;
    QDBusMessage reply = callDaemon(QString(DBus::Interface::LayoutManager),
                                    QStringLiteral("hasExplicitAssignmentForScreenActivity"),
                                    {screenName, activityId});
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
    if (slotNumber < 1 || slotNumber > 9) return QString();
    return m_quickLayoutSlots.value(slotNumber, QString());
}

void AssignmentManager::setQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    if (slotNumber < 1 || slotNumber > 9) return;
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
    if (slotNumber < 1 || slotNumber > 9) return QString();
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
    if (slotNumber < 1 || slotNumber > 9) return;
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
        callDaemon(QString(DBus::Interface::LayoutManager), QStringLiteral("getLayout"), {layoutId});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) return {};

    QString json = reply.arguments().first().toString();
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (doc.isNull() || !doc.isObject()) return {};

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

void AssignmentManager::addAppRuleToLayout(const QString& layoutId, const QString& pattern,
                                            int zoneNumber, const QString& targetScreen)
{
    QString trimmed = pattern.trimmed();
    if (trimmed.isEmpty() || zoneNumber < 1) return;

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
    if (index < 0 || index >= rules.size()) return;
    rules.removeAt(index);
    setAppRulesForLayout(layoutId, rules);
}

// ═══════════════════════════════════════════════════════════════════════════════
// D-Bus event handlers
// ═══════════════════════════════════════════════════════════════════════════════

void AssignmentManager::onScreenLayoutChanged(const QString& screenName, const QString& layoutId)
{
    if (screenName.isEmpty()) return;

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
    if (algo.isEmpty()) algo = m_settings->autotileAlgorithm();
    if (algo.isEmpty()) algo = AlgorithmRegistry::defaultAlgorithmId();
    return LayoutId::makeAutotileId(algo);
}

bool AssignmentManager::syncAutotileAssignmentIds(QVariantMap& assignments, bool isScreenAssignment)
{
    bool changed = false;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        if (!LayoutId::isAutotile(it.value().toString())) continue;
        const QString resolved = resolveAutotileAlgorithm(it.key(), isScreenAssignment);
        if (resolved.isEmpty()) continue;
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
        if (!LayoutId::isAutotile(it.value())) continue;
        const QString resolved = resolveAutotileAlgorithm(it.key(), isScreenAssignment);
        if (resolved.isEmpty()) continue;
        if (it.value() != resolved) {
            it.value() = resolved;
            changed = true;
        }
    }
    return changed;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Save
// ═══════════════════════════════════════════════════════════════════════════════

void AssignmentManager::save(QStringList& failedOperations)
{
    const QString layoutInterface = QString(DBus::Interface::LayoutManager);

    // ── Screen assignments D-Bus push ──────────────────────────────────────
    QVariantMap screenAssignments;
    for (auto it = m_screenAssignments.begin(); it != m_screenAssignments.end(); ++it) {
        screenAssignments[it.key()] = it.value().toString();
    }

    // Auto-populate tiling assignments when autotiling enabled but none exist
    bool screenAssignmentsMutated = false;
    if (m_settings->autotileEnabled() && m_tilingScreenAssignments.isEmpty()) {
        QVariantList screens = m_kcm->screens();
        if (screens.isEmpty()) {
            qCWarning(lcKcm) << "Auto-populate: no screens available, cannot create tiling assignments";
        } else {
            QString algo = m_settings->autotileAlgorithm();
            if (algo.isEmpty()) algo = AlgorithmRegistry::defaultAlgorithmId();
            const QString autotileId = LayoutId::makeAutotileId(algo);
            for (const QVariant& screenVar : std::as_const(screens)) {
                const QVariantMap screenInfo = screenVar.toMap();
                const QString name = screenInfo.value(QStringLiteral("name")).toString();
                if (!name.isEmpty()) {
                    m_tilingScreenAssignments[name] = autotileId;
                    screenAssignmentsMutated = true;
                }
            }
            if (!screenAssignmentsMutated) {
                qCWarning(lcKcm) << "Auto-populate: all screen names were empty, no assignments created";
            }
        }
    }

    // Sync autotile IDs with current effective algorithm
    screenAssignmentsMutated |= syncAutotileAssignmentIds(m_tilingScreenAssignments, true);
    if (screenAssignmentsMutated) {
        Q_EMIT tilingScreenAssignmentsChanged();
    }

    // Merge tiling screen assignments (autotile IDs take precedence)
    for (auto it = m_tilingScreenAssignments.constBegin(); it != m_tilingScreenAssignments.constEnd(); ++it) {
        QString layoutId = it.value().toString();
        if (!layoutId.isEmpty()) {
            screenAssignments[it.key()] = layoutId;
        }
    }
    QDBusMessage screenReply = callDaemon(layoutInterface, QStringLiteral("setAllScreenAssignments"), {screenAssignments});
    if (screenReply.type() == QDBusMessage::ErrorMessage) {
        failedOperations.append(QStringLiteral("Screen assignments"));
    }

    // ── Quick layout slots D-Bus push ──────────────────────────────────────
    QVariantMap quickSlots;
    for (int slot = 1; slot <= 9; ++slot) {
        quickSlots[QString::number(slot)] = m_quickLayoutSlots.value(slot, QString());
    }
    QDBusMessage quickReply = callDaemon(layoutInterface, QStringLiteral("setAllQuickLayoutSlots"), {quickSlots});
    if (quickReply.type() == QDBusMessage::ErrorMessage) {
        failedOperations.append(QStringLiteral("Quick layout slots"));
    }

    // ── KConfig shadow save ────────────────────────────────────────────────
    {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        const QStringList allGroups = config->groupList();

        auto safeScreenId = [](const QString& connectorName) -> QString {
            QString screenId = Utils::screenIdForName(connectorName);
            if (screenId == connectorName && !connectorName.contains(QLatin1Char(':'))) {
                qCWarning(lcKcm) << "Screen" << connectorName
                                 << "not found - saving with connector name (may not survive port changes)";
            }
            return screenId;
        };

        // Delete old per-screen groups (all types in one pass)
        for (const QString& groupName : allGroups) {
            if (groupName.startsWith(QLatin1String("SnappingScreen:")) ||
                groupName.startsWith(QLatin1String("TilingScreen:")) ||
                groupName.startsWith(QLatin1String("TilingActivity:")) ||
                groupName.startsWith(QLatin1String("TilingDesktop:"))) {
                config->deleteGroup(groupName);
            }
        }

        // Clean up legacy flat-key groups
        for (const auto& legacyName : {QStringLiteral("SnappingScreenAssignments"),
                                        QStringLiteral("TilingScreenAssignments")}) {
            KConfigGroup legacy = config->group(legacyName);
            if (legacy.exists()) legacy.deleteGroup();
        }

        // Write snapping per-screen groups
        for (auto it = m_screenAssignments.constBegin(); it != m_screenAssignments.constEnd(); ++it) {
            QString screenId = safeScreenId(it.key());
            KConfigGroup screenGroup = config->group(QStringLiteral("SnappingScreen:") + screenId);
            screenGroup.writeEntry(QStringLiteral("Assignment"), it.value().toString());
        }

        // Write tiling per-screen groups
        for (auto it = m_tilingScreenAssignments.constBegin(); it != m_tilingScreenAssignments.constEnd(); ++it) {
            QString screenId = safeScreenId(it.key());
            KConfigGroup screenGroup = config->group(QStringLiteral("TilingScreen:") + screenId);
            screenGroup.writeEntry(QStringLiteral("Assignment"), it.value().toString());
        }

        // Write tiling activity assignments
        for (auto it = m_tilingActivityAssignments.constBegin(); it != m_tilingActivityAssignments.constEnd(); ++it) {
            int sepIdx = it.key().lastIndexOf(QLatin1Char('|'));
            if (sepIdx <= 0) continue;
            QString connectorName = it.key().left(sepIdx);
            QString activityId = it.key().mid(sepIdx + 1);
            QString screenId = safeScreenId(connectorName);
            KConfigGroup actGroup = config->group(QStringLiteral("TilingActivity:%1|%2").arg(screenId, activityId));
            actGroup.writeEntry(QStringLiteral("Assignment"), it.value());
        }

        // Write tiling per-desktop assignments
        for (auto it = m_tilingDesktopAssignments.constBegin(); it != m_tilingDesktopAssignments.constEnd(); ++it) {
            int sepIdx = it.key().lastIndexOf(QLatin1Char('|'));
            if (sepIdx <= 0) continue;
            QString connectorName = it.key().left(sepIdx);
            QString desktopNum = it.key().mid(sepIdx + 1);
            QString screenId = safeScreenId(connectorName);
            KConfigGroup deskGroup = config->group(QStringLiteral("TilingDesktop:%1|%2").arg(screenId, desktopNum));
            deskGroup.writeEntry(QStringLiteral("Assignment"), it.value());
        }

        // Tiling quick layout slots
        KConfigGroup tilingSlots = config->group(QStringLiteral("TilingQuickLayoutSlots"));
        tilingSlots.deleteGroup();
        for (auto it = m_tilingQuickLayoutSlots.constBegin(); it != m_tilingQuickLayoutSlots.constEnd(); ++it) {
            tilingSlots.writeEntry(QString::number(it.key()), it.value());
        }

        config->sync();
    }

    // ── Per-Desktop assignments (batch) ────────────────────────────────────
    syncAutotileAssignmentIds(m_tilingDesktopAssignments, false);

    if (!m_pendingDesktopAssignments.isEmpty() || !m_clearedDesktopAssignments.isEmpty()
        || m_tilingDesktopAssignmentsDirty) {
        QVariantMap desktopAssignments;
        QDBusMessage currentReply = callDaemon(layoutInterface, QStringLiteral("getAllDesktopAssignments"));
        if (currentReply.type() == QDBusMessage::ReplyMessage && !currentReply.arguments().isEmpty()) {
            desktopAssignments = qdbus_cast<QVariantMap>(currentReply.arguments().first());
        }
        for (const QString& key : std::as_const(m_clearedDesktopAssignments)) {
            desktopAssignments.remove(key);
        }
        for (auto it = m_pendingDesktopAssignments.begin(); it != m_pendingDesktopAssignments.end(); ++it) {
            desktopAssignments[it.key()] = it.value();
        }
        for (auto it = m_tilingDesktopAssignments.constBegin(); it != m_tilingDesktopAssignments.constEnd(); ++it) {
            if (!it.value().isEmpty()) desktopAssignments[it.key()] = it.value();
        }
        QDBusMessage desktopReply = callDaemon(layoutInterface, QStringLiteral("setAllDesktopAssignments"), {desktopAssignments});
        if (desktopReply.type() == QDBusMessage::ErrorMessage) {
            failedOperations.append(QStringLiteral("Per-desktop assignments"));
        }
        m_clearedDesktopAssignments.clear();
        m_pendingDesktopAssignments.clear();
        m_tilingDesktopAssignmentsDirty = false;
    }

    // ── Per-Activity assignments (batch) ───────────────────────────────────
    syncAutotileAssignmentIds(m_tilingActivityAssignments, false);

    if (!m_pendingActivityAssignments.isEmpty() || !m_clearedActivityAssignments.isEmpty()
        || m_tilingActivityAssignmentsDirty) {
        QVariantMap activityAssignments;
        QDBusMessage currentReply = callDaemon(layoutInterface, QStringLiteral("getAllActivityAssignments"));
        if (currentReply.type() == QDBusMessage::ReplyMessage && !currentReply.arguments().isEmpty()) {
            activityAssignments = qdbus_cast<QVariantMap>(currentReply.arguments().first());
        }
        for (const QString& key : std::as_const(m_clearedActivityAssignments)) {
            activityAssignments.remove(key);
        }
        for (auto it = m_pendingActivityAssignments.begin(); it != m_pendingActivityAssignments.end(); ++it) {
            activityAssignments[it.key()] = it.value();
        }
        for (auto it = m_tilingActivityAssignments.constBegin(); it != m_tilingActivityAssignments.constEnd(); ++it) {
            if (!it.value().isEmpty()) activityAssignments[it.key()] = it.value();
        }
        QDBusMessage activityReply = callDaemon(layoutInterface, QStringLiteral("setAllActivityAssignments"), {activityAssignments});
        if (activityReply.type() == QDBusMessage::ErrorMessage) {
            failedOperations.append(QStringLiteral("Per-activity assignments"));
        }
        m_clearedActivityAssignments.clear();
        m_pendingActivityAssignments.clear();
        m_tilingActivityAssignmentsDirty = false;
    }

    // ── App-to-zone rules ──────────────────────────────────────────────────
    if (!m_pendingAppRules.isEmpty()) {
        for (auto it = m_pendingAppRules.cbegin(); it != m_pendingAppRules.cend(); ++it) {
            const QString& layoutId = it.key();
            const QVariantList& rules = it.value();

            QDBusMessage layoutReply = callDaemon(layoutInterface, QStringLiteral("getLayout"), {layoutId});
            if (layoutReply.type() != QDBusMessage::ReplyMessage || layoutReply.arguments().isEmpty()) {
                failedOperations.append(QStringLiteral("App rules (get %1)").arg(layoutId));
                continue;
            }

            QString json = layoutReply.arguments().first().toString();
            QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
            if (doc.isNull() || !doc.isObject()) {
                failedOperations.append(QStringLiteral("App rules (parse %1)").arg(layoutId));
                continue;
            }

            QJsonArray rulesArray;
            for (const auto& ruleVar : rules) {
                QVariantMap ruleMap = ruleVar.toMap();
                PlasmaZones::AppRule rule;
                rule.pattern = ruleMap[QStringLiteral("pattern")].toString();
                rule.zoneNumber = ruleMap[QStringLiteral("zoneNumber")].toInt();
                rule.targetScreen = ruleMap[QStringLiteral("targetScreen")].toString();
                rulesArray.append(rule.toJson());
            }

            QJsonObject obj = doc.object();
            obj[JsonKeys::AppRules] = rulesArray;
            QString updatedJson = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
            QDBusMessage updateReply = callDaemon(layoutInterface, QStringLiteral("updateLayout"), {updatedJson});
            if (updateReply.type() == QDBusMessage::ErrorMessage) {
                failedOperations.append(QStringLiteral("App rules (save %1)").arg(layoutId));
            }
        }
        m_pendingAppRules.clear();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Load
// ═══════════════════════════════════════════════════════════════════════════════

void AssignmentManager::load()
{
    const QString layoutInterface = QString(DBus::Interface::LayoutManager);

    m_screenAssignments.clear();
    m_tilingScreenAssignments.clear();
    m_tilingActivityAssignments.clear();
    m_tilingDesktopAssignments.clear();
    m_tilingQuickLayoutSlots.clear();
    m_tilingDesktopAssignmentsDirty = false;
    m_tilingActivityAssignmentsDirty = false;
    {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        const QStringList allGroups = config->groupList();

        auto resolveScreenId = [](const QString& screenId) -> QString {
            if (Utils::isConnectorName(screenId)) return screenId;
            return Utils::screenNameForId(screenId);
        };

        auto loadPerScreenAssignments = [&](const QLatin1String& prefix, QVariantMap& target) {
            for (const QString& groupName : allGroups) {
                if (groupName.startsWith(prefix)) {
                    QString screenId = groupName.mid(prefix.size());
                    if (screenId.isEmpty()) continue;
                    KConfigGroup screenGroup = config->group(groupName);
                    QString layoutId = screenGroup.readEntry(QStringLiteral("Assignment"), QString());
                    if (!layoutId.isEmpty()) {
                        QString connectorName = resolveScreenId(screenId);
                        if (!connectorName.isEmpty()) target[connectorName] = layoutId;
                    }
                }
            }
        };

        auto loadCompoundAssignments = [&](const QLatin1String& prefix, QMap<QString, QString>& target) {
            for (const QString& groupName : allGroups) {
                if (groupName.startsWith(prefix)) {
                    QString compoundKey = groupName.mid(prefix.size());
                    int sepIdx = compoundKey.lastIndexOf(QLatin1Char('|'));
                    if (sepIdx <= 0) continue;
                    QString screenId = compoundKey.left(sepIdx);
                    QString suffix = compoundKey.mid(sepIdx + 1);
                    if (screenId.isEmpty() || suffix.isEmpty()) continue;
                    KConfigGroup group = config->group(groupName);
                    QString layoutId = group.readEntry(QStringLiteral("Assignment"), QString());
                    if (!layoutId.isEmpty()) {
                        QString connectorName = resolveScreenId(screenId);
                        if (!connectorName.isEmpty()) {
                            target[QStringLiteral("%1|%2").arg(connectorName, suffix)] = layoutId;
                        }
                    }
                }
            }
        };

        // Snapping screen assignments
        const QLatin1String snappingPrefix("SnappingScreen:");
        bool foundSnappingGroups = false;
        for (const QString& groupName : allGroups) {
            if (groupName.startsWith(snappingPrefix)) { foundSnappingGroups = true; break; }
        }
        if (foundSnappingGroups) {
            loadPerScreenAssignments(snappingPrefix, m_screenAssignments);
        } else {
            KConfigGroup legacySnapping = config->group(QStringLiteral("SnappingScreenAssignments"));
            if (legacySnapping.exists()) {
                const QStringList keys = legacySnapping.keyList();
                for (const QString& key : keys) {
                    QString layoutId = legacySnapping.readEntry(key, QString());
                    if (!layoutId.isEmpty()) m_screenAssignments[key] = layoutId;
                }
            } else {
                // Fallback: load from daemon for first run
                QDBusMessage assignmentsReply = callDaemon(layoutInterface, QStringLiteral("getAllScreenAssignments"));
                if (assignmentsReply.type() == QDBusMessage::ReplyMessage && !assignmentsReply.arguments().isEmpty()) {
                    QString assignmentsJson = assignmentsReply.arguments().first().toString();
                    QJsonDocument doc = QJsonDocument::fromJson(assignmentsJson.toUtf8());
                    if (doc.isObject()) {
                        QJsonObject root = doc.object();
                        for (auto it = root.begin(); it != root.end(); ++it) {
                            QString screenName = it.key();
                            QJsonObject screenObj = it.value().toObject();
                            if (screenObj.contains(QStringLiteral("default"))) {
                                QString layoutId = screenObj.value(QStringLiteral("default")).toString();
                                if (!layoutId.isEmpty()) {
                                    if (layoutId.startsWith(QLatin1String("autotile:"))) {
                                        m_tilingScreenAssignments[screenName] = layoutId;
                                    } else {
                                        m_screenAssignments[screenName] = layoutId;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Tiling screen assignments
        const QLatin1String tilingPrefix("TilingScreen:");
        loadPerScreenAssignments(tilingPrefix, m_tilingScreenAssignments);
        if (m_tilingScreenAssignments.isEmpty()) {
            KConfigGroup legacyTiling = config->group(QStringLiteral("TilingScreenAssignments"));
            if (legacyTiling.exists()) {
                const QStringList keys = legacyTiling.keyList();
                for (const QString& key : keys) {
                    QString layoutId = legacyTiling.readEntry(key, QString());
                    if (!layoutId.isEmpty()) m_tilingScreenAssignments[key] = layoutId;
                }
            }
        }

        // Tiling activity and desktop assignments
        loadCompoundAssignments(QLatin1String("TilingActivity:"), m_tilingActivityAssignments);
        loadCompoundAssignments(QLatin1String("TilingDesktop:"), m_tilingDesktopAssignments);

        // Tiling quick layout slots
        KConfigGroup tilingSlots = config->group(QStringLiteral("TilingQuickLayoutSlots"));
        for (int slot = 1; slot <= 9; ++slot) {
            QString layoutId = tilingSlots.readEntry(QString::number(slot), QString());
            if (!layoutId.isEmpty()) m_tilingQuickLayoutSlots[slot] = layoutId;
        }

        // Assignment view mode
        KConfigGroup general = config->group(QStringLiteral("General"));
        m_assignmentViewMode = qBound(0, general.readEntry(QStringLiteral("AssignmentViewMode"), 0), 1);
    }

    // Quick layout slots from daemon
    m_quickLayoutSlots.clear();
    QDBusMessage slotsReply = callDaemon(layoutInterface, QStringLiteral("getAllQuickLayoutSlots"));
    if (slotsReply.type() == QDBusMessage::ReplyMessage && !slotsReply.arguments().isEmpty()) {
        QVariantMap slotsMap = qdbus_cast<QVariantMap>(slotsReply.arguments().first());
        for (auto it = slotsMap.begin(); it != slotsMap.end(); ++it) {
            bool ok;
            int slotNum = it.key().toInt(&ok);
            if (ok && slotNum >= 1 && slotNum <= 9) {
                QString layoutId = it.value().toString();
                if (!layoutId.isEmpty()) m_quickLayoutSlots[slotNum] = layoutId;
            }
        }
    }

    clearPendingStates();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Defaults / clear
// ═══════════════════════════════════════════════════════════════════════════════

void AssignmentManager::resetToDefaults()
{
    m_screenAssignments.clear();
    m_quickLayoutSlots.clear();
    m_tilingScreenAssignments.clear();
    m_tilingActivityAssignments.clear();
    m_tilingDesktopAssignments.clear();
    m_tilingQuickLayoutSlots.clear();
    m_tilingDesktopAssignmentsDirty = false;
    m_tilingActivityAssignmentsDirty = false;
    m_assignmentViewMode = 0;
    clearPendingStates();

    Q_EMIT tilingScreenAssignmentsChanged();
    Q_EMIT tilingActivityAssignmentsChanged();
    Q_EMIT tilingDesktopAssignmentsChanged();
    Q_EMIT assignmentViewModeChanged();
}

void AssignmentManager::clearPendingStates()
{
    m_pendingDesktopAssignments.clear();
    m_clearedDesktopAssignments.clear();
    m_pendingActivityAssignments.clear();
    m_clearedActivityAssignments.clear();
    m_pendingAppRules.clear();
}

} // namespace PlasmaZones
