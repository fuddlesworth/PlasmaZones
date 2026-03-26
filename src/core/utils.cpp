// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "utils.h"
#include <QGuiApplication>
#include <QScreen>
#include <QDir>
#include <QFile>
#include <QHash>

namespace PlasmaZones {
namespace Utils {

// Shared cache for EDID header serial lookups (avoids sysfs I/O per call).
// Must only be called from the main (GUI) thread — no synchronization.
static QHash<QString, QString>& edidSerialCache()
{
    static QHash<QString, QString> s_cache;
    return s_cache;
}

// Retry counter for connectors where EDID read returned empty.
// After maxRetries attempts, the empty result is cached permanently
// to avoid unbounded sysfs I/O for virtual displays / embedded panels.
static QHash<QString, int>& edidMissCounter()
{
    static QHash<QString, int> s_counter;
    return s_counter;
}

QString readEdidHeaderSerial(const QString& connectorName)
{
    auto& cache = edidSerialCache();
    auto cacheIt = cache.constFind(connectorName);
    if (cacheIt != cache.constEnd()) {
        return *cacheIt;
    }

    QString result;

    // Find the sysfs EDID file: /sys/class/drm/card*-<connector>/edid
    QDir drmDir(QStringLiteral("/sys/class/drm"));
    if (drmDir.exists()) {
        const QStringList entries = drmDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& entry : entries) {
            // Match entries like "card0-DP-2", "card1-HDMI-A-1"
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
            // Validate EDID magic header: bytes 0-7 must be 00 FF FF FF FF FF FF 00
            const auto* data = reinterpret_cast<const uint8_t*>(header.constData());
            if (data[0] != 0x00 || data[1] != 0xFF || data[2] != 0xFF || data[3] != 0xFF || data[4] != 0xFF
                || data[5] != 0xFF || data[6] != 0xFF || data[7] != 0x00) {
                continue; // Not a valid EDID blob
            }
            // Bytes 12-15: serial number (little-endian uint32)
            uint32_t serial = data[12] | (static_cast<uint32_t>(data[13]) << 8)
                | (static_cast<uint32_t>(data[14]) << 16) | (static_cast<uint32_t>(data[15]) << 24);
            if (serial != 0) {
                result = QString::number(serial);
                break; // Found valid EDID with non-zero serial
            }
        }
    }

    if (!result.isEmpty()) {
        cache.insert(connectorName, result);
        edidMissCounter().remove(connectorName);
    } else {
        // Track failed reads. After 3 misses, cache the empty result permanently
        // to avoid unbounded sysfs I/O for virtual displays and embedded panels.
        // Boot-time races resolve within 1-2 retries; anything beyond 3 is genuinely
        // absent EDID data. invalidateEdidCache() resets both caches on hotplug.
        constexpr int maxRetries = 3;
        int& misses = edidMissCounter()[connectorName];
        ++misses;
        if (misses >= maxRetries) {
            cache.insert(connectorName, result); // Cache empty permanently
        }
    }
    return result;
}

void invalidateEdidCache(const QString& connectorName)
{
    if (connectorName.isEmpty()) {
        edidSerialCache().clear();
        edidMissCounter().clear();
    } else {
        edidSerialCache().remove(connectorName);
        edidMissCounter().remove(connectorName);
    }
}

// EDID-based identifier without duplicate disambiguation.
// Returns "manufacturer:model:serial" or fallback to connector name.
static QString screenBaseIdentifier(const QScreen* screen)
{
    if (!screen) {
        return QString();
    }

    const QString manufacturer = screen->manufacturer();
    const QString model = screen->model();

    // Prefer Qt's text serial descriptor (from EDID descriptor blocks)
    QString serial = screen->serialNumber();

    // KWin on Wayland exposes the EDID header serial (bytes 12-15) as hex via
    // QScreen::serialNumber() (e.g. "0x0001C1A3").  The effect side normalizes
    // this to decimal (e.g. "115107") so both sides produce identical IDs.
    // Apply the same normalization here.
    if (!serial.isEmpty() && serial.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        bool ok = false;
        uint32_t numericSerial = serial.toUInt(&ok, 16);
        if (ok && numericSerial != 0) {
            serial = QString::number(numericSerial);
        }
    }

    // Fallback: read the EDID header serial from sysfs (always present, what KDE shows)
    if (serial.isEmpty()) {
        serial = readEdidHeaderSerial(screen->name());
    }

    // Note: leading colons (":model:serial") are possible for screens with empty
    // manufacturer fields (some cheap/generic monitors). This is intentional —
    // the identifier is still unique and stable, and isConnectorName() correctly
    // classifies it as a screen ID (contains ':').
    if (!serial.isEmpty()) {
        return manufacturer + QLatin1Char(':') + model + QLatin1Char(':') + serial;
    }
    if (!manufacturer.isEmpty() || !model.isEmpty()) {
        return manufacturer + QLatin1Char(':') + model;
    }
    // Fallback: connector name (virtual displays, some embedded panels)
    return screen->name();
}

QString screenIdentifier(const QScreen* screen)
{
    if (!screen) {
        return QString();
    }

    const QString baseId = screenBaseIdentifier(screen);

    // Check if another connected screen produces the same base ID (identical monitors).
    // When duplicates exist, append "/ConnectorName" to disambiguate.
    // This mirrors KWin's OutputConfigurationStore strategy: EDID primary, connector fallback.
    for (const QScreen* other : QGuiApplication::screens()) {
        if (other != screen && screenBaseIdentifier(other) == baseId) {
            return baseId + QLatin1Char('/') + screen->name();
        }
    }

    return baseId;
}

QString screenIdForName(const QString& connectorName)
{
    if (connectorName.isEmpty()) {
        return connectorName;
    }
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() == connectorName) {
            return screenIdentifier(screen);
        }
    }
    return connectorName;
}

