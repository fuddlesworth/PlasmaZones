// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QVariantList>
#include <QString>

namespace PlasmaZones {

/**
 * @brief PhosphorZones::Zone serialization utilities for clipboard and import/export
 *
 * Centralizes zone ⇄ JSON conversion for clipboard operations,
 * import/export, and other persistence needs.
 */
namespace ZoneSerialization {

/**
 * @brief Serialize zones to JSON format for clipboard
 * @param zones List of zones to serialize (QVariantMap format from ZoneManager)
 * @param fixedGeometryInDevicePixels The source layout's unit for its fixed
 *        zones' pixels, so a paste into a layout counting the other unit can
 *        convert them
 * @return JSON string containing zone data with PlasmaZones clipboard format
 *
 * The output format includes:
 * - version: "1.0"
 * - application: "PlasmaZones"
 * - dataType: "zones"
 * - fixedGeometryInDevicePixels: the unit above
 * - zones: Array of zone objects with all properties, a fixed zone's
 *   geometry mode and pixel rect included
 *
 * Each zone gets a new UUID generated for paste operations.
 */
QString serializeZonesToClipboard(const QVariantList& zones, bool fixedGeometryInDevicePixels);

/**
 * @brief Deserialize zones from clipboard JSON format
 * @param clipboardText JSON string from clipboard
 * @param fixedGeometryInDevicePixels Receives the unit the fixed zones' pixels
 *        were copied in (false when the clipboard does not say); may be null
 * @return List of zones in QVariantMap format, or empty list if invalid
 *
 * Validates the clipboard format (application = "PlasmaZones", dataType = "zones")
 * and converts all zone properties back to QVariantMap format used by ZoneManager.
 */
QVariantList deserializeZonesFromClipboard(const QString& clipboardText, bool* fixedGeometryInDevicePixels = nullptr);

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
