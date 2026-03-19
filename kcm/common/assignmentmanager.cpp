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

AssignmentEntry AssignmentManager::resolveDesktopEntry(const QString& key) const
{
    auto pendIt = m_pendingDesktopEntries.constFind(key);
    if (pendIt != m_pendingDesktopEntries.constEnd())
        return *pendIt;
    auto cacheIt = m_cachedDesktopAssignments.constFind(key);
    if (cacheIt != m_cachedDesktopAssignments.constEnd())
        return *cacheIt;
    return {};
}

AssignmentEntry AssignmentManager::resolveActivityEntry(const QString& key) const
{
    auto pendIt = m_pendingActivityEntries.constFind(key);
    if (pendIt != m_pendingActivityEntries.constEnd())
        return *pendIt;
    auto cacheIt = m_cachedActivityAssignments.constFind(key);
    if (cacheIt != m_cachedActivityAssignments.constEnd())
        return *cacheIt;
    return {};
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
        m_screenModes[screenName] = AssignmentEntry::Snapping;

        // Build full entry for save — update snapping field only, preserve tiling
        AssignmentEntry entry;
        auto it = m_pendingScreenEntries.constFind(screenName);
        if (it != m_pendingScreenEntries.constEnd()) {
            entry = *it; // Start from existing pending entry
        } else {
            // Preserve tiling from the daemon's cached state
            entry.tilingAlgorithm =
                LayoutId::extractAlgorithmId(m_tilingScreenAssignments.value(screenName).toString());
        }
        entry.mode = AssignmentEntry::Snapping;
        entry.snappingLayout = layoutId;
        m_pendingScreenEntries[screenName] = entry;

        Q_EMIT screenAssignmentsChanged();
        Q_EMIT needsSave();
    }
}

void AssignmentManager::clearScreenAssignment(const QString& screenName)
{
    m_screenAssignments.remove(screenName);

    // Clear only the snapping field, preserve mode and tiling algorithm
    AssignmentEntry entry;
    auto it = m_pendingScreenEntries.constFind(screenName);
    if (it != m_pendingScreenEntries.constEnd()) {
        entry = *it;
    } else {
        entry.tilingAlgorithm = LayoutId::extractAlgorithmId(m_tilingScreenAssignments.value(screenName).toString());
        entry.mode = m_screenModes.value(screenName, AssignmentEntry::Snapping);
    }
    entry.snappingLayout.clear();

    if (entry.tilingAlgorithm.isEmpty()) {
        // No tiling either — truly clear this screen (remove from daemon)
        m_pendingScreenEntries.remove(screenName);
        m_clearedScreenAssignments.insert(screenName);
    } else {
        // Keep entry with tiling preserved, snapping cleared
        m_pendingScreenEntries[screenName] = entry;
        m_clearedScreenAssignments.remove(screenName);
    }

    Q_EMIT screenAssignmentsChanged();
    Q_EMIT needsSave();
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
        m_screenModes[screenName] = AssignmentEntry::Autotile;

        // Build full entry — update tiling field only, preserve snapping
        AssignmentEntry entry;
        auto it = m_pendingScreenEntries.constFind(screenName);
        if (it != m_pendingScreenEntries.constEnd()) {
            entry = *it;
        } else {
            entry.snappingLayout = m_screenAssignments.value(screenName).toString();
        }
        entry.mode = AssignmentEntry::Autotile;
        entry.tilingAlgorithm = LayoutId::extractAlgorithmId(layoutId);
        m_pendingScreenEntries[screenName] = entry;

        Q_EMIT tilingScreenAssignmentsChanged();
        Q_EMIT needsSave();
    }
}

