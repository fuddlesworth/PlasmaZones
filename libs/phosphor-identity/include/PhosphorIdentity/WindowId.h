// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// PhosphorIdentity is an INTERFACE library — no generated export header.
// Every helper below is `inline` and lives entirely in this header.

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
 * @brief Build a composite window id from its appId + instance parts.
 *
 * Symmetric counterpart to @c extractAppId / @c extractInstanceId — together
 * these three helpers make this header the single source of truth for the
 * wire format. Callers should prefer this over hand-rolled
 * `appId + "|" + instanceId` concatenation so the separator choice stays in
 * exactly one place.
 *
 * Either part may be empty; the result is always formed, with the separator
 * preserved when @p instanceId is non-empty. An empty @p instanceId yields a
 * bare @p appId (no trailing separator), which matches what
 * @c extractAppId would reconstruct.
 */
inline QString buildCompositeId(QStringView appId, QStringView instanceId)
{
    if (instanceId.isEmpty()) {
        return appId.toString();
    }
    QString out;
    out.reserve(appId.size() + 1 + instanceId.size());
    out.append(appId);
    out.append(QLatin1Char('|'));
    out.append(instanceId);
    return out;
}

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
 * @brief Derive a canonical appId from a window's desktop-file name and class.
 *
 * Prefers @p desktopFileName (stable across sessions). Falls back to the
 * LAST whitespace-delimited token of @p windowClass: an X11 window class is
 * "resourceName resourceClass" and the resourceName may itself contain
 * spaces, so the last space is the split point and the resourceClass is the
 * token kept. A Wayland app_id has no space and is taken whole.
 *
 * The result is trimmed and lower-cased. A blank or whitespace-only input
 * (KWin reports a bare " " class for some unmapped / transient surfaces)
 * yields an EMPTY string, never " " — a space-only appId would otherwise
 * become a live key in every appId-map (most damagingly the snap restore
 * queue, where it lets unrelated blank-class windows consume each other's
 * saved zones). @c isValidAppId rejects whatever survives.
 */
inline QString normalizeAppId(const QString& desktopFileName, const QString& windowClass)
{
    QString appId = desktopFileName.trimmed();
    if (appId.isEmpty()) {
        // Trim the whole class BEFORE the split: a class with trailing
        // whitespace ("resourceName resourceClass ") would otherwise split at
        // the trailing space, yield an empty token, and drop an otherwise
        // valid identity.
        const QString trimmedClass = windowClass.trimmed();
        const int sep = trimmedClass.lastIndexOf(QLatin1Char(' '));
        appId = (sep >= 0 ? trimmedClass.mid(sep + 1) : trimmedClass);
    }
    return appId.toLower();
}

/**
 * @brief Whether @p appId is a well-formed canonical app identifier.
 *
 * A canonical appId is a non-empty, whitespace-free token — a desktop-file
 * name ("org.kde.konsole") or a normalized window-class token. A blank,
 * whitespace-only, or whitespace-bearing string is a corrupt identity (a
 * KWin bare-" " class, or a stale pre-3.0 "resourceName resourceClass"
 * key) and must never be used as a map key. Gate every appId-keyed insert
 * and every persisted-key load on this.
 */
inline bool isValidAppId(const QString& appId)
{
    if (appId.isEmpty()) {
        return false;
    }
    for (const QChar c : appId) {
        if (c.isSpace()) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Derive short name from app ID for icon/app display
 * Reverse-DNS: "org.kde.dolphin" → last dot-segment (e.g., "dolphin")
 * Simple name: "firefox" → as-is
 *
 * @warning Operates on a bare appId, not a composite window id. If you hold a
 * composite `appId|instanceId`, run it through @c extractAppId first — passing
 * the composite in directly would make the '|'-separated instance id look
 * like a trailing segment and produce that as the short name.
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
