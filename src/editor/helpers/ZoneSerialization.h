// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QVariantList>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Zone serialization utilities for clipboard and import/export
 *
 * Centralizes zone ⇄ JSON conversion to avoid duplication across
 * clipboard operations, import/export, and other persistence needs.
 * Fixes DRY violation where the same zone property set was duplicated.
 */
namespace ZoneSerialization {

/**
 * @brief Serialize zones to JSON format for clipboard
 * @param zones List of zones to serialize (QVariantMap format from ZoneManager)
 * @return JSON string containing zone data with PlasmaZones clipboard format
 *
 * The output format includes:
 * - version: "1.0"
 * - application: "PlasmaZones"
 * - dataType: "zones"
 * - zones: Array of zone objects with all properties
 *
 * Each zone gets a new UUID generated for paste operations.
 */
QString serializeZonesToClipboard(const QVariantList& zones);

/**
 * @brief Deserialize zones from clipboard JSON format
 * @param clipboardText JSON string from clipboard
 * @return List of zones in QVariantMap format, or empty list if invalid
 *
 * Validates the clipboard format (application = "PlasmaZones", dataType = "zones")
 * and converts all zone properties back to QVariantMap format used by ZoneManager.
 */
QVariantList deserializeZonesFromClipboard(const QString& clipboardText);

/**
 * @brief Validate clipboard content without full deserialization
 * @param clipboardText Text content from clipboard
 * @return true if content appears to be valid PlasmaZones zone data
 *
 * Quick validation for canPaste() checks - only validates format,
 * not full zone data integrity.
 */
bool isValidClipboardFormat(const QString& clipboardText);

/**
 * @brief Prepare zones for pasting with new IDs and adjusted positions
 * @param zones Deserialized zones from clipboard
 * @param offsetX X offset to apply (e.g., for paste-with-offset)
 * @param offsetY Y offset to apply
 * @param startingZoneNumber Starting zone number for numbering
 * @return Prepared zones ready for insertion via ZoneManager
 */
QVariantList prepareZonesForPaste(const QVariantList& zones, qreal offsetX, qreal offsetY, int startingZoneNumber);

} // namespace ZoneSerialization

} // namespace PlasmaZones