void AssignmentManager::clearTilingScreenAssignment(const QString& screenName)
{
    m_tilingScreenAssignments.remove(screenName);

    // Clear only tiling, preserve mode and snapping
    AssignmentEntry entry;
    auto it = m_pendingScreenEntries.constFind(screenName);
    if (it != m_pendingScreenEntries.constEnd()) {
        entry = *it;
    } else {
        entry.snappingLayout = m_screenAssignments.value(screenName).toString();
        entry.mode = m_screenModes.value(screenName, AssignmentEntry::Snapping);
    }
    entry.tilingAlgorithm.clear();

    if (entry.snappingLayout.isEmpty()) {
        m_pendingScreenEntries.remove(screenName);
        m_clearedTilingScreenAssignments.insert(screenName);
    } else {
        m_pendingScreenEntries[screenName] = entry;
        m_clearedTilingScreenAssignments.remove(screenName);
    }

    Q_EMIT tilingScreenAssignmentsChanged();
    Q_EMIT needsSave();
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

    m_clearedDesktopAssignments.remove(key);

    if (virtualDesktop == 0) {
        if (layoutId.isEmpty()) {
            m_screenAssignments.remove(screenName);
        } else {
            m_screenAssignments[screenName] = layoutId;
        }
    }

    // Build full entry — update snapping, preserve tiling from cache
    AssignmentEntry entry = resolveDesktopEntry(key);
    entry.mode = AssignmentEntry::Snapping;
    entry.snappingLayout = layoutId;
    m_pendingDesktopEntries[key] = entry;

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

    if (virtualDesktop == 0 && m_screenAssignments.contains(screenName)) {
        m_screenAssignments.remove(screenName);
    }

    // Clear only the snapping field — preserve mode and tiling
    AssignmentEntry entry = resolveDesktopEntry(key);

    entry.snappingLayout.clear();

    if (!entry.tilingAlgorithm.isEmpty()) {
        // Entry still meaningful (has tiling) — keep it
        m_pendingDesktopEntries[key] = entry;
        m_clearedDesktopAssignments.remove(key);
    } else {
        // Check if base has a DIFFERENT mode — if so, keep this entry to prevent inheritance
        auto baseMode = m_screenModes.value(screenName, AssignmentEntry::Snapping);
        if (baseMode != entry.mode) {
            m_pendingDesktopEntries[key] = entry;
            m_clearedDesktopAssignments.remove(key);
        } else {
            // Same mode as base, no tiling, no snapping — safe to clear entirely
            m_pendingDesktopEntries.remove(key);
            m_clearedDesktopAssignments.insert(key);
        }
    }

    Q_EMIT screenAssignmentsChanged();
    Q_EMIT needsSave();
}

QString AssignmentManager::getLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName)).arg(virtualDesktop);
    auto pendIt = m_pendingDesktopEntries.constFind(key);
    if (pendIt != m_pendingDesktopEntries.constEnd())
        return pendIt->activeLayoutId();
    if (m_clearedDesktopAssignments.contains(key))
        return QString();
    auto cacheIt = m_cachedDesktopAssignments.constFind(key);
    if (cacheIt != m_cachedDesktopAssignments.constEnd())
        return cacheIt->activeLayoutId();
    return QString();
}

bool AssignmentManager::hasExplicitAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName)).arg(virtualDesktop);
    auto pendIt = m_pendingDesktopEntries.constFind(key);
    if (pendIt != m_pendingDesktopEntries.constEnd())
        return !pendIt->snappingLayout.isEmpty();
    if (m_clearedDesktopAssignments.contains(key))
        return false;
    auto cacheIt = m_cachedDesktopAssignments.constFind(key);
    if (cacheIt != m_cachedDesktopAssignments.constEnd())
        return !cacheIt->snappingLayout.isEmpty();
    return false;
}

QString AssignmentManager::getSnappingLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName)).arg(virtualDesktop);
    auto pendIt = m_pendingDesktopEntries.constFind(key);
    if (pendIt != m_pendingDesktopEntries.constEnd())
        return pendIt->snappingLayout;
    if (m_clearedDesktopAssignments.contains(key))
        return QString();
    auto cacheIt = m_cachedDesktopAssignments.constFind(key);
    if (cacheIt != m_cachedDesktopAssignments.constEnd())
        return cacheIt->snappingLayout;
    return QString();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tiling per-desktop screen assignments — delegates to snapping equivalents
// (daemon-backed pending cache + D-Bus queries, shared code path)
// ═══════════════════════════════════════════════════════════════════════════════

