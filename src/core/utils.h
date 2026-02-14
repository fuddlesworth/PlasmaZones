// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QGuiApplication>
#include <QScreen>
#include <QUuid>
#include <QString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDir>
#include <QFile>
#include <optional>

namespace PlasmaZones {
namespace Utils {

/**
 * @brief Find a screen by its name
 * @param screenName Screen name to find
 * @return Pointer to QScreen if found, nullptr otherwise
 */
inline QScreen* findScreenByName(const QString& screenName)
{
    if (screenName.isEmpty()) {
        return QGuiApplication::primaryScreen();
    }
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() == screenName) {
            return screen;
        }
    }
    return nullptr;
}

/**
 * @brief Get the primary screen
 * @return Pointer to primary QScreen
 */
inline QScreen* primaryScreen()
{
    return QGuiApplication::primaryScreen();
}

/**
 * @brief Get all available screens
 * @return List of all screens
 */
inline QList<QScreen*> allScreens()
{
    return QGuiApplication::screens();
}

/**
 * @brief Find the screen containing a point
 * @param pos Position to check
 * @return Pointer to QScreen if found, primary screen as fallback
 */
inline QScreen* findScreenAtPosition(const QPoint& pos)
{
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->geometry().contains(pos)) {
            return screen;
        }
    }
    return QGuiApplication::primaryScreen();
}

/**
 * @brief Find the screen containing a point (x, y overload)
 * @param x X coordinate
 * @param y Y coordinate
 * @return Pointer to QScreen if found, primary screen as fallback
 */
inline QScreen* findScreenAtPosition(int x, int y)
{
    return findScreenAtPosition(QPoint(x, y));
}

/**
 * @brief Find the nearest screen to a point
 * @param pos Position to check
 * @return Pointer to nearest QScreen
 */
inline QScreen* findNearestScreen(const QPoint& pos)
{
    QScreen* nearest = QGuiApplication::primaryScreen();
    int minDistance = INT_MAX;

    for (QScreen* screen : QGuiApplication::screens()) {
        QPoint screenCenter = screen->geometry().center();
        int distance = (screenCenter - pos).manhattanLength();
        if (distance < minDistance) {
            minDistance = distance;
            nearest = screen;
        }
    }
    return nearest;
}

/**
 * @brief Parse a UUID string safely
 * @param uuidString String to parse
 * @return Optional QUuid, empty if invalid
 */
inline std::optional<QUuid> parseUuid(const QString& uuidString)
{
    if (uuidString.isEmpty()) {
        return std::nullopt;
    }
    QUuid uuid = QUuid::fromString(uuidString);
    if (uuid.isNull()) {
        return std::nullopt;
    }
    return uuid;
}

// ═══════════════════════════════════════════════════════════════════════════════
// JSON Parsing Utilities
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Parse a JSON string into a QJsonObject safely
 * @param json JSON string to parse
 * @return Optional QJsonObject, empty if invalid or not an object
 */
inline std::optional<QJsonObject> parseJsonObject(const QString& json)
{
    if (json.isEmpty()) {
        return std::nullopt;
    }
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }
    return doc.object();
}

/**
 * @brief Direction constants for use in comparisons
 */
namespace Direction {
inline const QString Left = QStringLiteral("left");
inline const QString Right = QStringLiteral("right");
inline const QString Up = QStringLiteral("up");
inline const QString Down = QStringLiteral("down");
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window ID Utilities
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Extract stable ID from a full window ID
 *
 * Window ID format: "windowClass:resourceName:pointerAddress"
 * Stable ID format: "windowClass:resourceName" (without pointer address)
 *
 * The stable ID allows matching windows across KWin restarts since only
 * the pointer address changes between sessions.
 *
 * @param windowId Full window ID including pointer address
 * @return Stable ID without pointer address, or original if not in expected format
 */
inline QString extractStableId(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return windowId;
    }

    // Find the last colon (separates pointer address from the rest)
    int lastColon = windowId.lastIndexOf(QLatin1Char(':'));
    if (lastColon <= 0) {
        // No colon found or only one part - return as-is
        return windowId;
    }

    // Check if what's after the last colon looks like a pointer address (all digits)
    QStringView potentialPointer = QStringView(windowId).mid(lastColon + 1);
    if (potentialPointer.isEmpty()) {
        return windowId;
    }

    bool isPointer = true;
    for (QChar c : potentialPointer) {
        if (!c.isDigit()) {
            isPointer = false;
            break;
        }
    }

    if (isPointer) {
        // Strip the pointer address
        return windowId.left(lastColon);
    }

    // Not a pointer format, return as-is
    return windowId;
}

/**
 * @brief Extract window class from a window ID or stable ID
 *
 * Window class is the first component before the first colon.
 * Examples:
 *   "firefox:firefox:123456" -> "firefox"
 *   "org.kde.dolphin:org.kde.dolphin:789" -> "org.kde.dolphin"
 *   "firefox firefox:Navigator:123" -> "firefox firefox"
 *
 * @param windowId Full window ID or stable ID
 * @return Window class (first component), or entire string if no colon found
 */
