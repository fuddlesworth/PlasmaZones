// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Save, Load, and Defaults/clear methods for AssignmentManager.
// Split from assignmentmanager.cpp to keep files under 500 lines.

#include "assignmentmanager.h"
#include "dbusutils.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <KConfigGroup>
#include <KSharedConfig>
#include "../../src/config/settings.h"
#include "../../src/core/constants.h"
#include "../../src/core/layout.h"
#include "../../src/core/logging.h"
#include "../../src/core/utils.h"
#include "../../src/autotile/AlgorithmRegistry.h"

namespace PlasmaZones {

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
        QVariantList screens = m_screenListProvider ? m_screenListProvider() : QVariantList{};
        if (screens.isEmpty()) {
            qCWarning(lcKcm) << "Auto-populate: no screens available, cannot create tiling assignments";
        } else {
            QString algo = m_settings->autotileAlgorithm();
            if (algo.isEmpty())
                algo = AlgorithmRegistry::defaultAlgorithmId();
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
    QDBusMessage screenReply =
        KCMDBus::callDaemon(layoutInterface, QStringLiteral("setAllScreenAssignments"), {screenAssignments});
    if (screenReply.type() == QDBusMessage::ErrorMessage) {
        failedOperations.append(QStringLiteral("Screen assignments"));
    }

    // ── Quick layout slots D-Bus push ──────────────────────────────────────
    QVariantMap quickSlots;
    for (int slot = 1; slot <= 9; ++slot) {
        quickSlots[QString::number(slot)] = m_quickLayoutSlots.value(slot, QString());
    }
    QDBusMessage quickReply =
        KCMDBus::callDaemon(layoutInterface, QStringLiteral("setAllQuickLayoutSlots"), {quickSlots});
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
            if (groupName.startsWith(QLatin1String("SnappingScreen:"))
                || groupName.startsWith(QLatin1String("TilingScreen:"))
                || groupName.startsWith(QLatin1String("TilingActivity:"))
                || groupName.startsWith(QLatin1String("TilingDesktop:"))) {
                config->deleteGroup(groupName);
            }
        }

        // Clean up legacy flat-key groups
        for (const auto& legacyName :
             {QStringLiteral("SnappingScreenAssignments"), QStringLiteral("TilingScreenAssignments")}) {
            KConfigGroup legacy = config->group(legacyName);
            if (legacy.exists())
                legacy.deleteGroup();
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
            if (sepIdx <= 0)
                continue;
            QString connectorName = it.key().left(sepIdx);
            QString activityId = it.key().mid(sepIdx + 1);
            QString screenId = safeScreenId(connectorName);
            KConfigGroup actGroup = config->group(QStringLiteral("TilingActivity:%1|%2").arg(screenId, activityId));
            actGroup.writeEntry(QStringLiteral("Assignment"), it.value());
        }