void AssignmentManager::assignTilingLayoutToScreenDesktop(const QString& screenName, int virtualDesktop,
                                                          const QString& layoutId)
{
    if (virtualDesktop < 1) {
        qCWarning(lcKcm) << "Cannot assign tiling layout - invalid desktop number:" << virtualDesktop;
        return;
    }
    QString screenId = Utils::screenIdForName(screenName);
    QString key = QStringLiteral("%1|%2").arg(screenId).arg(virtualDesktop);

    m_clearedDesktopAssignments.remove(key);

    // Build full entry — update tiling, preserve snapping from cache
    AssignmentEntry entry = resolveDesktopEntry(key);
    entry.mode = AssignmentEntry::Autotile;
    entry.tilingAlgorithm = LayoutId::extractAlgorithmId(layoutId);
    m_pendingDesktopEntries[key] = entry;

    Q_EMIT tilingDesktopAssignmentsChanged();
    Q_EMIT screenAssignmentsChanged();
    Q_EMIT needsSave();
}

void AssignmentManager::clearTilingScreenDesktopAssignment(const QString& screenName, int virtualDesktop)
{
    if (virtualDesktop < 1) {
        qCWarning(lcKcm) << "Cannot clear tiling assignment - invalid desktop number:" << virtualDesktop;
        return;
    }
    QString screenId = Utils::screenIdForName(screenName);
    QString key = QStringLiteral("%1|%2").arg(screenId).arg(virtualDesktop);

    AssignmentEntry entry = resolveDesktopEntry(key);
    entry.tilingAlgorithm.clear();

    auto baseMode = m_screenModes.value(screenName, AssignmentEntry::Snapping);
    if (!entry.snappingLayout.isEmpty() || (entry.mode == AssignmentEntry::Autotile && baseMode != entry.mode)) {
        m_pendingDesktopEntries[key] = entry;
        m_clearedDesktopAssignments.remove(key);
    } else {
        m_pendingDesktopEntries.remove(key);
        m_clearedDesktopAssignments.insert(key);
    }

    Q_EMIT tilingDesktopAssignmentsChanged();
    Q_EMIT screenAssignmentsChanged();
    Q_EMIT needsSave();
}

QString AssignmentManager::getTilingLayoutForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName)).arg(virtualDesktop);
    auto pendIt = m_pendingDesktopEntries.constFind(key);
    if (pendIt != m_pendingDesktopEntries.constEnd()) {
        if (!pendIt->tilingAlgorithm.isEmpty())
            return LayoutId::makeAutotileId(pendIt->tilingAlgorithm);
        return QString();
    }
    if (m_clearedDesktopAssignments.contains(key))
        return QString();
    auto cacheIt = m_cachedDesktopAssignments.constFind(key);
    if (cacheIt != m_cachedDesktopAssignments.constEnd() && !cacheIt->tilingAlgorithm.isEmpty())
        return LayoutId::makeAutotileId(cacheIt->tilingAlgorithm);
    return QString();
}

bool AssignmentManager::hasExplicitTilingAssignmentForScreenDesktop(const QString& screenName, int virtualDesktop) const
{
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName)).arg(virtualDesktop);
    auto pendIt = m_pendingDesktopEntries.constFind(key);
    if (pendIt != m_pendingDesktopEntries.constEnd())
        return !pendIt->tilingAlgorithm.isEmpty();
    if (m_clearedDesktopAssignments.contains(key))
        return false;
    auto cacheIt = m_cachedDesktopAssignments.constFind(key);
    if (cacheIt != m_cachedDesktopAssignments.constEnd())
        return !cacheIt->tilingAlgorithm.isEmpty();
    return false;
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
        m_clearedActivityAssignments.insert(key);
    } else {
        m_clearedActivityAssignments.remove(key);
    }

    // Build full entry
    AssignmentEntry entry = resolveActivityEntry(key);

    if (LayoutId::isAutotile(layoutId)) {
        entry.mode = AssignmentEntry::Autotile;
        entry.tilingAlgorithm = LayoutId::extractAlgorithmId(layoutId);
    } else {
        entry.mode = AssignmentEntry::Snapping;
        entry.snappingLayout = layoutId;
    }
    if (!layoutId.isEmpty()) {
        m_pendingActivityEntries[key] = entry;
    } else {
        // Empty layoutId = clear. Remove stale pending entry so it doesn't
        // override the clear during save.
        m_pendingActivityEntries.remove(key);
    }

    Q_EMIT activityAssignmentsChanged();
    Q_EMIT screenAssignmentsChanged();
    Q_EMIT needsSave();
}

