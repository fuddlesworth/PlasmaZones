// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QGuiApplication>
#include <QScreen>
#include <QUuid>
#include <QString>
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

/**
 * @brief Check if a string is a valid UUID
 * @param uuidString String to check
 * @return true if valid UUID, false otherwise
 */
inline bool isValidUuid(const QString& uuidString)
{
    if (uuidString.isEmpty()) {
        return false;
    }
    return !QUuid::fromString(uuidString).isNull();
}

/**
 * @brief Convert navigation direction string to enum-like index
 * @param direction Direction string ("left", "right", "up", "down")
 * @return Index (0=left, 1=right, 2=up, 3=down), -1 if invalid
 */
inline int directionToIndex(const QString& direction)
{
    static const QStringList directions = {QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("up"),
                                           QStringLiteral("down")};
    return directions.indexOf(direction.toLower());
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

} // namespace Utils
} // namespace PlasmaZones
