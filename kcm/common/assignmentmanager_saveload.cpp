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

    // ── KConfig save ────────────────────────────────────────────────────────
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

        // Delete old per-screen groups (all old formats + new Assignment: in one pass)
        for (const QString& groupName : allGroups) {
            if (groupName.startsWith(QLatin1String("SnappingScreen:"))
                || groupName.startsWith(QLatin1String("TilingScreen:"))
                || groupName.startsWith(QLatin1String("TilingActivity:"))
                || groupName.startsWith(QLatin1String("TilingDesktop:"))
                || (groupName.startsWith(QLatin1String("Assignment:"))
                    && !groupName.contains(QLatin1String(":Desktop:"))
                    && !groupName.contains(QLatin1String(":Activity:")))) {
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

        // Write unified [Assignment:screenId] groups with explicit fields
        // Collect all screen names from both maps
        QSet<QString> allScreenNames;
        for (auto it = m_screenAssignments.constBegin(); it != m_screenAssignments.constEnd(); ++it)
            allScreenNames.insert(it.key());
        for (auto it = m_tilingScreenAssignments.constBegin(); it != m_tilingScreenAssignments.constEnd(); ++it)
            allScreenNames.insert(it.key());

        for (const QString& screenName : std::as_const(allScreenNames)) {
            QString screenId = safeScreenId(screenName);
            KConfigGroup grp = config->group(QStringLiteral("Assignment:") + screenId);

            QString snappingId = m_screenAssignments.value(screenName).toString();
            QString tilingId = m_tilingScreenAssignments.value(screenName).toString();

            // Determine mode: if tiling assignment exists and is autotile, mode is Autotile
            int mode = 0; // Snapping
            QString tilingAlgorithm;
            if (!tilingId.isEmpty() && LayoutId::isAutotile(tilingId)) {
                tilingAlgorithm = LayoutId::extractAlgorithmId(tilingId);
            }

            // If we have a tiling assignment, check if it's the active mode
            // (tiling takes precedence if screen has a tiling assignment)
            if (!tilingAlgorithm.isEmpty()) {
                mode = 1; // Autotile
            }

            grp.writeEntry(QStringLiteral("Mode"), mode);
            if (!snappingId.isEmpty())
                grp.writeEntry(QStringLiteral("SnappingLayout"), snappingId);
            if (!tilingAlgorithm.isEmpty())
                grp.writeEntry(QStringLiteral("TilingAlgorithm"), tilingAlgorithm);
        }

        // Tiling per-desktop and per-activity assignments are stored via the
        // shared pending cache and pushed to the daemon in the batch D-Bus calls
        // below. The daemon writes its own [Assignment:*:Desktop:*] KConfig groups.

        // Tiling quick layout slots
        KConfigGroup tilingSlots = config->group(QStringLiteral("TilingQuickLayoutSlots"));
        tilingSlots.deleteGroup();
        for (auto it = m_tilingQuickLayoutSlots.constBegin(); it != m_tilingQuickLayoutSlots.constEnd(); ++it) {
            tilingSlots.writeEntry(QString::number(it.key()), it.value());
        }

        config->sync();
    }

    // ── Per-Desktop assignments (batch — shared by snapping and tiling) ────
    // Sync autotile IDs in pending assignments before push
    syncAutotileAssignmentIds(m_pendingDesktopAssignments, false);

    if (!m_pendingDesktopAssignments.isEmpty() || !m_clearedDesktopAssignments.isEmpty()) {
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
        QDBusMessage desktopReply =
            KCMDBus::callDaemon(layoutInterface, QStringLiteral("setAllDesktopAssignments"), {desktopAssignments});
        if (desktopReply.type() == QDBusMessage::ErrorMessage) {
            failedOperations.append(QStringLiteral("Per-desktop assignments"));
        }
        m_clearedDesktopAssignments.clear();
        m_pendingDesktopAssignments.clear();
    }

    // ── Per-Activity assignments (batch — shared by snapping and tiling) ───
    // Sync autotile IDs in pending assignments before push
    syncAutotileAssignmentIds(m_pendingActivityAssignments, false);

    if (!m_pendingActivityAssignments.isEmpty() || !m_clearedActivityAssignments.isEmpty()) {
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
        QDBusMessage activityReply =
            KCMDBus::callDaemon(layoutInterface, QStringLiteral("setAllActivityAssignments"), {activityAssignments});
        if (activityReply.type() == QDBusMessage::ErrorMessage) {
            failedOperations.append(QStringLiteral("Per-activity assignments"));
        }
        m_clearedActivityAssignments.clear();
        m_pendingActivityAssignments.clear();
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
    m_tilingQuickLayoutSlots.clear();
    m_cachedDesktopAssignments.clear();
    m_cachedActivityAssignments.clear();
    {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        config->reparseConfiguration(); // Re-read from disk (daemon may have written)
        const QStringList allGroups = config->groupList();

        auto resolveScreenId = [](const QString& screenId) -> QString {
            if (Utils::isConnectorName(screenId))
                return screenId;
            return Utils::screenNameForId(screenId);
        };

        // ── Read all [Assignment:*] KConfig groups (written by daemon) ──
        const QLatin1String assignmentPrefix("Assignment:");
        bool foundAssignmentGroups = false;
        for (const QString& groupName : allGroups) {
            if (!groupName.startsWith(assignmentPrefix))
                continue;

            foundAssignmentGroups = true;

            // Parse group name: Assignment:screenId[:Desktop:N][:Activity:id]
            QString remainder = groupName.mid(assignmentPrefix.size());
            if (remainder.isEmpty())
                continue;

            QString screenId;
            int virtualDesktop = 0;
            QString activity;

            // Extract :Activity:id suffix first (activity IDs may contain colons)
            int actIdx = remainder.indexOf(QLatin1String(":Activity:"));
            if (actIdx >= 0) {
                activity = remainder.mid(actIdx + 10);
                remainder = remainder.left(actIdx);
            }

            // Extract :Desktop:N suffix
            int deskIdx = remainder.indexOf(QLatin1String(":Desktop:"));
            if (deskIdx >= 0) {
                bool ok = false;
                virtualDesktop = remainder.mid(deskIdx + 9).toInt(&ok);
                if (!ok)
                    virtualDesktop = 0;
                remainder = remainder.left(deskIdx);
            }

            screenId = remainder;
            if (screenId.isEmpty())
                continue;

            KConfigGroup grp = config->group(groupName);
            AssignmentEntry entry;
            entry.mode = static_cast<AssignmentEntry::Mode>(grp.readEntry(QStringLiteral("Mode"), 0));
            entry.snappingLayout = grp.readEntry(QStringLiteral("SnappingLayout"), QString());
            entry.tilingAlgorithm = grp.readEntry(QStringLiteral("TilingAlgorithm"), QString());

            if (!entry.isValid())
                continue;

            if (virtualDesktop == 0 && activity.isEmpty()) {
                // Base screen assignment → populate the existing split maps for QML compat
                QString connectorName = resolveScreenId(screenId);
                if (connectorName.isEmpty())
                    continue;
                if (!entry.snappingLayout.isEmpty())
                    m_screenAssignments[connectorName] = entry.snappingLayout;
                if (!entry.tilingAlgorithm.isEmpty())
                    m_tilingScreenAssignments[connectorName] = LayoutId::makeAutotileId(entry.tilingAlgorithm);
            } else if (virtualDesktop > 0 && activity.isEmpty()) {
                // Per-desktop assignment
                QString key = QStringLiteral("%1|%2").arg(screenId).arg(virtualDesktop);
                m_cachedDesktopAssignments[key] = entry;
            } else if (!activity.isEmpty()) {
                // Per-activity assignment
                QString key = QStringLiteral("%1|%2").arg(screenId, activity);
                m_cachedActivityAssignments[key] = entry;
            }
        }

        if (!foundAssignmentGroups) {
            // ── Fall back to old SnappingScreen:/TilingScreen: groups ──
            auto loadPerScreenAssignments = [&](const QLatin1String& prefix, QVariantMap& target) {
                for (const QString& groupName : allGroups) {
                    if (groupName.startsWith(prefix)) {
                        QString sid = groupName.mid(prefix.size());
                        if (sid.isEmpty())
                            continue;
                        KConfigGroup screenGroup = config->group(groupName);
                        QString layoutId = screenGroup.readEntry(QStringLiteral("Assignment"), QString());
                        if (!layoutId.isEmpty()) {
                            QString connectorName = resolveScreenId(sid);
                            if (!connectorName.isEmpty())
                                target[connectorName] = layoutId;
                        }
                    }
                }
            };

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
                }
            }

            // Tiling screen assignments (old format)
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
        }

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
    m_tilingQuickLayoutSlots.clear();
    m_cachedDesktopAssignments.clear();
    m_cachedActivityAssignments.clear();
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