void AssignmentManager::clearScreenActivityAssignment(const QString& screenName, const QString& activityId)
{
    if (screenName.isEmpty() || activityId.isEmpty())
        return;
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName), activityId);

    // Clear snapping field, preserve tiling
    AssignmentEntry entry = resolveActivityEntry(key);
    entry.snappingLayout.clear();

    if (!entry.tilingAlgorithm.isEmpty()) {
        m_pendingActivityEntries[key] = entry;
        m_clearedActivityAssignments.remove(key);
    } else {
        m_pendingActivityEntries.remove(key);
        m_clearedActivityAssignments.insert(key);
    }

    Q_EMIT activityAssignmentsChanged();
    Q_EMIT screenAssignmentsChanged();
    Q_EMIT needsSave();
}

QString AssignmentManager::getLayoutForScreenActivity(const QString& screenName, const QString& activityId) const
{
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName), activityId);
    auto pendIt = m_pendingActivityEntries.constFind(key);
    if (pendIt != m_pendingActivityEntries.constEnd())
        return pendIt->activeLayoutId();
    if (m_clearedActivityAssignments.contains(key))
        return QString();
    auto cacheIt = m_cachedActivityAssignments.constFind(key);
    if (cacheIt != m_cachedActivityAssignments.constEnd())
        return cacheIt->activeLayoutId();
    return QString();
}

bool AssignmentManager::hasExplicitAssignmentForScreenActivity(const QString& screenName,
                                                               const QString& activityId) const
{
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName), activityId);
    auto pendIt = m_pendingActivityEntries.constFind(key);
    if (pendIt != m_pendingActivityEntries.constEnd())
        return !pendIt->snappingLayout.isEmpty();
    if (m_clearedActivityAssignments.contains(key))
        return false;
    auto cacheIt = m_cachedActivityAssignments.constFind(key);
    if (cacheIt != m_cachedActivityAssignments.constEnd())
        return !cacheIt->snappingLayout.isEmpty();
    return false;
}

QString AssignmentManager::getSnappingLayoutForScreenActivity(const QString& screenName,
                                                              const QString& activityId) const
{
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName), activityId);
    auto pendIt = m_pendingActivityEntries.constFind(key);
    if (pendIt != m_pendingActivityEntries.constEnd())
        return pendIt->snappingLayout;
    if (m_clearedActivityAssignments.contains(key))
        return QString();
    auto cacheIt = m_cachedActivityAssignments.constFind(key);
    if (cacheIt != m_cachedActivityAssignments.constEnd())
        return cacheIt->snappingLayout;
    return QString();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tiling per-activity screen assignments — delegates to snapping equivalents
// (daemon-backed pending cache + D-Bus queries, shared code path)
// ═══════════════════════════════════════════════════════════════════════════════

void AssignmentManager::assignTilingLayoutToScreenActivity(const QString& screenName, const QString& activityId,
                                                           const QString& layoutId)
{
    if (screenName.isEmpty() || activityId.isEmpty()) {
        qCWarning(lcKcm) << "Cannot assign tiling layout - empty screen name or activity ID";
        return;
    }
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName), activityId);

    m_clearedActivityAssignments.remove(key);

    // Build full entry — update tiling, preserve snapping
    AssignmentEntry entry = resolveActivityEntry(key);
    entry.mode = AssignmentEntry::Autotile;
    entry.tilingAlgorithm = LayoutId::extractAlgorithmId(layoutId);
    m_pendingActivityEntries[key] = entry;

    Q_EMIT tilingActivityAssignmentsChanged();
    Q_EMIT activityAssignmentsChanged();
    Q_EMIT screenAssignmentsChanged();
    Q_EMIT needsSave();
}

