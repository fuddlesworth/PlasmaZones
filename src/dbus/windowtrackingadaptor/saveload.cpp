// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../windowtrackingadaptor.h"
#include "internal.h"
#include "../../core/interfaces.h"
#include "../../core/layoutmanager.h"
#include "../../core/layout.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../core/virtualdesktopmanager.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QTimer>

namespace PlasmaZones {
using namespace WindowTrackingInternal;

static QHash<QString, QStringList> parseZoneListMap(const QString& json)
{
    QHash<QString, QStringList> result;
    if (json.isEmpty()) {
        return result;
    }
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject()) {
        return result;
    }
    QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (it.value().isArray()) {
            QStringList zones;
            for (const QJsonValue& v : it.value().toArray()) {
                if (v.isString() && !v.toString().isEmpty()) {
                    zones.append(v.toString());
                }
            }
            if (!zones.isEmpty()) {
                result[it.key()] = zones;
            }
        } else if (it.value().isString() && !it.value().toString().isEmpty()) {
            // Backward compat: old format stored single zone ID string
            result[it.key()] = QStringList{it.value().toString()};
        }
    }
    return result;
}

void WindowTrackingAdaptor::saveState()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup tracking = config->group(QStringLiteral("WindowTracking"));

    // Save active layout ID so we can restore it after daemon restart.
    if (m_layoutManager && m_layoutManager->activeLayout()) {
        tracking.writeEntry(QStringLiteral("ActiveLayoutId"), m_layoutManager->activeLayout()->id().toString());
    }

    // Save zone assignments as JSON arrays (from service state, keyed by appId)
    QJsonObject assignmentsObj;
    for (auto it = m_service->zoneAssignments().constBegin(); it != m_service->zoneAssignments().constEnd(); ++it) {
        QString appId = Utils::extractAppId(it.key());
        assignmentsObj[appId] = toJsonArray(it.value());
    }
    tracking.writeEntry(QStringLiteral("WindowZoneAssignments"),
                        QString::fromUtf8(QJsonDocument(assignmentsObj).toJson(QJsonDocument::Compact)));

    // Save screen assignments (translate connector names to stable screen IDs for persistence)
    QJsonObject screenAssignmentsObj;
    for (auto it = m_service->screenAssignments().constBegin(); it != m_service->screenAssignments().constEnd(); ++it) {
        QString appId = Utils::extractAppId(it.key());
        screenAssignmentsObj[appId] = Utils::screenIdForName(it.value());
    }
    tracking.writeEntry(QStringLiteral("WindowScreenAssignments"),
                        QString::fromUtf8(QJsonDocument(screenAssignmentsObj).toJson(QJsonDocument::Compact)));

    // Save active desktop assignments (for cross-restart persistence)
    QJsonObject desktopAssignmentsObj;
    for (auto it = m_service->desktopAssignments().constBegin(); it != m_service->desktopAssignments().constEnd();
         ++it) {
        if (it.value() > 0) {
            QString appId = Utils::extractAppId(it.key());
            desktopAssignmentsObj[appId] = it.value();
        }
    }
    tracking.writeEntry(QStringLiteral("WindowDesktopAssignments"),
                        QString::fromUtf8(QJsonDocument(desktopAssignmentsObj).toJson(QJsonDocument::Compact)));

    // Save pending restore queues as JSON: appId -> array of entry objects
    // Each entry: {zoneIds: [...], screen: "...", desktop: N, layout: "...", zoneNumbers: [...]}
    QJsonObject pendingQueuesObj;
    for (auto it = m_service->pendingRestoreQueues().constBegin(); it != m_service->pendingRestoreQueues().constEnd();
         ++it) {
        QJsonArray entryArray;
        for (const auto& entry : it.value()) {
            QJsonObject entryObj;
            entryObj[QLatin1String("zoneIds")] = toJsonArray(entry.zoneIds);
            if (!entry.screenName.isEmpty()) {
                entryObj[QLatin1String("screen")] = Utils::screenIdForName(entry.screenName);
            }
            if (entry.virtualDesktop > 0) {
                entryObj[QLatin1String("desktop")] = entry.virtualDesktop;
            }
            if (!entry.layoutId.isEmpty()) {
                entryObj[QLatin1String("layout")] = entry.layoutId;
            }
            if (!entry.zoneNumbers.isEmpty()) {
                QJsonArray numArray;
                for (int num : entry.zoneNumbers) {
                    numArray.append(num);
                }
                entryObj[QLatin1String("zoneNumbers")] = numArray;
            }
            entryArray.append(entryObj);
        }
        if (!entryArray.isEmpty()) {
            pendingQueuesObj[it.key()] = entryArray;
        }
    }
    tracking.writeEntry(QStringLiteral("PendingRestoreQueues"),
                        QString::fromUtf8(QJsonDocument(pendingQueuesObj).toJson(QJsonDocument::Compact)));

    // Clean up obsolete keys from old format
    tracking.deleteEntry(QStringLiteral("PendingWindowScreenAssignments"));
    tracking.deleteEntry(QStringLiteral("PendingWindowDesktopAssignments"));
    tracking.deleteEntry(QStringLiteral("PendingWindowLayoutAssignments"));
    tracking.deleteEntry(QStringLiteral("PendingWindowZoneNumbers"));

    // Save pre-snap geometries (convert to appId for cross-restart persistence)
    tracking.writeEntry(QStringLiteral("PreSnapGeometries"), serializeGeometryMap(m_service->preSnapGeometries()));

    // Save pre-autotile geometries (convert to appId for cross-restart persistence)
    tracking.writeEntry(QStringLiteral("PreAutotileGeometries"),
                        serializeGeometryMap(m_service->preAutotileGeometries()));

    // Save last used zone info (from service)
    tracking.writeEntry(QStringLiteral("LastUsedZoneId"), m_service->lastUsedZoneId());
    // Note: Other last-used fields would need accessors in service

    // Save floating windows (convert to appId for cross-restart persistence, deduplicate)
    QJsonArray floatingArray;
    QSet<QString> savedFloatingIds;
    for (const QString& windowId : m_service->floatingWindows()) {
        QString appId = Utils::extractAppId(windowId);
        if (!appId.isEmpty() && !savedFloatingIds.contains(appId)) {
            floatingArray.append(appId);
            savedFloatingIds.insert(appId);
        }
    }
    tracking.writeEntry(QStringLiteral("FloatingWindows"),
                        QString::fromUtf8(QJsonDocument(floatingArray).toJson(QJsonDocument::Compact)));

    // Save pre-float zone assignments (for unfloating after session restore).
    // Runtime keys may be full window IDs; convert to
    // app IDs for cross-restart compatibility.
    QJsonObject preFloatZonesObj;
    for (auto it = m_service->preFloatZoneAssignments().constBegin();
         it != m_service->preFloatZoneAssignments().constEnd(); ++it) {
        QString key = Utils::extractAppId(it.key());
        preFloatZonesObj[key] = toJsonArray(it.value());
    }
    tracking.writeEntry(QStringLiteral("PreFloatZoneAssignments"),
                        QString::fromUtf8(QJsonDocument(preFloatZonesObj).toJson(QJsonDocument::Compact)));

    // Save pre-float screen assignments (for unfloating to correct monitor).
    // Same app ID conversion as above, plus translate to screen IDs.
    QJsonObject preFloatScreensObj;
    for (auto it = m_service->preFloatScreenAssignments().constBegin();
         it != m_service->preFloatScreenAssignments().constEnd(); ++it) {
        QString key = Utils::extractAppId(it.key());
        preFloatScreensObj[key] = Utils::screenIdForName(it.value());
    }
    tracking.writeEntry(QStringLiteral("PreFloatScreenAssignments"),
                        QString::fromUtf8(QJsonDocument(preFloatScreensObj).toJson(QJsonDocument::Compact)));

    // Save user-snapped classes
    QJsonArray userSnappedArray;
    for (const QString& windowClass : m_service->userSnappedClasses()) {
        userSnappedArray.append(windowClass);
    }
    tracking.writeEntry(QStringLiteral("UserSnappedClasses"),
                        QString::fromUtf8(QJsonDocument(userSnappedArray).toJson(QJsonDocument::Compact)));

    config->sync();
    qCInfo(lcDbusWindow) << "Saved state to KConfig";
}

