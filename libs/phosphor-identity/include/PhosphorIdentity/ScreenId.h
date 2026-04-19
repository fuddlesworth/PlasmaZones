// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Compositor-portable EDID parsing + screen-ID format helpers.
//
// Header-only inline so PhosphorIdentity stays INTERFACE — the cross-
// process consumers (KWin effect, Wayfire plugin, daemon) all share one
// definition without anyone shipping an extra .so. The function-local
// static caches are guaranteed unique across translation units by the
// C++17 inline-function-static rule.

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QIODevice>
#include <QLatin1Char>
#include <QLatin1String>
#include <QString>
#include <QStringList>

#include <cstdint>

namespace PhosphorIdentity {

/**
 * @brief Compositor-agnostic EDID parsing and screen-ID construction.
 *
 * Owns the canonical "manufacturer:model:serial" wire format used by
 * every Phosphor consumer that refers to a physical screen across
 * processes. Daemon (via QScreen properties) and compositor plugins
 * (via wlr_output / KWin::Output) feed in their own raw fields and
 * get back byte-identical IDs.
 *
 * EDID header format (VESA E-EDID 1.3+, sysfs blob /sys/class/drm/.../edid):
 *   Bytes 0-7   : magic header (00 FF FF FF FF FF FF 00)
 *   Bytes 8-9   : manufacturer ID
 *   Bytes 10-11 : product code
 *   Bytes 12-15 : serial number (little-endian uint32)
 *
 * Threading: the sysfs cache is accessed without synchronisation; every
 * function MUST be called from the GUI / main thread.
 */
namespace ScreenId {

namespace detail {

inline QHash<QString, QString>& edidSerialCache()
{
    static QHash<QString, QString> s_cache;
    return s_cache;
}

inline QHash<QString, int>& edidMissCounter()
{
    static QHash<QString, int> s_counter;
    return s_counter;
}

} // namespace detail

/**
 * @brief Read the EDID header serial from sysfs for a DRM connector.
 *
 * Scans `/sys/class/drm/card*-<connectorName>/edid`, validates the EDID
 * magic header, and extracts the serial number from bytes 12-15
 * (little-endian uint32, decimal-formatted).
 *
 * Cached after the first successful read. After 3 consecutive misses
 * the empty result is cached permanently to avoid unbounded sysfs I/O
 * for virtual displays / embedded panels. Use @ref invalidateEdidCache
 * on hotplug.
 */
inline QString readEdidHeaderSerial(const QString& connectorName)
{
    auto& cache = detail::edidSerialCache();
    auto cacheIt = cache.constFind(connectorName);
    if (cacheIt != cache.constEnd()) {
        return *cacheIt;
    }

    QString result;

    QDir drmDir(QStringLiteral("/sys/class/drm"));
    if (drmDir.exists()) {
        const QStringList entries = drmDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& entry : entries) {
            int dashPos = entry.indexOf(QLatin1Char('-'));
            if (dashPos < 0) {
                continue;
            }
            if (entry.mid(dashPos + 1) != connectorName) {
                continue;
            }
            QFile edidFile(drmDir.filePath(entry) + QStringLiteral("/edid"));
            if (!edidFile.open(QIODevice::ReadOnly)) {
                continue;
            }
            QByteArray header = edidFile.read(16);
            if (header.size() < 16) {
                continue;
            }
            const auto* data = reinterpret_cast<const uint8_t*>(header.constData());
            if (data[0] != 0x00 || data[1] != 0xFF || data[2] != 0xFF || data[3] != 0xFF || data[4] != 0xFF
                || data[5] != 0xFF || data[6] != 0xFF || data[7] != 0x00) {
                continue;
            }
            uint32_t serial = data[12] | (static_cast<uint32_t>(data[13]) << 8)
                | (static_cast<uint32_t>(data[14]) << 16) | (static_cast<uint32_t>(data[15]) << 24);
            if (serial != 0) {
                result = QString::number(serial);
                break;
            }
        }
    }

    if (!result.isEmpty()) {
        cache.insert(connectorName, result);
        detail::edidMissCounter().remove(connectorName);
    } else {
        constexpr int maxRetries = 3;
        int& misses = detail::edidMissCounter()[connectorName];
        ++misses;
        if (misses >= maxRetries) {
            cache.insert(connectorName, result);
        }
    }
    return result;
}

/**
 * @brief Drop cached EDID serial data. Call on hotplug; pass an empty
 *        string to drop everything.
 */
inline void invalidateEdidCache(const QString& connectorName = QString())
{
    if (connectorName.isEmpty()) {
        detail::edidSerialCache().clear();
        detail::edidMissCounter().clear();
    } else {
        detail::edidSerialCache().remove(connectorName);
        detail::edidMissCounter().remove(connectorName);
    }
}

/**
 * @brief Coerce a possibly-hex serial ("0x0001C1A3") to decimal ("115107").
 *
 * KWin on Wayland exposes the EDID header serial as hex via
 * `QScreen::serialNumber()`. Both daemon and compositor must produce
 * byte-identical IDs, so we normalise to decimal.
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
 * @brief Assemble a base identifier from EDID-style fields.
 *
 * "manuf:model:serial" when serial is available, "manuf:model" when
 * only those are present, empty when no field is set (caller should
 * fall back to the connector name).
 */
inline QString buildBaseId(const QString& manufacturer, const QString& model, const QString& serial)
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
 * @brief Build a base ID from raw EDID fields with sysfs serial fallback.
 *
 * Tries `serialNumber` (after `normalizeHexSerial`) first, then sysfs.
 * Returns the connector name as a final fallback so the result is
 * always a non-empty stable string.
 */
inline QString buildScreenBaseId(const QString& manufacturer, const QString& model, const QString& serialNumber,
                                 const QString& connectorName)
{
    QString serial = normalizeHexSerial(serialNumber);
    if (serial.isEmpty()) {
        serial = readEdidHeaderSerial(connectorName);
    }
    QString baseId = buildBaseId(manufacturer, model, serial);
    return baseId.isEmpty() ? connectorName : baseId;
}

} // namespace ScreenId

} // namespace PhosphorIdentity
