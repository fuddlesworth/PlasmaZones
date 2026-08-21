// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpudevicelist.h"

#include "core/platform/logging.h"
#include "phosphor_i18n.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QPair>
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

struct PciName
{
    QString name; ///< Device name when deviceMatched, else the vendor name (or empty).
    bool deviceMatched = false;
};

/// Resolve every wanted "vendor:device" pair against the hwdata pci.ids
/// database in a SINGLE pass. The format is line-oriented: a vendor line is
/// "<4 hex>  <name>" at column 0, its device lines follow as
/// "\t<4 hex>  <name>" until the next vendor line. One pass for all pairs —
/// the database is ~1.5 MB and re-scanning it per GPU was the dominant cost
/// of building this list.
QHash<QString, PciName> pciIdsLookupAll(const QList<QPair<QString, QString>>& wanted)
{
    QHash<QString, PciName> results;
    if (wanted.isEmpty()) {
        return results;
    }
    QSet<QString> wantedVendors;
    for (const auto& pair : wanted) {
        wantedVendors.insert(pair.first);
    }

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
        QString currentVendorId;
        QString currentVendorName;
        while (!db.atEnd()) {
            const QByteArray raw = db.readLine();
            // Cheap skip without allocating a trimmed copy: comments and
            // blank lines start with '#', '\n' or '\r'.
            if (raw.isEmpty() || raw.at(0) == '#' || raw.at(0) == '\n' || raw.at(0) == '\r') {
                continue;
            }
            if (raw.at(0) != '\t') {
                // Vendor line. Only decode it when its 4-hex id is wanted.
                const QString line = QString::fromLatin1(raw).trimmed();
                currentVendorId.clear();
                currentVendorName.clear();
                for (const QString& vendorId : wantedVendors) {
                    if (line.startsWith(vendorId + QLatin1String("  "), Qt::CaseInsensitive)) {
                        currentVendorId = vendorId;
                        currentVendorName = line.mid(vendorId.size()).trimmed();
                        break;
                    }
                }
                if (!currentVendorId.isEmpty()) {
                    // Seed the vendor-name fallback for every wanted pair of
                    // this vendor; a later device-line match overwrites it.
                    // Done once here on the vendor line, not per device line.
                    for (const auto& pair : wanted) {
                        if (pair.first != currentVendorId) {
                            continue;
                        }
                        PciName& entry = results[pair.first + QLatin1Char(':') + pair.second];
                        if (!entry.deviceMatched && entry.name.isEmpty()) {
                            entry.name = currentVendorName;
                        }
                    }
                }
            } else if (!currentVendorId.isEmpty() && !raw.startsWith("\t\t")) {
                const QString line = QString::fromLatin1(raw).trimmed();
                for (const auto& pair : wanted) {
                    if (pair.first != currentVendorId) {
                        continue;
                    }
                    if (line.startsWith(pair.second + QLatin1String("  "), Qt::CaseInsensitive)) {
                        PciName& entry = results[pair.first + QLatin1Char(':') + pair.second];
                        entry.name = line.mid(pair.second.size()).trimmed();
                        entry.deviceMatched = true;
                    }
                }
            }
        }
        // Try the next database path only when this one resolved nothing at
        // all. Deliberately all-or-nothing across pairs: a machine with two
        // databases of differing completeness keeps the first one's answers
        // rather than mixing sources per pair.
        bool anyResolved = false;
        for (const PciName& entry : results) {
            if (!entry.name.isEmpty()) {
                anyResolved = true;
                break;
            }
        }
        if (anyResolved) {
            break;
        }
        results.clear();
    }
    return results;
}

} // namespace

QVariantList enumerate()
{
    struct Node
    {
        QString vendorId;
        QString deviceId;
        QString pair;
    };
    QList<Node> gpus;
    QSet<QString> seen;

    const QDir drm(QStringLiteral("/sys/class/drm"));
    const QStringList nodes =
        drm.entryList(QStringList{QStringLiteral("renderD*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& node : nodes) {
        const QString deviceDir = drm.filePath(node) + QStringLiteral("/device");
        const QString vendorId = readSysfsHex(deviceDir + QStringLiteral("/vendor"));
        const QString deviceId = readSysfsHex(deviceDir + QStringLiteral("/device"));
        if (vendorId.isEmpty() || deviceId.isEmpty()) {
            // Non-PCI GPU or unreadable attributes — the device cannot be
            // expressed as a vendor:device pin, so it is unpickable. Log it
            // so the omission is diagnosable from a log rather than by
            // reading sysfs by hand.
            qCDebug(lcCore) << "GpuDeviceList: skipping" << node << "— no readable PCI vendor/device attributes";
            continue;
        }
        const QString pair = vendorId + QLatin1Char(':') + deviceId;
        if (seen.contains(pair)) {
            continue;
        }
        seen.insert(pair);
        gpus.append(Node{vendorId, deviceId, pair});
    }

    QList<QPair<QString, QString>> wanted;
    wanted.reserve(gpus.size());
    for (const Node& gpu : gpus) {
        wanted.append({gpu.vendorId, gpu.deviceId});
    }
    const QHash<QString, PciName> names = pciIdsLookupAll(wanted);

    QVariantList result;
    for (const Node& gpu : gpus) {
        const PciName resolved = names.value(gpu.pair);
        QString name = resolved.name;
        const QString vendorLabel = vendorFallbackName(gpu.vendorId);
        if (resolved.deviceMatched) {
            // pci.ids DEVICE names read like "TU104 [GeForce RTX 2080 Rev. A]"
            // — the chip code first, the marketing name users actually
            // recognize in brackets. Show the bracketed part, prefixed with
            // the vendor when the name doesn't already carry it. Vendor names
            // keep their own bracket suffix ("... [AMD/ATI]") untouched: the
            // transform is for device names only.
            const qsizetype b1 = name.indexOf(QLatin1Char('['));
            const qsizetype b2 = name.lastIndexOf(QLatin1Char(']'));
            if (b1 >= 0 && b2 > b1) {
                name = name.mid(b1 + 1, b2 - b1 - 1).trimmed();
            }
            if (!vendorLabel.isEmpty() && !name.contains(vendorLabel, Qt::CaseInsensitive)) {
                name = vendorLabel + QLatin1Char(' ') + name;
            }
        } else if (!vendorLabel.isEmpty()) {
            // Prefer the curated short label ("AMD") over the database's
            // long vendor string ("Advanced Micro Devices, Inc. [AMD/ATI]").
            name = vendorLabel;
        }
        // Hardware names are not translatable strings, so plain formatting
        // covers the named cases. The raw PCI pair only appears when the
        // device name is unknown: a bare vendor label could otherwise repeat
        // for two different unidentified cards, and the pair keeps them
        // apart. The last-resort label leads with a translated "GPU" — the
        // one English word in these labels.
        QString text;
        if (resolved.deviceMatched && !name.isEmpty()) {
            text = name;
        } else if (!name.isEmpty()) {
            text = QStringLiteral("%1 (%2)").arg(name, gpu.pair);
        } else {
            text = PhosphorI18n::tr("GPU %1").arg(gpu.pair);
        }
        QVariantMap entry;
        entry.insert(QStringLiteral("text"), text);
        entry.insert(QStringLiteral("value"), gpu.pair);
        result.append(entry);
    }
    return result;
}

} // namespace GpuDeviceList
} // namespace PlasmaZones
