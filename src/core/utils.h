// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <window_id.h>

#include <QGuiApplication>
#include <QScreen>
#include <QUuid>
#include <QString>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDir>
#include <QFile>
#include <QLatin1StringView>
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
inline constexpr QLatin1StringView Left{"left"};
inline constexpr QLatin1StringView Right{"right"};
inline constexpr QLatin1StringView Up{"up"};
inline constexpr QLatin1StringView Down{"down"};
} // namespace Direction

// ═══════════════════════════════════════════════════════════════════════════════
// Window ID Utilities
// ═══════════════════════════════════════════════════════════════════════════════

// Window-id parsing utilities (extractAppId / extractInstanceId / appIdMatches)
// live in libs/phosphor-identity as `PhosphorIdentity::WindowId::*`. All
// PlasmaZones-side callers reference them qualified directly — no forwarding
// wrappers here.
//
// Wire format: `"appId|instanceId"`.
//   appId       — compositor-reported app identity:
//                   • preferred: `KWin::Window::desktopFileName()` (stable
//                     cross-session),
//                   • fallback:  normalized `KWin::EffectWindow::windowClass()`
//                     (X11: resourceClass after the space; Wayland: app_id
//                     as-is), lowercased.
//                 Composed in the KWin effect's getWindowAppId()
//                 (kwin-effect/plasmazoneseffect.cpp) — the daemon only
//                 observes the joined string.
//   instanceId  — `KWin::Window::internalId().toString(QUuid::WithoutBraces)`,
//                 stable for the window's lifetime within a KWin session and
//                 immune to WM_CLASS mutations (Electron/CEF apps).
// The effect caches the composite at first observation so the daemon's
// maps keyed by windowId remain stable across late class changes.

/**
 * @brief Resolve the effective screen ID at a global position (virtual-screen-aware)
 *
 * Queries ScreenManager::effectiveScreenAt() for virtual screen resolution,
 * falling back to the physical QScreen's stable identifier. Eliminates the
 * repeated pattern of:
 *   auto* mgr = ScreenManager::instance();
 *   QString id = mgr ? mgr->effectiveScreenAt(pos) : QString();
 *   if (id.isEmpty()) id = Utils::screenIdentifier(screen);
 *
 * @param pos Global compositor position
 * @param fallbackScreen Physical QScreen* to derive screen ID from if ScreenManager
 *        is unavailable or pos is outside all virtual screens. If nullptr, the
 *        QScreen at pos (via QGuiApplication::screenAt) is used.
 * @return Effective screen ID (virtual if subdivided, physical otherwise), or
 *         empty string if no screen could be resolved
 */
PLASMAZONES_EXPORT QString effectiveScreenIdAt(const QPoint& pos, QScreen* fallbackScreen = nullptr);

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
 * @brief Format-agnostic screen comparison (virtual-screen aware).
 *
 * Returns true if both identifiers refer to the same effective screen.
 * Handles connector names ("DP-2"), EDID-based screen IDs
 * ("LG Electronics:LG Ultra HD:115107"), and virtual screen IDs
 * ("Dell:U2722D:115107/vs:0"). Virtual screen IDs are never equal to
 * each other (unless identical strings) and never equal to their parent
 * physical screen ID — the physical screen has been subdivided, so the
 * physical ID no longer represents a usable screen. Resolves non-virtual
 * identifiers to QScreen* via findScreenByIdOrName and compares pointers.
 */
PLASMAZONES_EXPORT bool screensMatch(const QString& a, const QString& b);

/**
 * @brief Check whether @p storedScreenId belongs to a given physical screen
 *
 * Returns true when @p storedScreenId is either:
 *   - the physical screen ID itself (resolves via screensMatch), OR
 *   - a virtual screen ID whose physical parent is @p physicalScreenId.
 *
 * This is the "filter by physical screen including its virtual children"
 * operation that screensMatch deliberately does NOT provide (different
 * virtual IDs are not equivalent to each other or to their parent under
 * screensMatch's strict semantics). Use this for "do something to all
 * windows on this physical screen, including any subdivisions" — e.g.
 * resnap on VS reconfiguration.
 *
 * Both empty / both invalid → false. Empty physicalScreenId → false.
 */
PLASMAZONES_EXPORT bool belongsToPhysicalScreen(const QString& storedScreenId, const QString& physicalScreenId);

/**
 * @brief Check for duplicate screen identifiers among connected monitors
 */
PLASMAZONES_EXPORT void warnDuplicateScreenIds();

/// Build the context lock key for a given mode and screen.
/// Format: "mode:screenId" (e.g. "0:Dell:U2722D:115107/vs:0").
inline QString contextLockKey(int mode, const QString& screenId)
{
    return QString::number(mode) + QStringLiteral(":") + screenId;
}

} // namespace Utils
} // namespace PlasmaZones