        // Write tiling per-desktop assignments
        for (auto it = m_tilingDesktopAssignments.constBegin(); it != m_tilingDesktopAssignments.constEnd(); ++it) {
            int sepIdx = it.key().lastIndexOf(QLatin1Char('|'));
            if (sepIdx <= 0)
                continue;
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
        QDBusMessage currentReply = KCMDBus::callDaemon(layoutInterface, QStringLiteral("getAllDesktopAssignments"));
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
            if (!it.value().isEmpty())
                desktopAssignments[it.key()] = it.value();
        }
        QDBusMessage desktopReply =
            KCMDBus::callDaemon(layoutInterface, QStringLiteral("setAllDesktopAssignments"), {desktopAssignments});
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
        QDBusMessage currentReply = KCMDBus::callDaemon(layoutInterface, QStringLiteral("getAllActivityAssignments"));
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
            if (!it.value().isEmpty())
                activityAssignments[it.key()] = it.value();
        }
        QDBusMessage activityReply =
            KCMDBus::callDaemon(layoutInterface, QStringLiteral("setAllActivityAssignments"), {activityAssignments});
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

            QDBusMessage layoutReply = KCMDBus::callDaemon(layoutInterface, QStringLiteral("getLayout"), {layoutId});
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
            QDBusMessage updateReply =
                KCMDBus::callDaemon(layoutInterface, QStringLiteral("updateLayout"), {updatedJson});
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
            if (Utils::isConnectorName(screenId))
                return screenId;
            return Utils::screenNameForId(screenId);
        };

        auto loadPerScreenAssignments = [&](const QLatin1String& prefix, QVariantMap& target) {
            for (const QString& groupName : allGroups) {
                if (groupName.startsWith(prefix)) {
                    QString screenId = groupName.mid(prefix.size());
                    if (screenId.isEmpty())
                        continue;
                    KConfigGroup screenGroup = config->group(groupName);
                    QString layoutId = screenGroup.readEntry(QStringLiteral("Assignment"), QString());
                    if (!layoutId.isEmpty()) {
                        QString connectorName = resolveScreenId(screenId);
                        if (!connectorName.isEmpty())
                            target[connectorName] = layoutId;
                    }
                }
            }
        };

        auto loadCompoundAssignments = [&](const QLatin1String& prefix, QMap<QString, QString>& target) {
            for (const QString& groupName : allGroups) {
                if (groupName.startsWith(prefix)) {
                    QString compoundKey = groupName.mid(prefix.size());
                    int sepIdx = compoundKey.lastIndexOf(QLatin1Char('|'));
                    if (sepIdx <= 0)
                        continue;
                    QString screenId = compoundKey.left(sepIdx);
                    QString suffix = compoundKey.mid(sepIdx + 1);
                    if (screenId.isEmpty() || suffix.isEmpty())
                        continue;
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
            if (groupName.startsWith(snappingPrefix)) {
                foundSnappingGroups = true;
                break;
            }
        }
        if (foundSnappingGroups) {
            loadPerScreenAssignments(snappingPrefix, m_screenAssignments);
        } else {
            KConfigGroup legacySnapping = config->group(QStringLiteral("SnappingScreenAssignments"));
            if (legacySnapping.exists()) {
                const QStringList keys = legacySnapping.keyList();
                for (const QString& key : keys) {
                    QString layoutId = legacySnapping.readEntry(key, QString());
                    if (!layoutId.isEmpty())
                        m_screenAssignments[key] = layoutId;
                }
            } else {
                // Fallback: load from daemon for first run
                QDBusMessage assignmentsReply =
                    KCMDBus::callDaemon(layoutInterface, QStringLiteral("getAllScreenAssignments"));
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
                    if (!layoutId.isEmpty())
                        m_tilingScreenAssignments[key] = layoutId;
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
            if (!layoutId.isEmpty())
                m_tilingQuickLayoutSlots[slot] = layoutId;
        }

        // Assignment view mode
        KConfigGroup general = config->group(QStringLiteral("General"));
        m_assignmentViewMode = qBound(0, general.readEntry(QStringLiteral("AssignmentViewMode"), 0), 1);
    }

    // Quick layout slots from daemon
    m_quickLayoutSlots.clear();
    QDBusMessage slotsReply = KCMDBus::callDaemon(layoutInterface, QStringLiteral("getAllQuickLayoutSlots"));
    if (slotsReply.type() == QDBusMessage::ReplyMessage && !slotsReply.arguments().isEmpty()) {
        QVariantMap slotsMap = qdbus_cast<QVariantMap>(slotsReply.arguments().first());
        for (auto it = slotsMap.begin(); it != slotsMap.end(); ++it) {
            bool ok;
            int slotNum = it.key().toInt(&ok);
            if (ok && slotNum >= 1 && slotNum <= 9) {
                QString layoutId = it.value().toString();
                if (!layoutId.isEmpty())
                    m_quickLayoutSlots[slotNum] = layoutId;
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

    Q_EMIT screenAssignmentsChanged();
    Q_EMIT quickLayoutSlotsChanged();
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