QString screenNameForId(const QString& screenId)
{
    if (screenId.isEmpty()) {
        return QString();
    }
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screenIdentifier(screen) == screenId) {
            return screen->name();
        }
    }
    // Fallback: if the ID has a "/connector" suffix, return the connector name
    // only if a screen with that name actually exists
    int slashPos = screenId.lastIndexOf(QLatin1Char('/'));
    if (slashPos > 0) {
        const QString connector = screenId.mid(slashPos + 1);
        for (QScreen* screen : QGuiApplication::screens()) {
            if (screen->name() == connector) {
                return connector;
            }
        }
    }
    return QString();
}

bool isConnectorName(const QString& identifier)
{
    return !identifier.isEmpty() && !identifier.contains(QLatin1Char(':'));
}

QScreen* findScreenByIdOrName(const QString& identifier)
{
    if (identifier.isEmpty()) {
        return QGuiApplication::primaryScreen();
    }
    // Fast path: try connector name match first
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() == identifier) {
            return screen;
        }
    }
    // Try exact screen ID match (only if it looks like a screen ID)
    if (identifier.contains(QLatin1Char(':'))) {
        for (QScreen* screen : QGuiApplication::screens()) {
            if (screenIdentifier(screen) == identifier) {
                return screen;
            }
        }
    }
    // Fallback for connector-disambiguated IDs ("Manufacturer:Model:Serial/DP-3"):
    // if the identifier contains '/', try matching the connector suffix directly,
    // then fall back to the base ID (for when a previously-duplicate monitor is
    // now the only one connected, so its ID no longer has the suffix).
    int slashPos = identifier.lastIndexOf(QLatin1Char('/'));
    if (slashPos > 0) {
        const QString connectorPart = identifier.mid(slashPos + 1);
        const QString basePart = identifier.left(slashPos);
        // Try connector name from the suffix, but verify the EDID base matches
        // (prevents returning a different monitor that was plugged into the same port)
        for (QScreen* screen : QGuiApplication::screens()) {
            if (screen->name() == connectorPart && screenBaseIdentifier(screen) == basePart) {
                return screen;
            }
        }
        // Try base ID match (monitor is now unique, no suffix needed)
        for (QScreen* screen : QGuiApplication::screens()) {
            if (screenIdentifier(screen) == basePart) {
                return screen;
            }
        }
    }
    // Reverse fallback: saved config has base ID without suffix, but currently
    // connected monitors are duplicates (so screenIdentifier adds suffix).
    // Match by base part of the current screen IDs.
    if (identifier.contains(QLatin1Char(':')) && !identifier.contains(QLatin1Char('/'))) {
        for (QScreen* screen : QGuiApplication::screens()) {
            if (screenBaseIdentifier(screen) == identifier) {
                return screen; // returns first match — acceptable for legacy config
            }
        }
    }
    return nullptr;
}

bool screensMatch(const QString& a, const QString& b)
{
    if (a == b) {
        return true; // fast path: identical strings
    }
    if (a.isEmpty() || b.isEmpty()) {
        return false;
    }
    // Resolve both to QScreen* — handles connector names and screen IDs transparently
    QScreen* sa = findScreenByIdOrName(a);
    QScreen* sb = findScreenByIdOrName(b);
    return sa && sb && sa == sb;
}

void warnDuplicateScreenIds()
{
    QHash<QString, QStringList> idToConnectors;
    for (QScreen* screen : QGuiApplication::screens()) {
        QString id = screenBaseIdentifier(screen);
        idToConnectors[id].append(screen->name());
    }
    for (auto it = idToConnectors.constBegin(); it != idToConnectors.constEnd(); ++it) {
        if (it.value().size() > 1) {
            qInfo("PlasmaZones: identical monitors detected for EDID ID \"%s\" (connectors: %s). "
                  "Using connector-disambiguated IDs for independent layout assignments.",
                  qPrintable(it.key()), qPrintable(it.value().join(QStringLiteral(", "))));
        }
    }
}

} // namespace Utils
} // namespace PlasmaZones
