// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "rulepickeroptions.h"

#include "core/platform/logging.h"
#include "phosphor_i18n.h"

#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>

namespace PlasmaZones::RulePickerOptions {

namespace {

/// Hands out the labels of one list (the layouts, or the zones of one layout)
/// so no two read alike. Labels are compared ignoring case and with "/" read
/// as U+2215, the slash the zone picker substitutes so a name cannot split its
/// submenu.
///
/// A name the user typed keeps its plain label: every typed name in the list
/// is reserved before any label is handed out, so neither a fallback ("Zone 3"
/// for an unnamed zone) nor a suffix ever takes one, whichever item comes
/// first. Only a repeat of a typed name, or a fallback a typed name already
/// holds, gets a "(N)" suffix, the first N from 2 up that is free.
class LabelSet
{
public:
    explicit LabelSet(const QStringList& typedNames)
    {
        for (const QString& name : typedNames) {
            if (!name.isEmpty()) {
                m_reserved.insert(keyOf(name));
            }
        }
    }

    /// The label of an item named @p typedName, or, when that is empty,
    /// labelled @p fallback.
    QString take(const QString& typedName, const QString& fallback)
    {
        const bool typed = !typedName.isEmpty();
        const QString base = typed ? typedName : fallback;
        // A typed name's first holder takes it as is; a fallback, and any
        // suffixed label, must also stay clear of every typed name.
        bool free = typed ? !m_used.contains(keyOf(base)) : !isTaken(base);
        QString label = base;
        for (int n = 2; !free; ++n) {
            // Single-pass arg: a name holding "%2" must not be substituted again.
            label = PhosphorI18n::tr("%1 (%2)", "a name, then a number telling it apart from others of the same name")
                        .arg(base, QString::number(n));
            free = !isTaken(label);
        }
        m_used.insert(keyOf(label));
        return label;
    }

private:
    static QString keyOf(QString label)
    {
        return label.toLower().replace(QLatin1Char('/'), QChar(0x2215));
    }

    bool isTaken(const QString& label) const
    {
        const QString key = keyOf(label);
        return m_used.contains(key) || m_reserved.contains(key);
    }

    QSet<QString> m_reserved;
    QSet<QString> m_used;
};

} // namespace

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

    QStringList typedLayoutNames;
    for (const PhosphorZones::Layout* layout : std::as_const(ordered)) {
        typedLayoutNames.append(layout->name().trimmed());
    }
    LabelSet layoutLabels(typedLayoutNames);

    QVariantList options;
    for (int layoutOrder = 0; layoutOrder < ordered.size(); ++layoutOrder) {
        const PhosphorZones::Layout* layout = ordered.at(layoutOrder);
        const QString layoutName =
            layoutLabels.take(typedLayoutNames.at(layoutOrder), PhosphorI18n::tr("Unnamed layout"));

        QVector<const PhosphorZones::Zone*> zones;
        QStringList typedZoneNames;
        for (const PhosphorZones::Zone* zone : layout->zones()) {
            if (zone) {
                zones.append(zone);
                typedZoneNames.append(zone->name().trimmed());
            }
        }
        std::stable_sort(zones.begin(), zones.end(), [](const PhosphorZones::Zone* a, const PhosphorZones::Zone* b) {
            return a->zoneNumber() < b->zoneNumber();
        });
        LabelSet zoneLabels(typedZoneNames);
        for (const PhosphorZones::Zone* zone : std::as_const(zones)) {
            const QString zoneName =
                zoneLabels.take(zone->name().trimmed(), PhosphorI18n::tr("Zone %1").arg(zone->zoneNumber()));
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
