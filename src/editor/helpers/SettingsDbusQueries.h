// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace PlasmaZones {

/**
 * @brief D-Bus queries for daemon Settings service
 *
 * Centralizes D-Bus calls to the PlasmaZones daemon's Settings interface.
 * Avoids duplicating the query pattern across EditorController methods.
 */
namespace SettingsDbusQueries {

/**
 * @brief Batch-fetch multiple settings from the daemon in one D-Bus call.
 * @param keys List of setting keys to fetch
 * @return Map of key → value for keys the daemon recognized; unknown keys
 *         are omitted and callers must fall back to their own defaults.
 *
 * Collapses N individual getSetting() round-trips into one — the primary
 * reason this helper exists. Used on the editor startup hot path by
 * refreshGlobalGapOverlaySettings() in gaps.cpp.
 *
 * still sees the user's real configured values. Returns an empty map if
 * the daemon is unreachable or the call times out (500 ms cap); callers
 * should treat missing keys and empty maps the same way (use defaults).
 */
QVariantMap querySettingsBatch(const QStringList& keys);

/**
 * @brief Query a boolean setting from the daemon via D-Bus
 * @param settingKey The setting key to query
 * @param defaultValue Value to return if query fails
 * @return The setting value, or defaultValue if unavailable
 */
bool queryBoolSetting(const QString& settingKey, bool defaultValue);

} // namespace SettingsDbusQueries

} // namespace PlasmaZones
