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

    // Suppress screenLayoutChanged D-Bus signals during the entire save batch.
    // Without this, each setAssignmentEntry/clearAssignment call emits a signal
    // carrying the daemon's cascaded/resolved layout ID — which overwrites the
    // KCM's cache with stale data before load() can read the authoritative state.
    // RAII guard ensures the flag is always cleared, even if a D-Bus call fails.
    KCMDBus::callDaemon(layoutInterface, QStringLiteral("setSaveBatchMode"), {true});
    const auto batchGuard = qScopeGuard([&layoutInterface]() {
        KCMDBus::callDaemon(layoutInterface, QStringLiteral("setSaveBatchMode"), {false});
    });

    // ── Screen assignments — individual D-Bus calls per changed screen ────
    // No merge. Each pending entry is pushed as a full AssignmentEntry.

    // Auto-populate tiling assignments when autotiling enabled but none exist
    if (m_settings->autotileEnabled() && m_tilingScreenAssignments.isEmpty()) {
        QVariantList screens = m_screenListProvider ? m_screenListProvider() : QVariantList{};
        if (!screens.isEmpty()) {
            QString algo = m_settings->autotileAlgorithm();
            if (algo.isEmpty())
                algo = AlgorithmRegistry::defaultAlgorithmId();
            const QString autotileId = LayoutId::makeAutotileId(algo);
            for (const QVariant& screenVar : std::as_const(screens)) {
                const QVariantMap screenInfo = screenVar.toMap();
                const QString name = screenInfo.value(QStringLiteral("name")).toString();
                if (!name.isEmpty() && !m_tilingScreenAssignments.contains(name)) {
                    m_tilingScreenAssignments[name] = autotileId;
                    // Add as pending entry
                    AssignmentEntry entry;
                    entry.mode = AssignmentEntry::Autotile;
                    entry.tilingAlgorithm = algo;
                    entry.snappingLayout = m_screenAssignments.value(name).toString();
                    m_pendingScreenEntries[name] = entry;
                }
            }
        }
    }

    // Push pending screen entries individually
    for (auto it = m_pendingScreenEntries.constBegin(); it != m_pendingScreenEntries.constEnd(); ++it) {
        const AssignmentEntry& entry = it.value();
        QDBusMessage reply = KCMDBus::callDaemon(
            layoutInterface, QStringLiteral("setAssignmentEntry"),
            {it.key(), 0, QString(), static_cast<int>(entry.mode), entry.snappingLayout, entry.tilingAlgorithm});
        if (reply.type() == QDBusMessage::ErrorMessage) {
            failedOperations.append(QStringLiteral("Screen assignment: %1").arg(it.key()));
        }
    }
    m_pendingScreenEntries.clear();

    // Clear screens that were fully cleared (both modes empty)
    // Both sets track the same semantic: the screen has no snapping AND no tiling.
    // clearScreenAssignment → m_clearedScreenAssignments (when tiling also empty)
    // clearTilingScreenAssignment → m_clearedTilingScreenAssignments (when snapping also empty)
    QSet<QString> allClearedScreens = m_clearedScreenAssignments | m_clearedTilingScreenAssignments;
    for (const QString& screen : std::as_const(allClearedScreens)) {
        KCMDBus::callDaemon(layoutInterface, QStringLiteral("clearAssignment"), {screen});
    }
    m_clearedScreenAssignments.clear();
    m_clearedTilingScreenAssignments.clear();

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

    // ── KConfig save (KCM-managed entries only) ────────────────────────────
    {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        const QStringList allGroups = config->groupList();

        // Clean up legacy group formats
        for (const QString& groupName : allGroups) {
            if (groupName.startsWith(QLatin1String("SnappingScreen:"))
                || groupName.startsWith(QLatin1String("TilingScreen:"))
                || groupName.startsWith(QLatin1String("TilingActivity:"))
                || groupName.startsWith(QLatin1String("TilingDesktop:"))) {
                config->deleteGroup(groupName);
            }
        }
        for (const auto& legacyName :
             {QStringLiteral("SnappingScreenAssignments"), QStringLiteral("TilingScreenAssignments")}) {
            KConfigGroup legacy = config->group(legacyName);
            if (legacy.exists())
                legacy.deleteGroup();
        }

        // Tiling quick layout slots
        KConfigGroup tilingSlots = config->group(QStringLiteral("TilingQuickLayoutSlots"));
        tilingSlots.deleteGroup();
        for (auto it = m_tilingQuickLayoutSlots.constBegin(); it != m_tilingQuickLayoutSlots.constEnd(); ++it) {
            tilingSlots.writeEntry(QString::number(it.key()), it.value());
        }

        config->sync();
    }

    // ── Per-Desktop assignments — individual D-Bus calls ───────────────────
    // Push pending desktop entries individually
    for (auto it = m_pendingDesktopEntries.constBegin(); it != m_pendingDesktopEntries.constEnd(); ++it) {
        // Parse key: "screenId|desktop"
        int sep = it.key().lastIndexOf(QLatin1Char('|'));
        if (sep < 1)
            continue;
        QString screenName = it.key().left(sep);
        int desktop = it.key().mid(sep + 1).toInt();
        if (desktop < 1)
            continue;

        const AssignmentEntry& entry = it.value();
        QDBusMessage reply = KCMDBus::callDaemon(layoutInterface, QStringLiteral("setAssignmentEntry"),
                                                 {screenName, desktop, QString(), static_cast<int>(entry.mode),
                                                  entry.snappingLayout, entry.tilingAlgorithm});
        if (reply.type() == QDBusMessage::ErrorMessage) {
            failedOperations.append(QStringLiteral("Desktop assignment: %1").arg(it.key()));
        }
    }
    m_pendingDesktopEntries.clear();

    // Clear desktop entries that were fully cleared
    for (const QString& key : std::as_const(m_clearedDesktopAssignments)) {
        int sep = key.lastIndexOf(QLatin1Char('|'));
        if (sep < 1)
            continue;
        QString screenName = key.left(sep);
        int desktop = key.mid(sep + 1).toInt();
        if (desktop < 1)
            continue;
        KCMDBus::callDaemon(layoutInterface, QStringLiteral("clearAssignmentForScreenDesktop"), {screenName, desktop});
    }
    m_clearedDesktopAssignments.clear();

    // ── Per-Activity assignments — individual D-Bus calls ──────────────────
    for (auto it = m_pendingActivityEntries.constBegin(); it != m_pendingActivityEntries.constEnd(); ++it) {
        int sep = it.key().lastIndexOf(QLatin1Char('|'));
        if (sep < 1)
            continue;
        QString screenName = it.key().left(sep);
        QString activityId = it.key().mid(sep + 1);
        if (activityId.isEmpty())
            continue;

        const AssignmentEntry& entry = it.value();
        QDBusMessage reply = KCMDBus::callDaemon(
            layoutInterface, QStringLiteral("setAssignmentEntry"),
            {screenName, 0, activityId, static_cast<int>(entry.mode), entry.snappingLayout, entry.tilingAlgorithm});
        if (reply.type() == QDBusMessage::ErrorMessage) {
            failedOperations.append(QStringLiteral("Activity assignment: %1").arg(it.key()));
        }
    }
    m_pendingActivityEntries.clear();

    for (const QString& key : std::as_const(m_clearedActivityAssignments)) {
        int sep = key.lastIndexOf(QLatin1Char('|'));
        if (sep < 1)
            continue;
        QString screenName = key.left(sep);
        QString activityId = key.mid(sep + 1);
        if (activityId.isEmpty())
            continue;
        KCMDBus::callDaemon(layoutInterface, QStringLiteral("clearAssignmentForScreenActivity"),
                            {screenName, activityId});
    }
    m_clearedActivityAssignments.clear();

    // batchGuard (RAII) calls setSaveBatchMode(false) when save() exits.

    // Trigger resnap/retile + OSD for all screens. This is a separate D-Bus call
    // that runs AFTER all assignments are committed, avoiding feedback loops with
    // the daemon's settingsChanged handler.
    KCMDBus::callDaemon(layoutInterface, QStringLiteral("applyAssignmentChanges"));

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
    m_screenModes.clear();
    m_clearedScreenAssignments.clear();
    m_clearedTilingScreenAssignments.clear();
    m_tilingQuickLayoutSlots.clear();
    m_cachedDesktopAssignments.clear();
    m_cachedActivityAssignments.clear();
    {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        config->reparseConfiguration(); // Re-read from disk (daemon may have written)
        const QStringList allGroups = config->groupList();

        // Screen IDs are EDID-based (e.g. "LG Electronics:LG Ultra HD:115107").
        // Used as-is — the daemon writes EDID IDs to KConfig and QML passes
        // EDID IDs (from getScreens()).

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

            // Accept all entries — mode-only entries (empty layout fields) are
            // valid when explicitly set to preserve mode at this context level.

            if (virtualDesktop == 0 && activity.isEmpty()) {
                // Base screen assignment → populate the existing split maps for QML compat
                QString connectorName = screenId;
                if (connectorName.isEmpty())
                    continue;
                if (!entry.snappingLayout.isEmpty())
                    m_screenAssignments[connectorName] = entry.snappingLayout;
                if (!entry.tilingAlgorithm.isEmpty())
                    m_tilingScreenAssignments[connectorName] = LayoutId::makeAutotileId(entry.tilingAlgorithm);
                m_screenModes[connectorName] = entry.mode;
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
                            QString connectorName = sid;
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
    m_screenModes.clear();
    m_clearedScreenAssignments.clear();
    m_clearedTilingScreenAssignments.clear();
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
    m_clearedDesktopAssignments.clear();
    m_clearedActivityAssignments.clear();
    m_pendingScreenEntries.clear();
    m_pendingDesktopEntries.clear();
    m_pendingActivityEntries.clear();
    m_pendingAppRules.clear();
}

} // namespace PlasmaZones
