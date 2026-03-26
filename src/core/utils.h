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
 * @brief Find a screen by its connector name
 * @param connectorName Connector name to find (e.g., "DP-2")
 * @return Pointer to QScreen if found, nullptr otherwise
 */
inline QScreen* findScreenByName(const QString& connectorName)
{
    if (connectorName.isEmpty()) {
        return QGuiApplication::primaryScreen();
    }
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() == connectorName) {
            return screen;
        }
    }
    return nullptr;
}

// Defined in utils.cpp (EDID-based lookup)
PLASMAZONES_EXPORT QScreen* findScreenByIdOrName(const QString& identifier);

/**
 * @brief Get the aspect ratio of a screen
 * @param screen QScreen pointer (returns 0.0 if null or degenerate geometry)
 * @return width/height ratio, or 0.0 if unavailable
 */
inline qreal screenAspectRatio(QScreen* screen)
{
    if (!screen)
        return 0.0;
    const QRect geom = screen->geometry();
    if (geom.height() <= 0)
        return 0.0;
    return static_cast<qreal>(geom.width()) / geom.height();
}

/**
 * @brief Get the aspect ratio of a screen by name/ID (virtual-screen-aware)
 * @param screenNameOrId Screen connector name, EDID-based ID, or virtual screen ID
 * @return width/height ratio, or 0.0 if screen not found
 *
 * For virtual screen IDs, uses ScreenManager::screenGeometry() to get the
 * virtual screen dimensions. Falls back to physical QScreen* for non-virtual IDs.
 */
PLASMAZONES_EXPORT qreal screenAspectRatio(const QString& screenNameOrId);

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
 * @brief Extract app identity from a full window ID
 *
 * Window ID format: "appId|internalId"
 * App ID is the application identity (desktopFileName or normalized windowClass)
 * that persists across KWin restarts.
 *
 * @param windowId Full window ID
 * @return App ID portion, or original string if not in expected format
 */
inline QString extractAppId(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return windowId;
    }
    int sep = windowId.indexOf(QLatin1Char('|'));
    return (sep > 0) ? windowId.left(sep) : windowId;
}

/**
 * @brief Extract window class from a window ID
 *
 * With the new "appId|internalId" format, the app ID is the window class.
 * Examples:
 *   "firefox|a1b2c3d4-..." -> "firefox"
 *   "org.kde.dolphin|a1b2c3d4-..." -> "org.kde.dolphin"
 *
 * @param windowId Full window ID or app ID
 * @return Window class (app ID portion), or entire string if no separator found
 */
inline QString extractWindowClass(const QString& windowId)
{
    return extractAppId(windowId);
}

/**
 * @brief Segment-aware app ID matching.
 *
 * Returns true if @p pattern matches @p appId using dot-segment-boundary rules.
 * Handles both directions and partial last-segment prefixes.
 *
 * Match rules (all case-insensitive):
 *   - Exact: "firefox" == "firefox"
 *   - Trailing dot-segment: "firefox" matches "org.mozilla.firefox"
 *   - Reverse trailing: "org.mozilla.firefox" matches appId "firefox"
 *   - Last-segment prefix: "systemsettings" matches "org.kde.systemsettings5"
 *     (pattern matches start of appId's last dot-segment)
 *
 * Does NOT match arbitrary substrings: "fire" does NOT match "firefox"
 * because "fire" is not a complete segment and the last segment "firefox"
 * does not start with "fire" (length 4 < 5 threshold to prevent short matches).
 */
inline bool appIdMatches(const QString& appId, const QString& pattern)
{
    if (appId.isEmpty() || pattern.isEmpty()) {
        return false;
    }
    if (appId.compare(pattern, Qt::CaseInsensitive) == 0) {
        return true;
    }
    // Trailing dot-segment: "org.mozilla.firefox" ends with ".firefox"
    if (appId.length() > pattern.length() + 1 && appId[appId.length() - pattern.length() - 1] == QLatin1Char('.')
        && appId.endsWith(pattern, Qt::CaseInsensitive)) {
        return true;
    }
    // Reverse: appId is a trailing dot-segment of pattern
    if (pattern.length() > appId.length() + 1 && pattern[pattern.length() - appId.length() - 1] == QLatin1Char('.')
        && pattern.endsWith(appId, Qt::CaseInsensitive)) {
        return true;
    }
    // Last-segment prefix: "systemsettings" matches start of last segment "systemsettings5"
    // Only for patterns >= 5 chars to prevent short false positives like "fire" → "firefox"
    if (pattern.length() >= 5) {
        int lastDot = appId.lastIndexOf(QLatin1Char('.'));
        if (lastDot >= 0) {
            QStringView lastSeg = QStringView(appId).mid(lastDot + 1);
            if (lastSeg.startsWith(pattern, Qt::CaseInsensitive) && lastSeg.length() != pattern.length()) {
                return true;
            }
        }
        // Reverse: appId matches start of pattern's last segment
        lastDot = pattern.lastIndexOf(QLatin1Char('.'));
        if (lastDot >= 0 && appId.length() >= 5) {
            QStringView lastSeg = QStringView(pattern).mid(lastDot + 1);
            if (lastSeg.startsWith(appId, Qt::CaseInsensitive) && lastSeg.length() != appId.length()) {
                return true;
            }
        }
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Screen Identity Utilities (EDID-based stable identification)
// Declarations — bodies in utils.cpp
// ═══════════════════════════════════════════════════════════════════════════════

/**
 * @brief Read the EDID header serial number from sysfs (cached)
 */
PLASMAZONES_EXPORT QString readEdidHeaderSerial(const QString& connectorName);

/**
 * @brief Invalidate the EDID header serial cache for a connector
 */
PLASMAZONES_EXPORT void invalidateEdidCache(const QString& connectorName = QString());

/**
 * @brief Stable EDID-based identifier for a physical monitor
 */
PLASMAZONES_EXPORT QString screenIdentifier(const QScreen* screen);

/**
 * @brief Resolve a connector name to a stable screen ID
 */
PLASMAZONES_EXPORT QString screenIdForName(const QString& connectorName);

/**
 * @brief Reverse lookup: stable screen ID to current connector name
 */
PLASMAZONES_EXPORT QString screenNameForId(const QString& screenId);

/**
 * @brief Check if a string looks like a connector name rather than a screen ID
 */
PLASMAZONES_EXPORT bool isConnectorName(const QString& identifier);

/**
 * @brief Format-agnostic screen comparison.
 *
 * Returns true if both identifiers refer to the same physical screen,
 * regardless of whether they are connector names ("DP-2") or EDID-based
 * screen IDs ("LG Electronics:LG Ultra HD:115107"). Resolves both to
 * QScreen* via findScreenByIdOrName and compares pointers.
 */
PLASMAZONES_EXPORT bool screensMatch(const QString& a, const QString& b);

/**
 * @brief Check for duplicate screen identifiers among connected monitors
 */
PLASMAZONES_EXPORT void warnDuplicateScreenIds();

} // namespace Utils
} // namespace PlasmaZones
