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

    // Save zone assignments with full windowIds (appId|uuid) to preserve multi-instance
    // distinction. On daemon-only restart (KWin still running), UUIDs are stable so
    // exact matching prevents restoring the wrong instance of a multi-instance app.
    // Also save appId-keyed format as fallback for KWin restarts (UUIDs change).
    QJsonArray fullAssignments;
    QJsonObject assignmentsObj;
    for (auto it = m_service->zoneAssignments().constBegin(); it != m_service->zoneAssignments().constEnd(); ++it) {
        QJsonObject entry;
        entry[QLatin1String("windowId")] = it.key();
        entry[QLatin1String("zoneIds")] = toJsonArray(it.value());
        entry[QLatin1String("screen")] = Utils::screenIdForName(m_service->screenAssignments().value(it.key()));
        entry[QLatin1String("desktop")] = m_service->desktopAssignments().value(it.key(), 0);
        fullAssignments.append(entry);

        QString appId = Utils::extractAppId(it.key());
        assignmentsObj[appId] = toJsonArray(it.value());
    }
    tracking.writeEntry(QStringLiteral("WindowZoneAssignmentsFull"),
                        QString::fromUtf8(QJsonDocument(fullAssignments).toJson(QJsonDocument::Compact)));
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

    // Save pre-tile geometries so float-toggle restores to the correct position
    // even after daemon restart (windows stay at their zone positions across restarts).
    // Save full windowId format for daemon-only restarts (UUIDs stable, multi-instance distinction).
    // Save appId format as fallback for KWin restarts (UUIDs change).
    tracking.writeEntry(QStringLiteral("PreTileGeometriesFull"),
                        serializeGeometryMapFull(m_service->preTileGeometries()));
    tracking.writeEntry(QStringLiteral("PreTileGeometries"), serializeGeometryMap(m_service->preTileGeometries()));
    // Remove old split keys if present (migration)
    tracking.deleteEntry(QStringLiteral("PreSnapGeometries"));
    tracking.deleteEntry(QStringLiteral("PreAutotileGeometries"));

    // Save last used zone info (from service)
    tracking.writeEntry(QStringLiteral("LastUsedZoneId"), m_service->lastUsedZoneId());
    // Note: Other last-used fields would need accessors in service

    // Float state is ephemeral (session-only) — do NOT persist across restarts.
    // Clear any stale entry from older versions so restored sessions start clean.
    tracking.deleteEntry(QStringLiteral("FloatingWindows"));

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
    qCInfo(lcDbusWindow) << "Saved state to KConfig:"
                         << "zones=" << assignmentsObj.size() << "screens=" << screenAssignmentsObj.size()
                         << "desktops=" << desktopAssignmentsObj.size() << "pending=" << pendingQueuesObj.size()
                         << "preTile=" << m_service->preTileGeometries().size()
                         << "preFloat=" << preFloatZonesObj.size() << "userSnapped=" << userSnappedArray.size();
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
    // 1. Active zone assignments — windows still open at daemon shutdown
    // 2. Pending restore queues (PendingRestoreQueues) — windows closed before shutdown
    //
    // If full windowId format is available (WindowZoneAssignmentsFull), load exact
    // assignments into m_windowZoneAssignments for precise instance matching after
    // daemon-only restart (KWin still running, UUIDs stable). Also build appId-keyed
    // pending queues as fallback for KWin restarts (UUIDs change).

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

    // Phase 1: Load active zone assignments
    // Try full-windowId format first (preserves multi-instance distinction)
    QHash<QString, QStringList> fullZones;
    QHash<QString, QString> fullScreens;
    QHash<QString, int> fullDesktops;
    bool hasFullFormat = false;

    QString fullJson = tracking.readEntry(QStringLiteral("WindowZoneAssignmentsFull"), QString());
    if (!fullJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(fullJson.toUtf8());
        if (doc.isArray()) {
            hasFullFormat = true;
            for (const QJsonValue& val : doc.array()) {
                if (!val.isObject()) {
                    continue;
                }
                QJsonObject entry = val.toObject();
                QString windowId = entry[QLatin1String("windowId")].toString();
                if (windowId.isEmpty()) {
                    continue;
                }
                QStringList zoneIds;
                for (const QJsonValue& v : entry[QLatin1String("zoneIds")].toArray()) {
                    if (v.isString() && !v.toString().isEmpty()) {
                        zoneIds.append(v.toString());
                    }
                }
                if (zoneIds.isEmpty()) {
                    continue;
                }
                QString screen = resolveScreen(entry[QLatin1String("screen")].toString());
                int desktop = entry[QLatin1String("desktop")].toInt(0);

                fullZones[windowId] = zoneIds;
                fullScreens[windowId] = screen;
                fullDesktops[windowId] = desktop;

                // Also build appId-keyed pending queue as fallback for KWin restarts
                // (UUIDs change, so exact matches won't work). For daemon-only restarts,
                // calculateRestoreFromSession() guards against wrong-instance consumption.
                QString appId = Utils::extractAppId(windowId);
                PendingRestore pending;
                pending.zoneIds = zoneIds;
                pending.screenName = screen;
                pending.virtualDesktop = desktop;
                pendingQueues[appId].append(pending);
            }
        }
    }

    if (hasFullFormat) {
        // Load exact assignments so isWindowSnapped() returns true for daemon-only restarts.
        // calculateRestoreFromSession() checks for exact-match siblings before allowing
        // FIFO consumption, preventing wrong-instance restore for multi-instance apps.
        m_service->setActiveAssignments(fullZones, fullScreens, fullDesktops);
    } else {
        // Legacy format: appId-keyed (no multi-instance distinction)
        QHash<QString, QStringList> activeZones =
            parseZoneListMap(tracking.readEntry(QStringLiteral("WindowZoneAssignments"), QString()));

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
            pendingQueues[it.key()].append(entry);
        }
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

    // Load pre-tile geometries (with migration from old split keys)
    QHash<QString, QRect> preTileGeometries;
    auto loadGeometries = [](const QString& json, QHash<QString, QRect>& out) {
        if (json.isEmpty()) {
            return;
        }
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        if (!doc.isObject()) {
            return;
        }
        QJsonObject obj = doc.object();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            if (it.value().isObject()) {
                QJsonObject geomObj = it.value().toObject();
                QRect geom(geomObj[QLatin1String("x")].toInt(), geomObj[QLatin1String("y")].toInt(),
                           geomObj[QLatin1String("width")].toInt(), geomObj[QLatin1String("height")].toInt());
                if (geom.width() > 0 && geom.height() > 0) {
                    out[it.key()] = geom;
                }
            }
        }
    };

    // Load full windowId format first (preserves multi-instance distinction for
    // daemon-only restarts where KWin UUIDs are still valid). Each entry stores
    // both the full windowId key AND the appId key, mirroring storePreTileGeometry().
    QString fullTileJson = tracking.readEntry(QStringLiteral("PreTileGeometriesFull"), QString());
    if (!fullTileJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(fullTileJson.toUtf8());
        if (doc.isArray()) {
            for (const QJsonValue& val : doc.array()) {
                if (!val.isObject()) {
                    continue;
                }
                QJsonObject entry = val.toObject();
                QString windowId = entry[QLatin1String("windowId")].toString();
                if (windowId.isEmpty()) {
                    continue;
                }
                QRect geom(entry[QLatin1String("x")].toInt(), entry[QLatin1String("y")].toInt(),
                           entry[QLatin1String("width")].toInt(), entry[QLatin1String("height")].toInt());
                if (geom.width() > 0 && geom.height() > 0) {
                    preTileGeometries[windowId] = geom;
                    // Also store under appId for fallback (mirrors storePreTileGeometry)
                    QString appId = Utils::extractAppId(windowId);
                    if (appId != windowId) {
                        preTileGeometries[appId] = geom;
                    }
                }
            }
        }
    }

    // Load appId-keyed format (fallback for KWin restarts or entries without full format)
    QString tileJson = tracking.readEntry(QStringLiteral("PreTileGeometries"), QString());
    if (!tileJson.isEmpty()) {
        // Only fill in keys not already loaded from full format
        QHash<QString, QRect> appIdGeometries;
        loadGeometries(tileJson, appIdGeometries);
        for (auto it = appIdGeometries.constBegin(); it != appIdGeometries.constEnd(); ++it) {
            if (!preTileGeometries.contains(it.key())) {
                preTileGeometries[it.key()] = it.value();
            }
        }
    } else if (preTileGeometries.isEmpty()) {
        // Migration: merge old split keys
        loadGeometries(tracking.readEntry(QStringLiteral("PreAutotileGeometries"), QString()), preTileGeometries);
        loadGeometries(tracking.readEntry(QStringLiteral("PreSnapGeometries"), QString()), preTileGeometries);
    }
    m_service->setPreTileGeometries(preTileGeometries);

    // Load last used zone info
    QString lastZoneId = tracking.readEntry(QStringLiteral("LastUsedZoneId"), QString());
    QString lastScreenName = tracking.readEntry(QStringLiteral("LastUsedScreenName"), QString());
    QString lastZoneClass = tracking.readEntry(QStringLiteral("LastUsedZoneClass"), QString());
    int lastDesktop = tracking.readEntry(QStringLiteral("LastUsedDesktop"), 0);
    m_service->setLastUsedZone(lastZoneId, lastScreenName, lastZoneClass, lastDesktop);

    // Float state is ephemeral (session-only) — skip loading.
    // Any stale FloatingWindows entries from older versions are cleaned up in saveState().

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
            qCInfo(lcDbusWindow) << "  pending snap: app=" << it.key() << "zone=" << entry.zoneIds;
        }
    }
    qCInfo(lcDbusWindow) << "Loaded state from KConfig: pendingApps=" << pendingQueues.size()
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
