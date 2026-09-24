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
 * Collapses N individual getSetting() round-trips into one, which is the primary reason
 * this helper exists. The editor startup hot path uses it:
 * refreshGlobalGapOverlaySettings() in gaps.cpp.
 *
 * Returns an empty map if
 * the daemon is unreachable or the call times out (500 ms cap); callers
 * should treat missing keys and empty maps the same way (use defaults).
 */
QVariantMap querySettingsBatch(const QStringList& keys);

} // namespace SettingsDbusQueries

} // namespace PlasmaZones