void AssignmentManager::clearTilingScreenActivityAssignment(const QString& screenName, const QString& activityId)
{
    if (screenName.isEmpty() || activityId.isEmpty())
        return;
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName), activityId);

    AssignmentEntry entry = resolveActivityEntry(key);
    entry.tilingAlgorithm.clear();

    if (!entry.snappingLayout.isEmpty()) {
        m_pendingActivityEntries[key] = entry;
        m_clearedActivityAssignments.remove(key);
    } else {
        m_pendingActivityEntries.remove(key);
        m_clearedActivityAssignments.insert(key);
    }

    Q_EMIT tilingActivityAssignmentsChanged();
    Q_EMIT activityAssignmentsChanged();
    Q_EMIT screenAssignmentsChanged();
    Q_EMIT needsSave();
}

QString AssignmentManager::getTilingLayoutForScreenActivity(const QString& screenName, const QString& activityId) const
{
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName), activityId);
    auto pendIt = m_pendingActivityEntries.constFind(key);
    if (pendIt != m_pendingActivityEntries.constEnd()) {
        if (!pendIt->tilingAlgorithm.isEmpty())
            return LayoutId::makeAutotileId(pendIt->tilingAlgorithm);
        return QString();
    }
    if (m_clearedActivityAssignments.contains(key))
        return QString();
    auto cacheIt = m_cachedActivityAssignments.constFind(key);
    if (cacheIt != m_cachedActivityAssignments.constEnd() && !cacheIt->tilingAlgorithm.isEmpty())
        return LayoutId::makeAutotileId(cacheIt->tilingAlgorithm);
    return QString();
}

bool AssignmentManager::hasExplicitTilingAssignmentForScreenActivity(const QString& screenName,
                                                                     const QString& activityId) const
{
    QString key = QStringLiteral("%1|%2").arg(Utils::screenIdForName(screenName), activityId);
    auto pendIt = m_pendingActivityEntries.constFind(key);
    if (pendIt != m_pendingActivityEntries.constEnd())
        return !pendIt->tilingAlgorithm.isEmpty();
    if (m_clearedActivityAssignments.contains(key))
        return false;
    auto cacheIt = m_cachedActivityAssignments.constFind(key);
    if (cacheIt != m_cachedActivityAssignments.constEnd())
        return !cacheIt->tilingAlgorithm.isEmpty();
    return false;
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

void AssignmentManager::onScreenLayoutChanged(const QString& screenName, const QString& layoutId, int virtualDesktop)
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

    const bool isAutotile = LayoutId::isAutotile(layoutId);

    if (virtualDesktop == 0) {
        // Update display-level defaults (base screen assignments)
        if (layoutId.isEmpty()) {
            m_screenAssignments.remove(connectorName);
            m_tilingScreenAssignments.remove(connectorName);
            m_screenModes.remove(connectorName);
        } else if (isAutotile) {
            m_tilingScreenAssignments[connectorName] = layoutId;
            m_screenModes[connectorName] = AssignmentEntry::Autotile;
        } else {
            m_screenAssignments[connectorName] = layoutId;
            m_screenModes[connectorName] = AssignmentEntry::Snapping;
        }
    }

    // Update cached per-desktop AssignmentEntry so QML getters return fresh data.
    // The daemon writes KConfig and emits this signal — we mirror the change
    // into our in-memory cache to avoid requiring a full load() round-trip.
    if (virtualDesktop > 0 && !screenName.isEmpty()) {
        QString screenId = Utils::screenIdForName(screenName);
        if (screenId.isEmpty())
            screenId = screenName;
        QString key = QStringLiteral("%1|%2").arg(screenId).arg(virtualDesktop);

        if (layoutId.isEmpty()) {
            m_cachedDesktopAssignments.remove(key);
        } else {
            // Preserve existing entry fields, update the changed side
            AssignmentEntry entry = m_cachedDesktopAssignments.value(key);
            if (isAutotile) {
                entry.mode = AssignmentEntry::Autotile;
                entry.tilingAlgorithm = LayoutId::extractAlgorithmId(layoutId);
            } else {
                entry.mode = AssignmentEntry::Snapping;
                entry.snappingLayout = layoutId;
            }
            m_cachedDesktopAssignments[key] = entry;
        }
        Q_EMIT tilingDesktopAssignmentsChanged();
    }

    Q_EMIT screenAssignmentsChanged();
    Q_EMIT tilingScreenAssignmentsChanged();
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
