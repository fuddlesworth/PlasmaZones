// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rulepickeroptions.h"

#include "core/platform/logging.h"
#include "phosphor_i18n.h"

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QVariantMap>

#include <algorithm>

namespace PlasmaZones::RulePickerOptions {

QVariantList zoneMatchOptions(const QVector<PhosphorZones::Layout*>& layouts)
{
    QVector<const PhosphorZones::Layout*> ordered;
    ordered.reserve(layouts.size());
    for (const PhosphorZones::Layout* layout : layouts) {
        if (layout) {
            ordered.append(layout);
        }
    }
    // The layout pickers' order (lowercased name), stable so same-named
    // layouts keep the registry's order and with it their "(2)" suffixes.
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const PhosphorZones::Layout* a, const PhosphorZones::Layout* b) {
                         return a->name().trimmed().toLower() < b->name().trimmed().toLower();
                     });

    QVariantList options;
    QHash<QString, int> seenLayoutNames;
    for (int layoutOrder = 0; layoutOrder < ordered.size(); ++layoutOrder) {
        const PhosphorZones::Layout* layout = ordered.at(layoutOrder);
        const QString name = layout->name().trimmed();
        const QString baseName = name.isEmpty() ? PhosphorI18n::tr("Unnamed layout") : name;
        const int occurrence = ++seenLayoutNames[baseName.toLower()];
        const QString layoutName =
            occurrence > 1 ? PhosphorI18n::tr("%1 (%2)").arg(baseName).arg(occurrence) : baseName;

        QVector<const PhosphorZones::Zone*> zones;
        for (const PhosphorZones::Zone* zone : layout->zones()) {
            if (zone) {
                zones.append(zone);
            }
        }
        std::stable_sort(zones.begin(), zones.end(), [](const PhosphorZones::Zone* a, const PhosphorZones::Zone* b) {
            return a->zoneNumber() < b->zoneNumber();
        });
        for (const PhosphorZones::Zone* zone : std::as_const(zones)) {
            const QString ownName = zone->name().trimmed();
            const QString zoneName = ownName.isEmpty() ? PhosphorI18n::tr("Zone %1").arg(zone->zoneNumber()) : ownName;
            QVariantMap option;
            option[QStringLiteral("id")] = zone->id().toString();
            option[QStringLiteral("displayName")] = PhosphorI18n::tr("%1 — %2").arg(layoutName, zoneName);
            option[QStringLiteral("zoneName")] = zoneName;
            option[QStringLiteral("layoutName")] = layoutName;
            option[QStringLiteral("layoutOrder")] = layoutOrder;
            options.append(option);
        }
    }
    return options;
}

QVariantList runningWindows(const QString& json)
{
    if (json.isEmpty()) {
        return {};
    }
    // A signal payload rather than a method reply, so DaemonDBus::replyJsonArray
    // does not fit, but it warns for the same reason: the caller sees an empty
    // list either way, and without a log line "no windows are open" and "the
    // effect sent something unreadable" are the same state.
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcCore) << "RulePickerOptions::runningWindows: payload is not valid JSON:"
                          << parseError.errorString();
        return {};
    }
    if (!doc.isArray()) {
        qCWarning(lcCore) << "RulePickerOptions::runningWindows: payload JSON is not an array";
        return {};
    }

    QVariantList result;
    const QJsonArray array = doc.array();
    result.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        const QString windowClass = obj[QLatin1String("windowClass")].toString();
        QVariantMap item;
        item[QStringLiteral("windowClass")] = windowClass;
        item[QStringLiteral("rawWindowClass")] = obj.contains(QLatin1String("rawWindowClass"))
            ? obj[QLatin1String("rawWindowClass")].toString()
            : windowClass;
        item[QStringLiteral("appName")] = obj[QLatin1String("appName")].toString();
        item[QStringLiteral("caption")] = obj[QLatin1String("caption")].toString();
        // An effect that sends no desktopFile leaves it empty, and the picker's
        // DesktopFile mode greys the row out.
        item[QStringLiteral("desktopFile")] = obj[QLatin1String("desktopFile")].toString();
        result.append(item);
    }
    return result;
}

} // namespace PlasmaZones::RulePickerOptions
