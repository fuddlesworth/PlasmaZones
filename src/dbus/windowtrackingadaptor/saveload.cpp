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
#include "../../config/iconfigbackend.h"
#include "../../config/configkeys.h"
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
    auto tracking = m_sessionBackend->group(ConfigKeys::windowTrackingGroup());

    // Save active layout ID so we can restore it after daemon restart.
    if (m_layoutManager && m_layoutManager->activeLayout()) {
        tracking->writeString(ConfigKeys::activeLayoutIdKey(), m_layoutManager->activeLayout()->id().toString());
    }

    // Save zone assignments with full windowIds (appId|uuid) to preserve multi-instance
    // distinction. On daemon-only restart (KWin still running), UUIDs are stable so
    // exact matching prevents restoring the wrong instance of a multi-instance app.
    QJsonArray fullAssignments;
    for (auto it = m_service->zoneAssignments().constBegin(); it != m_service->zoneAssignments().constEnd(); ++it) {
        QJsonObject entry;
        entry[QLatin1String("windowId")] = it.key();
        entry[QLatin1String("zoneIds")] = toJsonArray(it.value());
        entry[QLatin1String("screen")] = Utils::screenIdForName(m_service->screenAssignments().value(it.key()));
        entry[QLatin1String("desktop")] = m_service->desktopAssignments().value(it.key(), 0);
        fullAssignments.append(entry);
    }
    tracking->writeString(ConfigKeys::windowZoneAssignmentsFullKey(),
                          QString::fromUtf8(QJsonDocument(fullAssignments).toJson(QJsonDocument::Compact)));

    // Save pending restore queues as JSON: appId -> array of entry objects
    // Each entry: {zoneIds: [...], screen: "...", desktop: N, layout: "...", zoneNumbers: [...]}
    QJsonObject pendingQueuesObj;
    for (auto it = m_service->pendingRestoreQueues().constBegin(); it != m_service->pendingRestoreQueues().constEnd();
         ++it) {
        QJsonArray entryArray;
        for (const auto& entry : it.value()) {
            QJsonObject entryObj;
            entryObj[QLatin1String("zoneIds")] = toJsonArray(entry.zoneIds);
            if (!entry.screenId.isEmpty()) {
                entryObj[QLatin1String("screen")] = Utils::screenIdForName(entry.screenId);
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
    tracking->writeString(ConfigKeys::pendingRestoreQueuesKey(),
                          QString::fromUtf8(QJsonDocument(pendingQueuesObj).toJson(QJsonDocument::Compact)));

    // Clean up obsolete keys from old formats
    tracking->deleteKey(ConfigKeys::obsoletePendingWindowScreenAssignmentsKey());
    tracking->deleteKey(ConfigKeys::obsoletePendingWindowDesktopAssignmentsKey());
    tracking->deleteKey(ConfigKeys::obsoletePendingWindowLayoutAssignmentsKey());
    tracking->deleteKey(ConfigKeys::obsoletePendingWindowZoneNumbersKey());
    tracking->deleteKey(ConfigKeys::obsoleteWindowZoneAssignmentsKey());
    tracking->deleteKey(ConfigKeys::obsoleteWindowScreenAssignmentsKey());
    tracking->deleteKey(ConfigKeys::obsoleteWindowDesktopAssignmentsKey());

    // Save pre-tile geometries so float-toggle restores to the correct position
    // even after daemon restart (windows stay at their zone positions across restarts).
    // Save full windowId format for daemon-only restarts (UUIDs stable, multi-instance distinction).
    // Save appId format as fallback for KWin restarts (UUIDs change).
    tracking->writeString(ConfigKeys::preTileGeometriesFullKey(),
                          serializeGeometryMapFull(m_service->preTileGeometries()));
    tracking->writeString(ConfigKeys::preTileGeometriesKey(),
                          serializeGeometryMap(m_service->preTileGeometries(), m_service));

    // Save last used zone info (from service)
    tracking->writeString(ConfigKeys::lastUsedZoneIdKey(), m_service->lastUsedZoneId());
    // Note: Other last-used fields would need accessors in service

    // Float state is ephemeral (session-only) — do NOT persist across restarts.
    // Clear any stale entry from older versions so restored sessions start clean.
    tracking->deleteKey(ConfigKeys::obsoleteFloatingWindowsKey());

    // Save pre-float zone assignments (for unfloating after session restore).
    // Runtime keys are full window IDs; convert to the CURRENT app class so
    // that a window which renamed mid-session persists under the live class.
    QJsonObject preFloatZonesObj;
    for (auto it = m_service->preFloatZoneAssignments().constBegin();
         it != m_service->preFloatZoneAssignments().constEnd(); ++it) {
        QString key = m_service->currentAppIdFor(it.key());
        if (key.isEmpty()) {
            continue;
        }
        preFloatZonesObj[key] = toJsonArray(it.value());
    }
    tracking->writeString(ConfigKeys::preFloatZoneAssignmentsKey(),
                          QString::fromUtf8(QJsonDocument(preFloatZonesObj).toJson(QJsonDocument::Compact)));

    // Save pre-float screen assignments (for unfloating to correct monitor).
    // Same current-class conversion as above, plus translate to screen IDs.
    QJsonObject preFloatScreensObj;
    for (auto it = m_service->preFloatScreenAssignments().constBegin();
         it != m_service->preFloatScreenAssignments().constEnd(); ++it) {
        QString key = m_service->currentAppIdFor(it.key());
        if (key.isEmpty()) {
            continue;
        }
        preFloatScreensObj[key] = Utils::screenIdForName(it.value());
    }
    tracking->writeString(ConfigKeys::preFloatScreenAssignmentsKey(),
                          QString::fromUtf8(QJsonDocument(preFloatScreensObj).toJson(QJsonDocument::Compact)));

    // Save user-snapped classes
    QJsonArray userSnappedArray;
    for (const QString& windowClass : m_service->userSnappedClasses()) {
        userSnappedArray.append(windowClass);
    }
    tracking->writeString(ConfigKeys::userSnappedClassesKey(),
                          QString::fromUtf8(QJsonDocument(userSnappedArray).toJson(QJsonDocument::Compact)));

    // Save autotile per-context window orders (analogous to WindowZoneAssignmentsFull
    // for snap mode). masterCount/splitRatio are NOT saved here — Settings owns those
    // via AutotileScreen:<id> per-screen overrides.
    if (m_serializeTilingStatesFn) {
        const QJsonArray autotileOrders = m_serializeTilingStatesFn();
        if (!autotileOrders.isEmpty()) {
            tracking->writeString(ConfigKeys::autotileWindowOrdersKey(),
                                  QString::fromUtf8(QJsonDocument(autotileOrders).toJson(QJsonDocument::Compact)));
        } else {
            tracking->deleteKey(ConfigKeys::autotileWindowOrdersKey());
        }
    } else {
        // No serialize delegate — clean up any stale key from a prior session
        // where autotile was enabled. Prevents restoring orphaned window orders.
        tracking->deleteKey(ConfigKeys::autotileWindowOrdersKey());
    }

    // Save autotile pending restore queues (close/reopen window preservation).
    // Separate key from window orders to keep the orders array homogeneous.
    if (m_serializePendingRestoresFn) {
        const QJsonObject pendingRestores = m_serializePendingRestoresFn();
        if (!pendingRestores.isEmpty()) {
            tracking->writeString(ConfigKeys::autotilePendingRestoresKey(),
                                  QString::fromUtf8(QJsonDocument(pendingRestores).toJson(QJsonDocument::Compact)));
        } else {
            tracking->deleteKey(ConfigKeys::autotilePendingRestoresKey());
        }
    } else {
        tracking->deleteKey(ConfigKeys::autotilePendingRestoresKey());
    }

    tracking.reset(); // release group before sync
    m_sessionBackend->sync();
    qCInfo(lcDbusWindow) << "Saved state:"
                         << "zones=" << fullAssignments.size() << "pending=" << pendingQueuesObj.size()
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

    // Block all subsequent saves. During Qt object destruction after stop(),
    // LayoutManager destruction can trigger activeLayoutChanged → onLayoutChanged()
    // which purges zone assignments and calls scheduleSaveState(). Without this
    // guard, the debounced save fires with empty state and overwrites the correct
    // shutdown save above — causing window snap state to be lost across restarts.
    m_shutdownSaveGuard = true;
}

void WindowTrackingAdaptor::loadState()
{
    // Read config via the IConfigBackend group API (readString/readInt).
    // This correctly handles values stored as native JSON objects/arrays
    // (e.g. PendingRestoreQueues, PreTileGeometries) — the group's readString()
    // serializes them back to compact JSON strings.
    //
    // The previous approach used readJsonConfigFromDisk() which flattened the
    // entire config into a QMap. Its flattener recursed into native JSON objects
    // as if they were config groups, making object-type values like
    // PendingRestoreQueues invisible to key lookups — the root cause of the
    // window restore bug after v2 config migration.
    //
    // Sync the shared backend first so any in-memory writes (e.g., from a
    // concurrent saveState()) are flushed to disk before we read.
    m_sessionBackend->sync();
    auto tracking = m_sessionBackend->group(ConfigKeys::windowTrackingGroup());
    auto readVal = [&](const QString& key, const QString& def = QString()) -> QString {
        return tracking->readString(key, def);
    };
    auto readIntVal = [&](const QString& key, int def = 0) -> int {
        return tracking->readInt(key, def);
    };

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

    // Helper: resolve stored screen value. Returns as-is — mixed formats
    // (connector names and screen IDs) are handled at comparison points via
    // Utils::screensMatch() which resolves both to QScreen* pointers.
    auto resolveScreen = [](const QString& storedScreen) -> QString {
        return storedScreen;
    };

    // Pending entries derived from active assignments (fallback for KWin restarts where
    // UUIDs change). Collected separately from persisted pending queues so we can do
    // count-based merging — prevents exponential growth while preserving multi-instance entries.
    QHash<QString, QList<PendingRestore>> activePending;

    // Try full-windowId format first (preserves multi-instance distinction)
    QHash<QString, QStringList> fullZones;
    QHash<QString, QString> fullScreens;
    QHash<QString, int> fullDesktops;

    QString fullJson = readVal(ConfigKeys::windowZoneAssignmentsFullKey(), QString());
    if (!fullJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(fullJson.toUtf8());
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

                // Also build appId-keyed pending entries as fallback for KWin restarts
                // (UUIDs change, so exact matches won't work). For daemon-only restarts,
                // calculateRestoreFromSession() guards against wrong-instance consumption.
                // Collected separately — merged after loading persisted queues to prevent duplication.
                // Registry is typically empty during load, so currentAppIdFor
                // falls back to string parsing — same behavior as the old code
                // but consistent with the rest of the codebase.
                QString appId = m_service->currentAppIdFor(windowId);
                PendingRestore pending;
                pending.zoneIds = zoneIds;
                pending.screenId = screen;
                pending.virtualDesktop = desktop;
                activePending[appId].append(pending);
            }
        }
    }

    // Load exact assignments so isWindowSnapped() returns true for daemon-only restarts.
    // calculateRestoreFromSession() checks for exact-match siblings before allowing
    // FIFO consumption, preventing wrong-instance restore for multi-instance apps.
    m_service->setActiveAssignments(fullZones, fullScreens, fullDesktops);

    // Load persisted pending restore queues (appId -> array of entry objects).
    // These go directly into pendingQueues. Persisted entries are richer than
    // active-derived ones (they include layoutId and zoneNumbers).
    QString pendingQueuesJson = readVal(ConfigKeys::pendingRestoreQueuesKey(), QString());
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
                    entry.screenId = resolveScreen(entryObj[QLatin1String("screen")].toString());
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

    // Merge active-derived entries: only add entries not already covered by persisted
    // queues. saveState() persists ALL pending entries including active-derived ones
    // from the previous load. Without count-based merging, entries would double on
    // every daemon restart (1→2→4→...).
    //
    // For each (appId, zoneIds, screen, desktop) fingerprint, a persisted entry
    // "covers" one active-derived entry. Uncovered active-derived entries (new windows
    // snapped since last save) are appended. This correctly handles multi-instance
    // apps in the same zone — two konsole windows in zone Z produce two entries.
    for (auto activeIt = activePending.constBegin(); activeIt != activePending.constEnd(); ++activeIt) {
        const QString& appId = activeIt.key();
        // Build a consumable budget of persisted fingerprints for this appId
        struct Fingerprint
        {
            QStringList zoneIds;
            QString screenId;
            int virtualDesktop;
        };
        QList<Fingerprint> persistedBudget;
        for (const auto& e : pendingQueues.value(appId)) {
            persistedBudget.append({e.zoneIds, e.screenId, e.virtualDesktop});
        }
        for (const auto& activeEntry : activeIt.value()) {
            bool covered = false;
            for (int i = 0; i < persistedBudget.size(); ++i) {
                if (persistedBudget[i].zoneIds == activeEntry.zoneIds
                    && persistedBudget[i].screenId == activeEntry.screenId
                    && persistedBudget[i].virtualDesktop == activeEntry.virtualDesktop) {
                    persistedBudget.removeAt(i); // consume one match
                    covered = true;
                    break;
                }
            }
            if (!covered) {
                // New window snapped since last save — persisted queues don't have it
                pendingQueues[appId].append(activeEntry);
            }
        }
    }

    m_service->setPendingRestoreQueues(pendingQueues);

    // Load pre-tile geometries (with migration from old split keys)
    using PreTileGeometry = WindowTrackingService::PreTileGeometry;
    QHash<QString, PreTileGeometry> preTileGeometries;
    auto loadGeometries = [&resolveScreen](const QString& json, QHash<QString, PreTileGeometry>& out) {
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
                    QString screen = resolveScreen(geomObj[QLatin1String("screen")].toString());
                    out[it.key()] = PreTileGeometry{geom, screen};
                }
            }
        }
    };

    // Load full windowId format first (preserves multi-instance distinction for
    // daemon-only restarts where KWin UUIDs are still valid). Each entry stores
    // both the full windowId key AND the appId key, mirroring storePreTileGeometry().
    QString fullTileJson = readVal(ConfigKeys::preTileGeometriesFullKey(), QString());
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
                    QString screen = resolveScreen(entry[QLatin1String("screen")].toString());
                    PreTileGeometry ptg{geom, screen};
                    preTileGeometries[windowId] = ptg;
                    // Also store under appId for fallback (mirrors storePreTileGeometry).
                    // Registry is empty during load so this resolves to string parsing.
                    QString appId = m_service->currentAppIdFor(windowId);
                    if (!appId.isEmpty() && appId != windowId) {
                        preTileGeometries[appId] = ptg;
                    }
                }
            }
        }
    }

    // Load appId-keyed format (fallback for KWin restarts or entries without full format)
    QString tileJson = readVal(ConfigKeys::preTileGeometriesKey(), QString());
    if (!tileJson.isEmpty()) {
        // Only fill in keys not already loaded from full format
        QHash<QString, PreTileGeometry> appIdGeometries;
        loadGeometries(tileJson, appIdGeometries);
        for (auto it = appIdGeometries.constBegin(); it != appIdGeometries.constEnd(); ++it) {
            if (!preTileGeometries.contains(it.key())) {
                preTileGeometries[it.key()] = it.value();
            }
        }
    }
    m_service->setPreTileGeometries(preTileGeometries);

    // Load last used zone info
    QString lastZoneId = readVal(ConfigKeys::lastUsedZoneIdKey(), QString());
    QString lastScreenId = readVal(ConfigKeys::lastUsedScreenNameKey(), QString());
    QString lastZoneClass = readVal(ConfigKeys::lastUsedZoneClassKey(), QString());
    int lastDesktop = readIntVal(ConfigKeys::lastUsedDesktopKey(), 0);
    m_service->setLastUsedZone(lastZoneId, lastScreenId, lastZoneClass, lastDesktop);

    // Float state is ephemeral (session-only) — skip loading.
    // Any stale FloatingWindows entries from older versions are cleaned up in saveState().

    // Load pre-float zone assignments (for unfloating after session restore)
    // Supports both old format (string) and new format (JSON array) for backward compat
    QHash<QString, QStringList> preFloatZones =
        parseZoneListMap(readVal(ConfigKeys::preFloatZoneAssignmentsKey(), QString()));
    m_service->setPreFloatZoneAssignments(preFloatZones);

    // Load pre-float screen assignments (for unfloating to correct monitor)
    // Values may be screen IDs (new) or connector names (legacy) — resolve to current connector name
    QHash<QString, QString> preFloatScreens;
    QString preFloatScreensJson = readVal(ConfigKeys::preFloatScreenAssignmentsKey(), QString());
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
    QString userSnappedJson = readVal(ConfigKeys::userSnappedClassesKey(), QString());
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

    // Restore autotile per-context window orders (analogous to WindowZoneAssignmentsFull)
    if (m_deserializeTilingStatesFn) {
        const QString autotileOrdersStr = readVal(ConfigKeys::autotileWindowOrdersKey(), QString());
        if (!autotileOrdersStr.isEmpty()) {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(autotileOrdersStr.toUtf8(), &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isArray()) {
                m_deserializeTilingStatesFn(doc.array());
            } else {
                qCWarning(lcDbusWindow) << "Failed to parse saved autotile window orders:" << parseError.errorString();
            }
        }
    }

    // Restore autotile pending restore queues (close/reopen window preservation)
    if (m_deserializePendingRestoresFn) {
        const QString pendingRestoresStr = readVal(ConfigKeys::autotilePendingRestoresKey(), QString());
        if (!pendingRestoresStr.isEmpty()) {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(pendingRestoresStr.toUtf8(), &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                m_deserializePendingRestoresFn(doc.object());
            } else {
                qCWarning(lcDbusWindow) << "Failed to parse saved autotile pending restores:"
                                        << parseError.errorString();
            }
        }
    }

    // Restore active layout from previous session so that previousLayout() is correct
    // on the next layout switch. Without this, the daemon starts with defaultLayout()
    // which may differ from the layout the user was on, causing resnap to build its
    // zone-position map from the wrong layout's zones (all entries get pos=0 → empty buffer).
    //
    // CRITICAL: Use QSignalBlocker to suppress activeLayoutChanged during restore.
    // At this point in init, m_layoutManager->currentVirtualDesktop() is still the
    // default (1) — the real desktop hasn't been set yet (done in Daemon::start()).
    // If activeLayoutChanged fires, onLayoutChanged() runs stale-assignment removal
    // using resolveLayoutForScreen() with the wrong desktop, which falls back to
    // defaultLayout() instead of the restored layout. Zone assignments from the
    // saved layout get purged because those zones don't exist in defaultLayout().
    // This is a state restoration, not a real layout switch — no signal needed.
    QString savedActiveLayoutId = readVal(ConfigKeys::activeLayoutIdKey(), QString());
    if (!savedActiveLayoutId.isEmpty() && m_layoutManager) {
        auto savedUuid = Utils::parseUuid(savedActiveLayoutId);
        if (savedUuid) {
            Layout* savedLayout = m_layoutManager->layoutById(*savedUuid);
            if (savedLayout && savedLayout != m_layoutManager->activeLayout()) {
                qCInfo(lcDbusWindow) << "Restoring active layout from previous session:" << savedLayout->name();
                QSignalBlocker blocker(m_layoutManager);
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
    // After saveStateOnShutdown(), block all saves. During Qt object destruction,
    // LayoutManager teardown can trigger onLayoutChanged() → scheduleSaveState()
    // with purged (empty) state, overwriting the correct shutdown save.
    if (m_shutdownSaveGuard) {
        return;
    }
    if (m_saveTimer) {
        m_saveTimer->start();
    } else {
        saveState();
    }
}

} // namespace PlasmaZones
