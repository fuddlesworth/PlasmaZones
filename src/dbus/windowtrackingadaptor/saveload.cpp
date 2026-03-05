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

    // Save zone assignments as JSON arrays (from service state)
    QJsonObject assignmentsObj;
    for (auto it = m_service->zoneAssignments().constBegin(); it != m_service->zoneAssignments().constEnd(); ++it) {
        QString stableId = Utils::extractStableId(it.key());
        assignmentsObj[stableId] = toJsonArray(it.value());
    }
    // Include pending assignments
    for (auto it = m_service->pendingZoneAssignments().constBegin();
         it != m_service->pendingZoneAssignments().constEnd(); ++it) {
        if (!assignmentsObj.contains(it.key())) {
            assignmentsObj[it.key()] = toJsonArray(it.value());
        }
    }
    tracking.writeEntry(QStringLiteral("WindowZoneAssignments"),
                        QString::fromUtf8(QJsonDocument(assignmentsObj).toJson(QJsonDocument::Compact)));

    // Save screen assignments (translate connector names to stable screen IDs for persistence)
    QJsonObject screenAssignmentsObj;
    for (auto it = m_service->screenAssignments().constBegin(); it != m_service->screenAssignments().constEnd(); ++it) {
        QString stableId = Utils::extractStableId(it.key());
        screenAssignmentsObj[stableId] = Utils::screenIdForName(it.value());
    }
    tracking.writeEntry(QStringLiteral("WindowScreenAssignments"),
                        QString::fromUtf8(QJsonDocument(screenAssignmentsObj).toJson(QJsonDocument::Compact)));

    // Save pending screen assignments (translate to stable screen IDs)
    QJsonObject pendingScreenAssignmentsObj;
    for (auto it = m_service->pendingScreenAssignments().constBegin();
         it != m_service->pendingScreenAssignments().constEnd(); ++it) {
        if (!it.value().isEmpty()) {
            pendingScreenAssignmentsObj[it.key()] = Utils::screenIdForName(it.value());
        }
    }
    tracking.writeEntry(QStringLiteral("PendingWindowScreenAssignments"),
                        QString::fromUtf8(QJsonDocument(pendingScreenAssignmentsObj).toJson(QJsonDocument::Compact)));

    // Save active desktop assignments (for cross-restart persistence, same pattern as screens)
    QJsonObject desktopAssignmentsObj;
    for (auto it = m_service->desktopAssignments().constBegin(); it != m_service->desktopAssignments().constEnd();
         ++it) {
        if (it.value() > 0) {
            QString stableId = Utils::extractStableId(it.key());
            desktopAssignmentsObj[stableId] = it.value();
        }
    }
    tracking.writeEntry(QStringLiteral("WindowDesktopAssignments"),
                        QString::fromUtf8(QJsonDocument(desktopAssignmentsObj).toJson(QJsonDocument::Compact)));

    // Save pending desktop assignments
    QJsonObject pendingDesktopAssignmentsObj;
    for (auto it = m_service->pendingDesktopAssignments().constBegin();
         it != m_service->pendingDesktopAssignments().constEnd(); ++it) {
        if (it.value() > 0) {
            pendingDesktopAssignmentsObj[it.key()] = it.value();
        }
    }
    tracking.writeEntry(QStringLiteral("PendingWindowDesktopAssignments"),
                        QString::fromUtf8(QJsonDocument(pendingDesktopAssignmentsObj).toJson(QJsonDocument::Compact)));

    // Save pending layout assignments (for layout validation on restore)
    QJsonObject pendingLayoutAssignmentsObj;
    for (auto it = m_service->pendingLayoutAssignments().constBegin();
         it != m_service->pendingLayoutAssignments().constEnd(); ++it) {
        if (!it.value().isEmpty()) {
            pendingLayoutAssignmentsObj[it.key()] = it.value();
        }
    }
    tracking.writeEntry(QStringLiteral("PendingWindowLayoutAssignments"),
                        QString::fromUtf8(QJsonDocument(pendingLayoutAssignmentsObj).toJson(QJsonDocument::Compact)));

    // Save pending zone numbers (for zone-number fallback when UUIDs change)
    QJsonObject pendingZoneNumbersObj;
    for (auto it = m_service->pendingZoneNumbers().constBegin(); it != m_service->pendingZoneNumbers().constEnd();
         ++it) {
        QJsonArray numArray;
        for (int num : it.value()) {
            numArray.append(num);
        }
        pendingZoneNumbersObj[it.key()] = numArray;
    }
    tracking.writeEntry(QStringLiteral("PendingWindowZoneNumbers"),
                        QString::fromUtf8(QJsonDocument(pendingZoneNumbersObj).toJson(QJsonDocument::Compact)));

    // Save pre-snap geometries (convert to stableId for cross-restart persistence)
    tracking.writeEntry(QStringLiteral("PreSnapGeometries"), serializeGeometryMap(m_service->preSnapGeometries()));

    // Save pre-autotile geometries (convert to stableId for cross-restart persistence)
    tracking.writeEntry(QStringLiteral("PreAutotileGeometries"),
                        serializeGeometryMap(m_service->preAutotileGeometries()));

    // Save last used zone info (from service)
    tracking.writeEntry(QStringLiteral("LastUsedZoneId"), m_service->lastUsedZoneId());
    // Note: Other last-used fields would need accessors in service

    // Save floating windows (convert to stableId for cross-restart persistence, deduplicate)
    QJsonArray floatingArray;
    QSet<QString> savedFloatingIds;
    for (const QString& windowId : m_service->floatingWindows()) {
        QString stableId = Utils::extractStableId(windowId);
        if (!stableId.isEmpty() && !savedFloatingIds.contains(stableId)) {
            floatingArray.append(stableId);
            savedFloatingIds.insert(stableId);
        }
    }
    tracking.writeEntry(QStringLiteral("FloatingWindows"),
                        QString::fromUtf8(QJsonDocument(floatingArray).toJson(QJsonDocument::Compact)));

    // Save pre-float zone assignments (for unfloating after session restore).
    // Runtime keys may be full window IDs (with pointer address); convert to
    // stable IDs for cross-restart compatibility.
    QJsonObject preFloatZonesObj;
    for (auto it = m_service->preFloatZoneAssignments().constBegin();
         it != m_service->preFloatZoneAssignments().constEnd(); ++it) {
        QString key = Utils::extractStableId(it.key());
        preFloatZonesObj[key] = toJsonArray(it.value());
    }
    tracking.writeEntry(QStringLiteral("PreFloatZoneAssignments"),
                        QString::fromUtf8(QJsonDocument(preFloatZonesObj).toJson(QJsonDocument::Compact)));

    // Save pre-float screen assignments (for unfloating to correct monitor).
    // Same stable ID conversion as above, plus translate to screen IDs.
    QJsonObject preFloatScreensObj;
    for (auto it = m_service->preFloatScreenAssignments().constBegin();
         it != m_service->preFloatScreenAssignments().constEnd(); ++it) {
        QString key = Utils::extractStableId(it.key());
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

    // Load zone assignments into pending (keyed by stable ID)
    // Supports both old format (string) and new format (JSON array) for backward compat
    QHash<QString, QStringList> pendingZones =
        parseZoneListMap(tracking.readEntry(QStringLiteral("WindowZoneAssignments"), QString()));
    m_service->setPendingZoneAssignments(pendingZones);

    // Load screen assignments: merge active (WindowScreenAssignments) into pending
    // (PendingWindowScreenAssignments) so that windows that were still open when the
    // daemon last saved state retain their screen assignment after a daemon restart.
    // Without this merge, the screen falls back to wherever KWin initially places the
    // window (typically the primary display), causing the "wrong display" restore bug.
    // This mirrors the zone-assignment merge pattern above.
    QHash<QString, QString> pendingScreens;

    // First: load active screen assignments as a base layer
    // Values may be screen IDs (new) or connector names (legacy) — resolve to current connector name
    QString activeScreensJson = tracking.readEntry(QStringLiteral("WindowScreenAssignments"), QString());
    if (!activeScreensJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(activeScreensJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isString() && !it.value().toString().isEmpty()) {
                    QString storedScreen = it.value().toString();
                    // Translate screen ID to connector name; legacy connector names pass through
                    if (!Utils::isConnectorName(storedScreen)) {
                        QString connectorName = Utils::screenNameForId(storedScreen);
                        if (!connectorName.isEmpty()) {
                            storedScreen = connectorName;
                        }
                    }
                    pendingScreens[it.key()] = storedScreen;
                }
            }
        }
    }

    // Second: overlay with pending screen assignments (pending takes priority —
    // these were explicitly saved when the window closed, so they're more recent)
    QString pendingScreensJson = tracking.readEntry(QStringLiteral("PendingWindowScreenAssignments"), QString());
    if (!pendingScreensJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(pendingScreensJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isString() && !it.value().toString().isEmpty()) {
                    QString storedScreen = it.value().toString();
                    if (!Utils::isConnectorName(storedScreen)) {
                        QString connectorName = Utils::screenNameForId(storedScreen);
                        if (!connectorName.isEmpty()) {
                            storedScreen = connectorName;
                        }
                    }
                    pendingScreens[it.key()] = storedScreen;
                }
            }
        }
    }
    m_service->setPendingScreenAssignments(pendingScreens);

    // Load desktop assignments: merge active (WindowDesktopAssignments) into pending
    // (PendingWindowDesktopAssignments) so that windows still open at daemon shutdown
    // retain their virtual desktop context. Same merge pattern as screens above.
    QHash<QString, int> pendingDesktops;

    // First: load active desktop assignments as a base layer
    QString activeDesktopsJson = tracking.readEntry(QStringLiteral("WindowDesktopAssignments"), QString());
    if (!activeDesktopsJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(activeDesktopsJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isDouble() && it.value().toInt() > 0) {
                    pendingDesktops[it.key()] = it.value().toInt();
                }
            }
        }
    }

    // Second: overlay with pending desktop assignments (pending takes priority)
    QString pendingDesktopsJson = tracking.readEntry(QStringLiteral("PendingWindowDesktopAssignments"), QString());
    if (!pendingDesktopsJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(pendingDesktopsJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isDouble() && it.value().toInt() > 0) {
                    pendingDesktops[it.key()] = it.value().toInt();
                }
            }
        }
    }
    m_service->setPendingDesktopAssignments(pendingDesktops);

    // Load pending layout assignments (for layout validation on restore)
    QHash<QString, QString> pendingLayouts;
    QString pendingLayoutsJson = tracking.readEntry(QStringLiteral("PendingWindowLayoutAssignments"), QString());
    if (!pendingLayoutsJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(pendingLayoutsJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isString()) {
                    pendingLayouts[it.key()] = it.value().toString();
                }
            }
        }
    }
    m_service->setPendingLayoutAssignments(pendingLayouts);

    // Load pending zone numbers (for zone-number fallback when UUIDs change)
    QHash<QString, QList<int>> pendingZoneNumbers;
    QString pendingZoneNumbersJson = tracking.readEntry(QStringLiteral("PendingWindowZoneNumbers"), QString());
    if (!pendingZoneNumbersJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(pendingZoneNumbersJson.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
                if (it.value().isArray()) {
                    QList<int> numbers;
                    for (const QJsonValue& v : it.value().toArray()) {
                        if (v.isDouble()) {
                            numbers.append(v.toInt());
                        }
                    }
                    if (!numbers.isEmpty()) {
                        pendingZoneNumbers[it.key()] = numbers;
                    }
                }
            }
        }
    }
    m_service->setPendingZoneNumbers(pendingZoneNumbers);

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

    qCInfo(lcDbusWindow) << "Loaded state from KConfig pendingAssignments= " << pendingZones.size();
    for (auto it = pendingZones.constBegin(); it != pendingZones.constEnd(); ++it) {
        qCInfo(lcDbusWindow) << "  Pending snap window= " << it.key() << " zone= " << it.value();
    }
    if (!pendingZones.isEmpty()) {
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
