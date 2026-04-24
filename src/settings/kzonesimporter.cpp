// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kzonesimporter.h"

#include "dbusutils.h"
#include "../pz_i18n.h"

#include <PhosphorZones/ZoneJsonKeys.h>

#include <QDBusMessage>
#include <QFile>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLatin1String>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringLiteral>
#include <QUuid>
#include <QtGlobal>

namespace PlasmaZones::KZonesImporter {

namespace {
constexpr auto kKwinrcGroup = "Script-kzones";
constexpr auto kLayoutsJsonKey = "layoutsJson";

QString kwinrcPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kwinrc");
}
} // namespace

bool hasKZonesConfig()
{
    QSettings kwinrc(kwinrcPath(), QSettings::IniFormat);
    kwinrc.beginGroup(QLatin1String(kKwinrcGroup));
    const bool has = kwinrc.contains(QLatin1String(kLayoutsJsonKey))
        && !kwinrc.value(QLatin1String(kLayoutsJsonKey)).toString().trimmed().isEmpty();
    kwinrc.endGroup();
    return has;
}

ImportResult importFromKwinrc()
{
    QSettings kwinrc(kwinrcPath(), QSettings::IniFormat);
    kwinrc.beginGroup(QLatin1String(kKwinrcGroup));
    const QString jsonStr = kwinrc.value(QLatin1String(kLayoutsJsonKey)).toString();
    kwinrc.endGroup();

    if (jsonStr.isEmpty()) {
        return {0, PzI18n::tr("No KZones configuration found in kwinrc"), QString()};
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        return {0, PzI18n::tr("Failed to parse KZones layoutsJson: %1").arg(parseError.errorString()), QString()};
    }

    ImportResult result = importLayouts(doc.array());
    if (result.imported > 0) {
        result.message = PzI18n::tr("Imported %n layout(s) from KZones", "", result.imported);
    } else if (result.message.isEmpty()) {
        result.message = PzI18n::tr("No layouts found in KZones configuration");
    }
    return result;
}

ImportResult importFromFile(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return {0, PzI18n::tr("No file path specified"), QString()};
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {0, PzI18n::tr("Could not open file: %1").arg(filePath), QString()};
    }

    QByteArray data = file.readAll();
    file.close();
    // Strip UTF-8 BOM if present (common in Windows-edited files).
    if (data.startsWith("\xEF\xBB\xBF")) {
        data.remove(0, 3);
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return {0, PzI18n::tr("Failed to parse KZones JSON: %1").arg(parseError.errorString()), QString()};
    }

    QJsonArray array;
    if (doc.isArray()) {
        array = doc.array();
    } else if (doc.isObject()) {
        // Single layout object — wrap in array.
        array.append(doc.object());
    } else {
        return {0, PzI18n::tr("KZones file does not contain a JSON array or object"), QString()};
    }

    ImportResult result = importLayouts(array);
    if (result.imported > 0) {
        result.message = PzI18n::tr("Imported %n layout(s) from KZones file", "", result.imported);
    } else if (result.message.isEmpty()) {
        result.message = PzI18n::tr("No valid layouts found in file");
    }
    return result;
}

