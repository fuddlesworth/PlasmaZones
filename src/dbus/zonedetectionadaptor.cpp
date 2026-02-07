// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zonedetectionadaptor.h"
#include "dbushelpers.h"
#include "../core/interfaces.h"
#include "../core/layout.h"
#include "../core/zone.h"
#include "../core/geometryutils.h"
#include "../core/screenmanager.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include <QGuiApplication>
#include <QScreen>
#include <limits>

namespace PlasmaZones {

ZoneDetectionAdaptor::ZoneDetectionAdaptor(IZoneDetector* detector, ILayoutManager* layoutManager, ISettings* settings,
                                           QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_zoneDetector(detector)
    , m_layoutManager(layoutManager)
    , m_settings(settings)
{
    Q_ASSERT(detector);
    Q_ASSERT(layoutManager);
    Q_ASSERT(settings);
}

QString ZoneDetectionAdaptor::detectZoneAtPosition(int x, int y)
{
    auto* layout = DbusHelpers::getActiveLayoutOrWarn(m_layoutManager, QStringLiteral("detect zone"));
    if (!layout) {
        return QString();
    }

    // Get primary screen geometry
    QScreen* screen = DbusHelpers::getPrimaryScreenOrWarn(QStringLiteral("detectZoneAtPosition"));
    if (!screen) {
        return QString();
    }

    // Use actualAvailableGeometry() which excludes panels/taskbars (queries PlasmaShell on Wayland)
    // This matches how zones are rendered and snapped
    QRectF availableGeom = ScreenManager::actualAvailableGeometry(screen);

    // Guard against zero-size geometry (disconnected or degenerate screen)
    if (availableGeom.width() <= 0 || availableGeom.height() <= 0) {
        return QString();
    }

    // Find which zone contains this point by checking relative coordinates
    qreal relX = static_cast<qreal>(x - availableGeom.x()) / availableGeom.width();
    qreal relY = static_cast<qreal>(y - availableGeom.y()) / availableGeom.height();

    Zone* foundZone = nullptr;
    for (auto* zone : layout->zones()) {
        QRectF relGeom = zone->relativeGeometry();
        if (relGeom.contains(QPointF(relX, relY))) {
            foundZone = zone;
            break;
        }
    }

    if (foundZone) {
        m_zoneDetector->setLayout(layout);
        m_zoneDetector->highlightZone(foundZone);
        // Trigger overlay update via signal (decoupled)
        Q_EMIT zoneDetected(foundZone->id().toString(), getZoneGeometry(foundZone->id().toString()));
        return foundZone->id().toString();
    }

    m_zoneDetector->clearHighlights();
    return QString();
}

QString ZoneDetectionAdaptor::getZoneGeometry(const QString& zoneId)
{
    // Use empty screen name to fall back to primary screen
    return getZoneGeometryForScreen(zoneId, QString());
}

QString ZoneDetectionAdaptor::getZoneGeometryForScreen(const QString& zoneId, const QString& screenName)
{
    // Find the zone - it may be in any layout (not just activeLayout)
    // when per-screen layout assignments are used
    Zone* zone = DbusHelpers::findZoneInAnyLayout(m_layoutManager, zoneId, QStringLiteral("get zone geometry"));
    if (!zone) {
        return QString();
    }

    // Find target screen - use specified screen name or fall back to primary
    QScreen* screen = DbusHelpers::getScreenOrWarn(screenName, QStringLiteral("getZoneGeometryForScreen"));
    if (!screen) {
        return QString();
    }

    // Use geometry with gaps (matches snap behavior)
    // Use per-layout zonePadding/outerGap if set, otherwise fall back to global settings
    Layout* zoneLayout = qobject_cast<Layout*>(zone->parent());
    int zonePadding = GeometryUtils::getEffectiveZonePadding(zoneLayout, m_settings);
    int outerGap = GeometryUtils::getEffectiveOuterGap(zoneLayout, m_settings);
    QRectF geom = GeometryUtils::getZoneGeometryWithGaps(zone, screen, zonePadding, outerGap, true);

    // Return as "x,y,width,height"
    return QStringLiteral("%1,%2,%3,%4")
        .arg(static_cast<int>(geom.x()))
        .arg(static_cast<int>(geom.y()))
        .arg(static_cast<int>(geom.width()))
        .arg(static_cast<int>(geom.height()));
}

QStringList ZoneDetectionAdaptor::getZonesForScreen(const QString& screenName)
{
    QStringList zoneIds;

    auto* layout = m_layoutManager->activeLayout();
    if (!layout) {
        return zoneIds;
    }

    // Find screen
    QScreen* screen = Utils::findScreenByName(screenName);
    if (!screen) {
        return zoneIds;
    }

    // Get all zones for this screen
    for (auto* zone : layout->zones()) {
        zoneIds.append(zone->id().toString());
    }

    return zoneIds;
}

QStringList ZoneDetectionAdaptor::detectMultiZoneAtPosition(int x, int y)
{
    QStringList zoneIds;

    auto* layout = m_layoutManager->activeLayout();
    if (!layout) {
        qCWarning(lcDbus) << "Cannot detect multi-zone - no active layout";
        return zoneIds;
    }

    m_zoneDetector->setLayout(layout);

    // Convert cursor position to QPointF for detection
    QPointF cursorPos(static_cast<qreal>(x), static_cast<qreal>(y));

    // Call detectMultiZone
    ZoneDetectionResult result = m_zoneDetector->detectMultiZone(cursorPos);

    if (result.isMultiZone && result.primaryZone) {
        // Multi-zone detected - collect all zone IDs
        zoneIds.append(result.primaryZone->id().toString());
        for (Zone* zone : result.adjacentZones) {
            if (zone && zone != result.primaryZone) {
                zoneIds.append(zone->id().toString());
            }
        }
    } else if (result.primaryZone) {
        // Single zone detected (fallback from multi-zone detection)
        zoneIds.append(result.primaryZone->id().toString());
    }
    // If no zone detected, return empty list

    return zoneIds;
}

QString ZoneDetectionAdaptor::getAdjacentZone(const QString& currentZoneId, const QString& direction)
{
    if (!DbusHelpers::validateNonEmpty(direction, QStringLiteral("direction"), QStringLiteral("get adjacent zone"))) {
        return QString();
    }

    // Find the zone - it may be in any layout (not just activeLayout)
    // when per-screen layout assignments are used
    Zone* currentZone = DbusHelpers::findZoneInAnyLayout(m_layoutManager, currentZoneId, QStringLiteral("get adjacent zone"));
    if (!currentZone) {
        return QString();
    }

    // Get the layout containing this zone
    Layout* layout = qobject_cast<Layout*>(currentZone->parent());
    if (!layout) {
        qCWarning(lcDbus) << "Zone has no parent layout:" << currentZoneId;
        return QString();
    }

    QRectF currentGeom = currentZone->relativeGeometry();
    QPointF currentCenter(currentGeom.center());

    Zone* bestZone = nullptr;
    qreal bestDistance = std::numeric_limits<qreal>::max();

    for (auto* zone : layout->zones()) {
        if (zone == currentZone) {
            continue;
        }

        QRectF zoneGeom = zone->relativeGeometry();
        QPointF zoneCenter(zoneGeom.center());

        // Check if zone is in the correct direction
        bool isValidDirection = false;
        qreal distance = 0;

        if (direction == Utils::Direction::Left) {
            // Zone must be to the left (its right edge <= current left edge, or center is left)
            if (zoneCenter.x() < currentCenter.x()) {
                isValidDirection = true;
                distance = currentCenter.x() - zoneCenter.x();
                // Prefer zones with similar vertical position
                distance += std::abs(zoneCenter.y() - currentCenter.y()) * 2;
            }
        } else if (direction == Utils::Direction::Right) {
            if (zoneCenter.x() > currentCenter.x()) {
                isValidDirection = true;
                distance = zoneCenter.x() - currentCenter.x();
                distance += std::abs(zoneCenter.y() - currentCenter.y()) * 2;
            }
        } else if (direction == Utils::Direction::Up) {
            if (zoneCenter.y() < currentCenter.y()) {
                isValidDirection = true;
                distance = currentCenter.y() - zoneCenter.y();
                distance += std::abs(zoneCenter.x() - currentCenter.x()) * 2;
            }
        } else if (direction == Utils::Direction::Down) {
            if (zoneCenter.y() > currentCenter.y()) {
                isValidDirection = true;
                distance = zoneCenter.y() - currentCenter.y();
                distance += std::abs(zoneCenter.x() - currentCenter.x()) * 2;
            }
        }

        if (isValidDirection && distance < bestDistance) {
            bestDistance = distance;
            bestZone = zone;
        }
    }

    return bestZone ? bestZone->id().toString() : QString();
}

QString ZoneDetectionAdaptor::getFirstZoneInDirection(const QString& direction, const QString& screenName)
{
    if (!DbusHelpers::validateNonEmpty(direction, QStringLiteral("direction"), QStringLiteral("get first zone"))) {
        return QString();
    }

    // Use per-screen layout (falls back to activeLayout via resolveLayoutForScreen)
    Layout* layout = m_layoutManager->resolveLayoutForScreen(screenName);
    if (!layout || layout->zones().isEmpty()) {
        return QString();
    }

    Zone* bestZone = nullptr;
    qreal bestValue = 0;
    bool initialized = false;

    for (auto* zone : layout->zones()) {
        QRectF geom = zone->relativeGeometry();
        qreal value = 0;

        if (direction == Utils::Direction::Left) {
            // Find leftmost zone (smallest x)
            value = geom.x();
            if (!initialized || value < bestValue) {
                bestValue = value;
                bestZone = zone;
                initialized = true;
            }
        } else if (direction == Utils::Direction::Right) {
            // Find rightmost zone (largest x + width, i.e., right edge)
            value = geom.x() + geom.width();
            if (!initialized || value > bestValue) {
                bestValue = value;
                bestZone = zone;
                initialized = true;
            }
        } else if (direction == Utils::Direction::Up) {
            // Find topmost zone (smallest y)
            value = geom.y();
            if (!initialized || value < bestValue) {
                bestValue = value;
                bestZone = zone;
                initialized = true;
            }
        } else if (direction == Utils::Direction::Down) {
            // Find bottommost zone (largest y + height, i.e., bottom edge)
            value = geom.y() + geom.height();
            if (!initialized || value > bestValue) {
                bestValue = value;
                bestZone = zone;
                initialized = true;
            }
        } else {
            qCWarning(lcDbus) << "Invalid direction:" << direction;
            return QString();
        }
    }

    if (bestZone) {
        qCDebug(lcDbus) << "First zone in direction" << direction << "is" << bestZone->id().toString();
        return bestZone->id().toString();
    }

    return QString();
}

QString ZoneDetectionAdaptor::getZoneByNumber(int zoneNumber)
{
    auto* layout = m_layoutManager->activeLayout();
    if (!layout) {
        return QString();
    }

    auto* zone = layout->zoneByNumber(zoneNumber);
    if (!zone) {
        return QString();
    }

    return zone->id().toString();
}

QStringList ZoneDetectionAdaptor::getAllZoneGeometries()
{
    QStringList result;

    auto* layout = DbusHelpers::getActiveLayoutOrWarn(m_layoutManager, QStringLiteral("get all zone geometries"));
    if (!layout) {
        return result;
    }

    QScreen* screen = DbusHelpers::getPrimaryScreenOrWarn(QStringLiteral("getAllZoneGeometries"));
    if (!screen) {
        return result;
    }

    // Use per-layout zonePadding/outerGap if set, otherwise fall back to global settings
    int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings);
    int outerGap = GeometryUtils::getEffectiveOuterGap(layout, m_settings);
    for (auto* zone : layout->zones()) {
        // Use geometry with gaps (matches snap behavior)
        QRectF geom = GeometryUtils::getZoneGeometryWithGaps(zone, screen, zonePadding, outerGap, true);
        // Format: "zoneId:x,y,width,height"
        QString entry = QStringLiteral("%1:%2,%3,%4,%5")
                            .arg(zone->id().toString())
                            .arg(static_cast<int>(geom.x()))
                            .arg(static_cast<int>(geom.y()))
                            .arg(static_cast<int>(geom.width()))
                            .arg(static_cast<int>(geom.height()));
        result.append(entry);
    }

    return result;
}

int ZoneDetectionAdaptor::getKeyboardModifiers()
{
    // queryKeyboardModifiers() queries the actual keyboard state from the platform
    // This is more reliable than keyboardModifiers() which only returns cached values
    Qt::KeyboardModifiers mods = QGuiApplication::queryKeyboardModifiers();
    return static_cast<int>(mods);
}

QString ZoneDetectionAdaptor::detectZoneWithModifiers(int x, int y)
{
    // Get modifiers first (before any potential delays from zone detection)
    int modifiers = getKeyboardModifiers();

    // Detect zone
    QString zoneId = detectZoneAtPosition(x, y);

    // Return combined result: "zoneId;modifiers"
    // If no zone found, zoneId will be empty but we still return modifiers
    return QStringLiteral("%1;%2").arg(zoneId).arg(modifiers);
}

} // namespace PlasmaZones
