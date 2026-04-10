// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Compositor-agnostic EDID parsing and screen ID construction
 *
 * Provides the shared algorithm for building stable "manufacturer:model:serial"
 * screen identifiers from EDID data. Used by both the daemon (via QScreen) and
 * compositor plugins (via wlr_output / KWin::Output) to produce byte-identical
 * screen IDs across process boundaries.
 *
 * The EDID header format (bytes 0-15) is a hardware standard (VESA E-EDID 1.3+):
 * - Bytes 0-7: magic header (00 FF FF FF FF FF FF 00)
 * - Bytes 8-9: manufacturer ID
 * - Bytes 10-11: product code
 * - Bytes 12-15: serial number (little-endian uint32)
 *
 * @note Thread safety: All functions must be called from the main thread.
 * Caching uses process-local static QHash (no mutex).
 */
namespace ScreenIdUtils {

/**
 * @brief Read the EDID header serial number from sysfs for a DRM connector.
 *
 * Scans /sys/class/drm/card*-<connectorName>/edid, validates the EDID magic
 * header, and extracts the serial number from bytes 12-15 (little-endian uint32).
 *
 * Results are cached after first successful read. After 3 consecutive misses
 * for a connector, the empty result is cached permanently to avoid unbounded
 * sysfs I/O for virtual displays and embedded panels.
 *
 * @param connectorName DRM connector name (e.g., "DP-2", "HDMI-A-1")
 * @return Decimal serial string (e.g., "115107"), or empty string if unavailable
 */
QString readEdidHeaderSerial(const QString& connectorName);

/**
 * @brief Invalidate cached EDID serial data.
 *
 * Call on monitor hotplug (connect/disconnect) to force re-read on next access.
 *
 * @param connectorName Connector to invalidate, or empty to clear all
 */
void invalidateEdidCache(const QString& connectorName = QString());

/**
 * @brief Normalize a hex serial string to decimal.
 *
 * KWin on Wayland exposes the EDID header serial as hex via QScreen::serialNumber()
 * (e.g., "0x0001C1A3"). This normalizes to decimal (e.g., "115107") so both daemon
 * and compositor produce identical screen IDs.
 *
 * @param serial Raw serial string (may be hex with "0x" prefix, or already decimal)
 * @return Normalized serial string (decimal if conversion succeeded, original otherwise)
 */
inline QString normalizeHexSerial(const QString& serial)
{
    if (!serial.isEmpty() && serial.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        bool ok = false;
        uint32_t numericSerial = serial.toUInt(&ok, 16);
        if (ok && numericSerial != 0) {
            return QString::number(numericSerial);
        }
    }
    return serial;
}

/**
 * @brief Build a base screen identifier from manufacturer/model/serial.
 *
 * Returns "manufacturer:model:serial" when serial is available,
 * "manufacturer:model" when only those are available, or empty string
 * when all fields are empty (caller should fall back to connector name).
 *
 * @param manufacturer Monitor manufacturer (from EDID or QScreen)
 * @param model Monitor model (from EDID or QScreen)
 * @param serial Normalized serial string (from normalizeHexSerial + readEdidHeaderSerial fallback)
 * @return Base identifier string, or empty if no useful fields
 */
inline QString buildBaseScreenId(const QString& manufacturer, const QString& model, const QString& serial)
{
    if (!serial.isEmpty()) {
        return manufacturer + QLatin1Char(':') + model + QLatin1Char(':') + serial;
    }
    if (!manufacturer.isEmpty() || !model.isEmpty()) {
        return manufacturer + QLatin1Char(':') + model;
    }
    return QString();
}

/**
 * @brief Build a complete base screen ID from QScreen properties + EDID fallback.
 *
 * Tries QScreen::serialNumber() first (normalized), then falls back to sysfs EDID.
 * Returns "manufacturer:model:serial" or connector name fallback.
 *
 * @param manufacturer Screen manufacturer
 * @param model Screen model
 * @param serialNumber Raw serial from QScreen::serialNumber()
 * @param connectorName DRM connector name (fallback)
 * @return Screen identifier string
 */
inline QString buildScreenBaseId(const QString& manufacturer, const QString& model, const QString& serialNumber,
                                 const QString& connectorName)
{
    QString serial = normalizeHexSerial(serialNumber);
    if (serial.isEmpty()) {
        serial = readEdidHeaderSerial(connectorName);
    }
    QString baseId = buildBaseScreenId(manufacturer, model, serial);
    return baseId.isEmpty() ? connectorName : baseId;
}

} // namespace ScreenIdUtils
} // namespace PlasmaZones