void WindowTrackingAdaptor::requestReapplyWindowGeometries()
{
    Q_EMIT reapplyWindowGeometriesRequested();
}

void WindowTrackingAdaptor::saveStateOnShutdown()
{
    if (m_saveTimer && m_saveTimer->isActive()) {
        m_saveTimer->stop();
    }
    saveState();
}

void WindowTrackingAdaptor::loadState()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup tracking = config->group(QStringLiteral("WindowTracking"));

    // Build pending restore queues from:
    // 1. Active zone assignments (WindowZoneAssignments) — windows still open at daemon shutdown
    // 2. Pending restore queues (PendingRestoreQueues) — windows closed before shutdown
    // Active assignments become single-entry queues; pending queue entries are appended after.

    using PendingRestore = WindowTrackingService::PendingRestore;
    QHash<QString, QList<PendingRestore>> pendingQueues;

    // Helper: resolve stored screen value (may be screen ID or connector name) to current connector
    auto resolveScreen = [](const QString& storedScreen) -> QString {
        if (storedScreen.isEmpty()) {
            return storedScreen;
        }
        if (!Utils::isConnectorName(storedScreen)) {
            QString connectorName = Utils::screenNameForId(storedScreen);
            if (!connectorName.isEmpty()) {
                return connectorName;
            }
        }
        return storedScreen;
    };

    // Phase 1: Convert active zone assignments into pending entries
    QHash<QString, QStringList> activeZones =
        parseZoneListMap(tracking.readEntry(QStringLiteral("WindowZoneAssignments"), QString()));

    // Load active screen and desktop assignments for enriching active zone entries
    QHash<QString, QString> activeScreens;
    QString activeScreensJson = tracking.readEntry(QStringLiteral("WindowScreenAssignments"), QString());
    if (!activeScreensJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(activeScreensJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isString() && !it.value().toString().isEmpty()) {
                    activeScreens[it.key()] = resolveScreen(it.value().toString());
                }
            }
        }
    }

    QHash<QString, int> activeDesktops;
    QString activeDesktopsJson = tracking.readEntry(QStringLiteral("WindowDesktopAssignments"), QString());
    if (!activeDesktopsJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(activeDesktopsJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isDouble() && it.value().toInt() > 0) {
                    activeDesktops[it.key()] = it.value().toInt();
                }
            }
        }
    }

    for (auto it = activeZones.constBegin(); it != activeZones.constEnd(); ++it) {
        PendingRestore entry;
        entry.zoneIds = it.value();
        entry.screenName = activeScreens.value(it.key());
        entry.virtualDesktop = activeDesktops.value(it.key(), 0);
        // No layout or zone number info for active assignments (not needed for merge)
        pendingQueues[it.key()].append(entry);
    }

    // Phase 2: Load pending restore queues (new format: appId -> array of entry objects)
    QString pendingQueuesJson = tracking.readEntry(QStringLiteral("PendingRestoreQueues"), QString());
    if (!pendingQueuesJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(pendingQueuesJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (!it.value().isArray()) {
                    continue;
                }
                for (const QJsonValue& entryVal : it.value().toArray()) {
                    if (!entryVal.isObject()) {
                        continue;
                    }
                    QJsonObject entryObj = entryVal.toObject();
                    PendingRestore entry;
                    // Parse zoneIds
                    for (const QJsonValue& v : entryObj[QLatin1String("zoneIds")].toArray()) {
                        if (v.isString() && !v.toString().isEmpty()) {
                            entry.zoneIds.append(v.toString());
                        }
                    }
                    if (entry.zoneIds.isEmpty()) {
                        continue;
                    }
                    entry.screenName = resolveScreen(entryObj[QLatin1String("screen")].toString());
                    entry.virtualDesktop = entryObj[QLatin1String("desktop")].toInt(0);
                    entry.layoutId = entryObj[QLatin1String("layout")].toString();
                    for (const QJsonValue& v : entryObj[QLatin1String("zoneNumbers")].toArray()) {
                        if (v.isDouble()) {
                            entry.zoneNumbers.append(v.toInt());
                        }
                    }
                    pendingQueues[it.key()].append(entry);
                }
            }
        }
    }

    m_service->setPendingRestoreQueues(pendingQueues);

    // Load pre-snap geometries
    QHash<QString, QRect> preSnapGeometries;
    QString geometriesJson = tracking.readEntry(QStringLiteral("PreSnapGeometries"), QString());
    if (!geometriesJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(geometriesJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isObject()) {
                    QJsonObject geomObj = it.value().toObject();
                    QRect geom(geomObj[QLatin1String("x")].toInt(), geomObj[QLatin1String("y")].toInt(),
                               geomObj[QLatin1String("width")].toInt(), geomObj[QLatin1String("height")].toInt());
                    if (geom.width() > 0 && geom.height() > 0) {
                        preSnapGeometries[it.key()] = geom;
                    }
                }
            }
        }
    }
    m_service->setPreSnapGeometries(preSnapGeometries);

    // Load pre-autotile geometries
    QHash<QString, QRect> preAutotileGeometries;
    QString autotileGeometriesJson = tracking.readEntry(QStringLiteral("PreAutotileGeometries"), QString());
    if (!autotileGeometriesJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(autotileGeometriesJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isObject()) {
                    QJsonObject geomObj = it.value().toObject();
                    QRect geom(geomObj[QLatin1String("x")].toInt(), geomObj[QLatin1String("y")].toInt(),
                               geomObj[QLatin1String("width")].toInt(), geomObj[QLatin1String("height")].toInt());
                    if (geom.width() > 0 && geom.height() > 0) {
                        preAutotileGeometries[it.key()] = geom;
                    }
                }
            }
        }
    }
    m_service->setPreAutotileGeometries(preAutotileGeometries);

    // Load last used zone info
    QString lastZoneId = tracking.readEntry(QStringLiteral("LastUsedZoneId"), QString());
    QString lastScreenName = tracking.readEntry(QStringLiteral("LastUsedScreenName"), QString());
    QString lastZoneClass = tracking.readEntry(QStringLiteral("LastUsedZoneClass"), QString());
    int lastDesktop = tracking.readEntry(QStringLiteral("LastUsedDesktop"), 0);
    m_service->setLastUsedZone(lastZoneId, lastScreenName, lastZoneClass, lastDesktop);

    // Load floating windows
    QSet<QString> floatingWindows;
    QString floatingJson = tracking.readEntry(QStringLiteral("FloatingWindows"), QString());
    if (!floatingJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(floatingJson.toUtf8());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const QJsonValue& val : arr) {
                if (val.isString()) {
                    floatingWindows.insert(val.toString());
                }
            }
        }
    }
    m_service->setFloatingWindows(floatingWindows);

    // Load pre-float zone assignments (for unfloating after session restore)
    // Supports both old format (string) and new format (JSON array) for backward compat
    QHash<QString, QStringList> preFloatZones =
        parseZoneListMap(tracking.readEntry(QStringLiteral("PreFloatZoneAssignments"), QString()));
    m_service->setPreFloatZoneAssignments(preFloatZones);

    // Load pre-float screen assignments (for unfloating to correct monitor)
    // Values may be screen IDs (new) or connector names (legacy) — resolve to current connector name
    QHash<QString, QString> preFloatScreens;
    QString preFloatScreensJson = tracking.readEntry(QStringLiteral("PreFloatScreenAssignments"), QString());
    if (!preFloatScreensJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(preFloatScreensJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isString()) {
                    QString storedScreen = it.value().toString();
                    if (!Utils::isConnectorName(storedScreen)) {
                        QString connectorName = Utils::screenNameForId(storedScreen);
                        if (!connectorName.isEmpty()) {
                            storedScreen = connectorName;
                        }
                    }
                    preFloatScreens[it.key()] = storedScreen;
                }
            }
        }
    }
    m_service->setPreFloatScreenAssignments(preFloatScreens);

    // Load user-snapped classes
    QSet<QString> userSnappedClasses;
    QString userSnappedJson = tracking.readEntry(QStringLiteral("UserSnappedClasses"), QString());
    if (!userSnappedJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(userSnappedJson.toUtf8());
        if (doc.isArray()) {
            QJsonArray arr = doc.array();
            for (const QJsonValue& val : arr) {
                if (val.isString()) {
                    userSnappedClasses.insert(val.toString());
                }
            }
        }
    }
    m_service->setUserSnappedClasses(userSnappedClasses);

    // Restore active layout from previous session so that previousLayout() is correct
    // on the next layout switch. Without this, the daemon starts with defaultLayout()
    // which may differ from the layout the user was on, causing resnap to build its
    // zone-position map from the wrong layout's zones (all entries get pos=0 → empty buffer).
    QString savedActiveLayoutId = tracking.readEntry(QStringLiteral("ActiveLayoutId"), QString());
    if (!savedActiveLayoutId.isEmpty() && m_layoutManager) {
        auto savedUuid = Utils::parseUuid(savedActiveLayoutId);
        if (savedUuid) {
            Layout* savedLayout = m_layoutManager->layoutById(*savedUuid);
            if (savedLayout && savedLayout != m_layoutManager->activeLayout()) {
                qCInfo(lcDbusWindow) << "Restoring active layout from previous session:" << savedLayout->name();
                m_layoutManager->setActiveLayoutById(*savedUuid);
            }
        }
    }

    int totalPendingEntries = 0;
    for (auto it = pendingQueues.constBegin(); it != pendingQueues.constEnd(); ++it) {
        totalPendingEntries += it.value().size();
        for (const auto& entry : it.value()) {
            qCInfo(lcDbusWindow) << "  Pending snap app=" << it.key() << " zone=" << entry.zoneIds;
        }
    }
    qCInfo(lcDbusWindow) << "Loaded state from KConfig pendingApps=" << pendingQueues.size()
                         << "totalEntries=" << totalPendingEntries;
    if (!pendingQueues.isEmpty()) {
        m_hasPendingRestores = true;
        tryEmitPendingRestoresAvailable();
    }
}

void WindowTrackingAdaptor::scheduleSaveState()
{
    if (m_saveTimer) {
        m_saveTimer->start();
    } else {
        saveState();
    }
}

} // namespace PlasmaZones
