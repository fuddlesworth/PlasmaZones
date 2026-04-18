// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphoridentity_export.h>

#include <QString>
#include <QStringView>

namespace PhosphorIdentity {

/**
 * @brief Compositor-agnostic window-identity utilities.
 *
 * Operates on the canonical composite window-id format `"appId|instanceId"`.
 * The format is stable across processes — KWin effect, daemon, and every
 * library reading window assignments must spell it the same way.  This is
 * the single source of truth for parsing, building, and matching the
 * format.
 */
namespace WindowId {

/**
 * @brief Extract app identity from window ID (portion before the '|' separator)
 * Format: "appId|internalUuid" → returns "appId"
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
 * @brief Extract the stable KWin instance identifier (UUID) from a full window ID.
 *
 * Legacy composite format: "appId|internalId". The instance id is stable for
 * the window's lifetime; the appId is mutable (Electron/CEF apps rebroadcast
 * WM_CLASS mid-session). Per discussion #271, runtime keys use the bare
 * instance id — this helper exists for legacy fixtures and compat paths.
 */
inline QString extractInstanceId(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return windowId;
    }
    int sep = windowId.indexOf(QLatin1Char('|'));
    return (sep >= 0) ? windowId.mid(sep + 1) : windowId;
}

/**
 * @brief Derive short name from app ID for icon/app display
 * Reverse-DNS: "org.kde.dolphin" → last dot-segment (e.g., "dolphin")
 * Simple name: "firefox" → as-is
 */
inline QString deriveShortName(const QString& windowClass)
{
    if (windowClass.isEmpty()) {
        return QString();
    }
    int dotIdx = windowClass.lastIndexOf(QLatin1Char('.'));
    if (dotIdx >= 0 && dotIdx < windowClass.length() - 1) {
        return windowClass.mid(dotIdx + 1);
    }
    return windowClass;
}

/**
 * @brief Segment-aware app ID matching for exclusion lists.
 *
 * Matches reverse-DNS app IDs flexibly:
 * - Exact match (case-insensitive): "firefox" == "Firefox"
 * - Trailing dot-segment: "org.mozilla.firefox" matches pattern "firefox"
 * - Reverse trailing: pattern "org.mozilla.firefox" matches appId "firefox"
 * - Last-segment prefix (pattern >= 5 chars): "systemsettings" matches "systemsettings5"
 *
 * The 5-char minimum on prefix matching prevents short false positives
 * like "fire" matching "firefox".
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

} // namespace WindowId
} // namespace PhosphorIdentity
