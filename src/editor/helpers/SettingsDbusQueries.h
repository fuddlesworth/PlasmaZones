// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVariant>

namespace PlasmaZones {

/**
 * @brief D-Bus queries for daemon Settings service
 *
 * Centralizes D-Bus calls to the PlasmaZones daemon's Settings interface.
 * Avoids duplicating the query pattern across EditorController methods.
 */
namespace SettingsDbusQueries {

/**
 * @brief Query an integer setting from the daemon via D-Bus
 * @param settingKey The setting key to query (e.g., "zonePadding", "outerGap")
 * @param defaultValue Value to return if query fails or returns invalid data
 * @return The setting value, or defaultValue if unavailable
 *
 * Connects to org.plasmazones Settings interface and calls getSetting().
 * Returns defaultValue if:
 * - D-Bus connection fails
 * - Setting doesn't exist
 * - Value is negative (invalid for these settings)
 */
int queryIntSetting(const QString& settingKey, int defaultValue);

/**
 * @brief Query the global zone padding setting
 * @return Zone padding in pixels, or default if unavailable
 */
int queryGlobalZonePadding();

/**
 * @brief Query the global outer gap setting
 * @return Outer gap in pixels, or default if unavailable
 */
int queryGlobalOuterGap();

} // namespace SettingsDbusQueries

} // namespace PlasmaZones
