// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpudevicelist.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QVariantMap>

namespace PlasmaZones {
namespace GpuDeviceList {

namespace {

/// Read a sysfs attribute like "0x1002\n" and return the bare lowercase hex
/// digits, or an empty string when unreadable.
QString readSysfsHex(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QString value = QString::fromLatin1(f.readAll()).trimmed().toLower();
    if (value.startsWith(QLatin1String("0x"))) {
        value.remove(0, 2);
    }
    return value;
}

/// Well-known GPU vendor labels for when no pci.ids database is installed.
QString vendorFallbackName(const QString& vendorId)
{
    if (vendorId == QLatin1String("1002")) {
        return QStringLiteral("AMD");
    }
    if (vendorId == QLatin1String("8086")) {
        return QStringLiteral("Intel");
    }
    if (vendorId == QLatin1String("10de")) {
        return QStringLiteral("NVIDIA");
    }
    if (vendorId == QLatin1String("1af4")) {
        return QStringLiteral("virtio");
    }
    return QString();
}

/// Look up "vendorId:deviceId" in the hwdata pci.ids database. The format is
/// line-oriented: a vendor line is "<4 hex>  <name>" at column 0, its device
/// lines follow as "\t<4 hex>  <name>" until the next vendor line. Returns
/// the device name (preferred) or the vendor name, or empty when the
/// database is absent or has no match.
QString pciIdsLookup(const QString& vendorId, const QString& deviceId, bool& deviceMatched)
{
    deviceMatched = false;
    static const QStringList dbPaths = {
        QStringLiteral("/usr/share/hwdata/pci.ids"),
        QStringLiteral("/usr/share/misc/pci.ids"),
        QStringLiteral("/usr/share/pci.ids"),
    };
    for (const QString& path : dbPaths) {
        QFile db(path);
        if (!db.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        QString vendorName;
        bool inVendor = false;
        while (!db.atEnd()) {
            const QByteArray raw = db.readLine();
            if (raw.startsWith('#') || raw.trimmed().isEmpty()) {
                continue;
            }
            if (!raw.startsWith('\t')) {
                // Vendor line — entering or leaving the block we care about.
                if (inVendor) {
                    break; // Left the vendor block without a device match.
                }
                const QString line = QString::fromLatin1(raw).trimmed();
                if (line.startsWith(vendorId + QLatin1String("  "), Qt::CaseInsensitive)) {
                    inVendor = true;
                    vendorName = line.mid(vendorId.size()).trimmed();
                }
            } else if (inVendor && !raw.startsWith("\t\t")) {
                const QString line = QString::fromLatin1(raw).trimmed();
                if (line.startsWith(deviceId + QLatin1String("  "), Qt::CaseInsensitive)) {
                    deviceMatched = true;
                    return line.mid(deviceId.size()).trimmed();
                }
            }
        }
        if (!vendorName.isEmpty()) {
            return vendorName;
        }
    }
    return QString();
}

} // namespace

QVariantList enumerate()
{
    QVariantList result;
    QSet<QString> seen;

    const QDir drm(QStringLiteral("/sys/class/drm"));
    const QStringList nodes =
        drm.entryList(QStringList{QStringLiteral("renderD*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& node : nodes) {
        const QString deviceDir = drm.filePath(node) + QStringLiteral("/device");
        const QString vendorId = readSysfsHex(deviceDir + QStringLiteral("/vendor"));
        const QString deviceId = readSysfsHex(deviceDir + QStringLiteral("/device"));
        if (vendorId.isEmpty() || deviceId.isEmpty()) {
            continue;
        }
        const QString pair = vendorId + QLatin1Char(':') + deviceId;
        if (seen.contains(pair)) {
            continue;
        }
        seen.insert(pair);

        bool deviceMatched = false;
        QString name = pciIdsLookup(vendorId, deviceId, deviceMatched);
        // pci.ids names read like "TU104 [GeForce RTX 2080 Rev. A]" — the
        // chip code first, the marketing name users actually recognize in
        // brackets. Show the bracketed part, prefixed with the vendor when
        // the name doesn't already carry it.
        const qsizetype b1 = name.indexOf(QLatin1Char('['));
        const qsizetype b2 = name.lastIndexOf(QLatin1Char(']'));
        if (b1 >= 0 && b2 > b1) {
            name = name.mid(b1 + 1, b2 - b1 - 1).trimmed();
        }
        const QString vendorLabel = vendorFallbackName(vendorId);
        if (deviceMatched && !vendorLabel.isEmpty() && !name.contains(vendorLabel, Qt::CaseInsensitive)) {
            name = vendorLabel + QLatin1Char(' ') + name;
        }
        // Hardware names are not translatable strings, so plain formatting is
        // fine here. The raw PCI pair only appears when the device name is
        // unknown: a bare vendor label could otherwise repeat for two
        // different unidentified cards, and the pair keeps them apart.
        QString text;
        if (deviceMatched && !name.isEmpty()) {
            text = name;
        } else if (!name.isEmpty() || !vendorLabel.isEmpty()) {
            text = QStringLiteral("%1 (%2)").arg(name.isEmpty() ? vendorLabel : name, pair);
        } else {
            text = QStringLiteral("GPU %1").arg(pair);
        }
        QVariantMap entry;
        entry.insert(QStringLiteral("text"), text);
        entry.insert(QStringLiteral("value"), pair);
        result.append(entry);
    }
    return result;
}

} // namespace GpuDeviceList
} // namespace PlasmaZones