ImportResult importLayouts(const QJsonArray& kzonesArray)
{
    ImportResult result;

    for (const QJsonValue& layoutVal : kzonesArray) {
        if (!layoutVal.isObject()) {
            continue;
        }

        const QJsonObject kzLayout = layoutVal.toObject();

        // Build PlasmaZones layout JSON.
        QJsonObject pzLayout;
        pzLayout[QLatin1String(::PhosphorZones::ZoneJsonKeys::Id)] = QUuid::createUuid().toString(QUuid::WithBraces);
        pzLayout[QLatin1String(::PhosphorZones::ZoneJsonKeys::Name)] =
            kzLayout[QStringLiteral("name")].toString(QStringLiteral("Imported Layout"));
        pzLayout[QLatin1String(::PhosphorZones::ZoneJsonKeys::Description)] = QStringLiteral("Imported from KZones");
        pzLayout[QLatin1String(::PhosphorZones::ZoneJsonKeys::IsBuiltIn)] = false;
        pzLayout[QLatin1String(::PhosphorZones::ZoneJsonKeys::ShowZoneNumbers)] = true;

        const int padding = kzLayout[QStringLiteral("padding")].toInt(0);
        if (padding > 0) {
            pzLayout[QLatin1String(::PhosphorZones::ZoneJsonKeys::ZonePadding)] = padding;
        }

        // Convert zones — skip layouts with no zones.
        const QJsonArray kzZones = kzLayout[QStringLiteral("zones")].toArray();
        if (kzZones.isEmpty()) {
            continue;
        }

        QJsonArray pzZones;
        QJsonArray appRules;

        for (int i = 0; i < kzZones.size(); ++i) {
            const QJsonObject kzZone = kzZones[i].toObject();

            // Convert 0-100 percentage to 0.0-1.0, clamped to valid range.
            const double x = qBound(0.0, kzZone[QStringLiteral("x")].toDouble(0) / 100.0, 1.0);
            const double y = qBound(0.0, kzZone[QStringLiteral("y")].toDouble(0) / 100.0, 1.0);
            const double w = qBound(0.0, kzZone[QStringLiteral("width")].toDouble(50) / 100.0, 1.0);
            const double h = qBound(0.0, kzZone[QStringLiteral("height")].toDouble(100) / 100.0, 1.0);

            // Skip zero-area zones.
            if (w <= 0.0 || h <= 0.0) {
                continue;
            }

            // Use contiguous numbering (skipped zones don't leave gaps).
            const int zoneNum = pzZones.size() + 1;

            QJsonObject pzZone;
            pzZone[QLatin1String(::PhosphorZones::ZoneJsonKeys::Id)] = QUuid::createUuid().toString(QUuid::WithBraces);
            pzZone[QLatin1String(::PhosphorZones::ZoneJsonKeys::ZoneNumber)] = zoneNum;
            pzZone[QLatin1String(::PhosphorZones::ZoneJsonKeys::Name)] = QStringLiteral("Zone %1").arg(zoneNum);

            QJsonObject relGeo;
            relGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::X)] = x;
            relGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::Y)] = y;
            relGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::Width)] = w;
            relGeo[QLatin1String(::PhosphorZones::ZoneJsonKeys::Height)] = h;
            pzZone[QLatin1String(::PhosphorZones::ZoneJsonKeys::RelativeGeometry)] = relGeo;

            pzZones.append(pzZone);

            // Collect per-zone applications into layout-level appRules.
            const QJsonArray apps = kzZone[QStringLiteral("applications")].toArray();
            for (const QJsonValue& appVal : apps) {
                const QString appClass = appVal.toString().trimmed();
                if (appClass.isEmpty()) {
                    continue;
                }
                QJsonObject rule;
                rule[QLatin1String(::PhosphorZones::ZoneJsonKeys::Pattern)] = appClass;
                rule[QLatin1String(::PhosphorZones::ZoneJsonKeys::ZoneNumber)] = zoneNum;
                appRules.append(rule);
            }
        }

        pzLayout[QLatin1String(::PhosphorZones::ZoneJsonKeys::Zones)] = pzZones;
        if (!appRules.isEmpty()) {
            pzLayout[QLatin1String(::PhosphorZones::ZoneJsonKeys::AppRules)] = appRules;
        }

        // Send to daemon via createLayoutFromJson D-Bus method.
        const QString layoutJson = QString::fromUtf8(QJsonDocument(pzLayout).toJson(QJsonDocument::Compact));
        const QDBusMessage reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                                          QStringLiteral("createLayoutFromJson"), {layoutJson});

        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            const QString newId = reply.arguments().first().toString();
            if (!newId.isEmpty()) {
                ++result.imported;
                if (result.pendingSelectLayoutId.isEmpty()) {
                    result.pendingSelectLayoutId = newId;
                }
            }
        }
    }

    return result;
}

} // namespace PlasmaZones::KZonesImporter
