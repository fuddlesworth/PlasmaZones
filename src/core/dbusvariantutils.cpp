// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dbusvariantutils.h"
#include "logging.h"

#include <QDBusArgument>

namespace PlasmaZones {
namespace DBusVariantUtils {

QVariant convertDbusArgument(const QVariant& value)
{
    // Handle QDBusArgument wrapper - extract to plain types first
    if (value.canConvert<QDBusArgument>()) {
        const QDBusArgument arg = value.value<QDBusArgument>();
        switch (arg.currentType()) {
        case QDBusArgument::MapType: {
            // Extract the entire map at once, then recursively convert values
            QVariantMap map;
            arg >> map;
            QVariantMap result;
            for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
                result.insert(it.key(), convertDbusArgument(it.value()));
            }
            return result;
        }
        case QDBusArgument::ArrayType: {
            // Extract the entire list at once using operator>>, which is more
            // reliable than beginArray()/endArray() for nested structures
            QVariantList list;
            arg >> list;
            QVariantList result;
            result.reserve(list.size());
            for (const QVariant& item : list) {
                result.append(convertDbusArgument(item));
            }
            return result;
        }
        case QDBusArgument::StructureType: {
            // Handle D-Bus structures (less common, but can occur)
            QVariantList structData;
            arg >> structData;
            QVariantList result;
            result.reserve(structData.size());
            for (const QVariant& item : structData) {
                result.append(convertDbusArgument(item));
            }
            return result;
        }
        case QDBusArgument::BasicType:
        case QDBusArgument::VariantType: {
            // Basic types can be extracted directly
            QVariant extracted;
            arg >> extracted;
            return extracted;
        }
        default:
            // Unknown type - return as-is (may cause issues, but log for debugging)
            qCWarning(lcCore) << "Unhandled QDBusArgument type:" << arg.currentType();
            return value;
        }
    }

    // Handle QVariantList that might contain nested QDBusArgument objects
    if (value.typeId() == QMetaType::QVariantList) {
        const QVariantList list = value.toList();
        QVariantList result;
        result.reserve(list.size());
        for (const QVariant& item : list) {
            result.append(convertDbusArgument(item));
        }
        return result;
    }

    // Handle QVariantMap that might contain nested QDBusArgument objects
    if (value.typeId() == QMetaType::QVariantMap) {
        const QVariantMap map = value.toMap();
        QVariantMap result;
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            result.insert(it.key(), convertDbusArgument(it.value()));
        }
        return result;
    }

    // Plain types pass through unchanged
    return value;
}

} // namespace DBusVariantUtils
} // namespace PlasmaZones
