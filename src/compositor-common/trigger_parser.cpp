// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "trigger_parser.h"

namespace PlasmaZones {
namespace TriggerParser {

QVector<ParsedTrigger> parseTriggers(const QVariant& triggerVariant, const QString& modifierFieldName,
                                     const QString& mouseButtonFieldName)
{
    QVariantList triggerList;
    if (triggerVariant.canConvert<QDBusArgument>()) {
        const QDBusArgument arg = triggerVariant.value<QDBusArgument>();
        arg.beginArray();
        while (!arg.atEnd()) {
            QVariant element;
            arg >> element;
            triggerList.append(element);
        }
        arg.endArray();
    } else {
        triggerList = triggerVariant.toList();
    }

    QVector<ParsedTrigger> result;
    result.reserve(triggerList.size());
    for (const auto& t : std::as_const(triggerList)) {
        QVariantMap map;
        if (t.canConvert<QDBusArgument>()) {
            const QDBusArgument elemArg = t.value<QDBusArgument>();
            elemArg >> map;
        } else {
            map = t.toMap();
        }
        ParsedTrigger pt;
        pt.modifier = map.value(modifierFieldName, 0).toInt();
        pt.mouseButton = map.value(mouseButtonFieldName, 0).toInt();
        result.append(pt);
    }
    return result;
}

} // namespace TriggerParser
} // namespace PlasmaZones