inline QString extractWindowClass(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return windowId;
    }

    int firstColon = windowId.indexOf(QLatin1Char(':'));
    if (firstColon <= 0) {
        // No colon found - return as-is (entire string is the class)
        return windowId;
    }

    return windowId.left(firstColon);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Screen Identity Utilities (EDID-based stable identification)
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Read the EDID header serial number from sysfs
 *
 * The EDID header contains a 4-byte little-endian serial number at bytes 12-15.
 * This is always present (unlike the optional text serial descriptor that
 * QScreen::serialNumber() returns) and is what KDE Display Settings shows
 * next to each monitor name.
 *
 * @param connectorName Connector name (e.g., "DP-2")
 * @return Header serial as string, or empty if not readable
 */
inline QString readEdidHeaderSerial(const QString& connectorName)
{
    // Find the sysfs EDID file: /sys/class/drm/card*-<connector>/edid
    QDir drmDir(QStringLiteral("/sys/class/drm"));
    if (!drmDir.exists()) {
        return QString();
    }

    const QStringList entries = drmDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        // Match entries like "card0-DP-2", "card1-HDMI-A-1"
        int dashPos = entry.indexOf(QLatin1Char('-'));
        if (dashPos < 0) {
            continue;
        }
        if (entry.mid(dashPos + 1) == connectorName) {
            QFile edidFile(drmDir.filePath(entry) + QStringLiteral("/edid"));
            if (!edidFile.open(QIODevice::ReadOnly)) {
                continue;
            }
            QByteArray header = edidFile.read(16);
            if (header.size() < 16) {
                continue;
            }
            // Bytes 12-15: serial number (little-endian uint32)
            const auto* data = reinterpret_cast<const uint8_t*>(header.constData());
            uint32_t serial = data[12]
                | (static_cast<uint32_t>(data[13]) << 8)
                | (static_cast<uint32_t>(data[14]) << 16)
                | (static_cast<uint32_t>(data[15]) << 24);
            if (serial != 0) {
                return QString::number(serial);
            }
            return QString();
        }
    }
    return QString();
}

/**
 * @brief Stable EDID-based identifier for a physical monitor
 *
 * Matches KDE's identification approach: uses manufacturer, model, and
 * EDID header serial number to uniquely identify monitors. The header
 * serial (uint32 from EDID bytes 12-15) is always present and is what
 * KDE Display Settings shows (e.g., "115107"), unlike the optional text
 * serial descriptor that QScreen::serialNumber() returns.
 *
 * Format: "manufacturer:model:serial" when any serial is available,
 * "manufacturer:model" when only those are available,
 * or connector name (screen->name()) as fallback for virtual displays
 * and embedded panels that lack EDID data.
 *
 * When two monitors produce the same identifier (identical EDID data),
 * the connector name is appended as a tiebreaker.
 *
 * @param screen QScreen to identify
 * @return Stable identifier string
 */
inline QString screenIdentifier(const QScreen* screen)
{
    if (!screen) {
        return QString();
    }

    const QString manufacturer = screen->manufacturer();
    const QString model = screen->model();

    // Prefer Qt's text serial descriptor (from EDID descriptor blocks)
    QString serial = screen->serialNumber();

    // Fallback: read the EDID header serial from sysfs (always present, what KDE shows)
    if (serial.isEmpty()) {
        serial = readEdidHeaderSerial(screen->name());
    }

    if (!serial.isEmpty()) {
        return manufacturer + QLatin1Char(':') + model + QLatin1Char(':') + serial;
    }
    if (!manufacturer.isEmpty() || !model.isEmpty()) {
        return manufacturer + QLatin1Char(':') + model;
    }
    // Fallback: connector name (virtual displays, some embedded panels)
    return screen->name();
}

/**
 * @brief Resolve a connector name to a stable screen ID
 *
 * Looks up the currently connected QScreen with the given connector name
 * and returns its EDID-based identifier. Returns the connector name as-is
 * if no matching screen is found.
 *
 * @param connectorName Connector name (e.g., "DP-2")
 * @return Stable screen ID, or connectorName if not resolvable
 */
inline QString screenIdForName(const QString& connectorName)
{
    if (connectorName.isEmpty()) {
        return connectorName;
    }
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() == connectorName) {
            return screenIdentifier(screen);
        }
    }
    return connectorName;
}

/**
 * @brief Reverse lookup: stable screen ID to current connector name
 *
 * Finds the currently connected QScreen whose EDID-based identifier
 * matches the given screenId and returns its connector name.
 *
 * @param screenId Stable EDID-based identifier
 * @return Current connector name, or empty string if no match
 */
inline QString screenNameForId(const QString& screenId)
{
    if (screenId.isEmpty()) {
        return QString();
    }
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screenIdentifier(screen) == screenId) {
            return screen->name();
        }
    }
    return QString();
}

/**
 * @brief Check if a string looks like a connector name rather than a screen ID
 *
 * Screen IDs (EDID-based) contain colons (e.g., "DEL:DELL U2722D:ABC123").
 * Connector names never contain colons (e.g., "DP-2", "HDMI-1").
 */
inline bool isConnectorName(const QString& identifier)
{
    return !identifier.isEmpty() && !identifier.contains(QLatin1Char(':'));
}

} // namespace Utils
} // namespace PlasmaZones
