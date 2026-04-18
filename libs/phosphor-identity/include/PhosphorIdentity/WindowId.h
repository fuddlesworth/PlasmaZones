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
    return (sep >= 0) ? windowId.left(sep) : windowId;
}

/**
 * @brief Extract the stable KWin instance identifier (UUID) from a full window ID.
 *
 * Legacy composite format: "appId|internalId". The instance id is stable for
 * the window's lifetime; the appId is mutable (Electron/CEF apps rebroadcast
 * WM_CLASS mid-session). Per discussion #271, runtime keys use the bare
 * instance id — this helper exists for legacy fixtures and compat paths.
 *
 * A bare appId with no '|' separator is returned verbatim.
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
    // Strip a trailing dot first so "org.kde.foo." behaves the same as
    // "org.kde.foo" — the dot would otherwise short-circuit the segment
    // extraction below into returning the original string verbatim.
    QStringView trimmed(windowClass);
    while (!trimmed.isEmpty() && trimmed.back() == QLatin1Char('.')) {
        trimmed.chop(1);
    }
    if (trimmed.isEmpty()) {
        return QString();
    }
    const int dotIdx = trimmed.lastIndexOf(QLatin1Char('.'));
    if (dotIdx >= 0 && dotIdx < trimmed.length() - 1) {
        return trimmed.mid(dotIdx + 1).toString();
    }
    return trimmed.toString();
}

/**
 * @brief Segment-aware app ID matching for exclusion lists.
 *
 * Matches reverse-DNS app IDs flexibly:
 * - Exact match (case-insensitive): "firefox" == "Firefox"
 * - Trailing dot-segment: "org.mozilla.firefox" matches pattern "firefox"
 * - Reverse trailing: pattern "org.mozilla.firefox" matches appId "firefox"
 * - Last-segment prefix (prefix >= 5 chars): "systemsettings" matches "systemsettings5"
 *
 * The 5-char minimum always applies to the *prefix* candidate — whichever
 * operand is the shorter one in the prefix/full-segment relationship. This
 * prevents short false positives like appId "fire" matching pattern
 * "org.mozilla.firefox" (the "firefox" last-segment would otherwise start
 * with "fire").
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
    // Last-segment prefix: pattern "systemsettings" matches start of appId's
    // last segment "systemsettings5". Here `pattern` is the prefix candidate,
    // so gate on its length.
    if (pattern.length() >= 5) {
        const int lastDot = appId.lastIndexOf(QLatin1Char('.'));
        if (lastDot >= 0) {
            QStringView lastSeg = QStringView(appId).mid(lastDot + 1);
            if (lastSeg.startsWith(pattern, Qt::CaseInsensitive) && lastSeg.length() != pattern.length()) {
                return true;
            }
        }
    }
    // Reverse: appId matches start of pattern's last segment. Here `appId`
    // is the prefix candidate, so gate on its length (not pattern's).
    if (appId.length() >= 5) {
        const int lastDot = pattern.lastIndexOf(QLatin1Char('.'));
        if (lastDot >= 0) {
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
