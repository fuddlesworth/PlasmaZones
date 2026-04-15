// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ShaderDbusQueries.h"
#include "../../core/constants.h"
#include "../../core/dbusvariantutils.h"
#include "../../core/shaderregistry.h"
#include "../../core/logging.h"
#include "DbusHelpers.h"

#include <QDBusInterface>
#include <QDBusReply>

namespace PlasmaZones {
namespace ShaderDbusQueries {

using DbusHelpers::createSettingsInterface;

bool queryShadersEnabled()
{
    QDBusInterface settingsIface = createSettingsInterface();

    if (!settingsIface.isValid()) {
        qCWarning(lcDbus) << "Shader query: daemon D-Bus interface unavailable";
        return false;
    }

    settingsIface.setTimeout(DBus::SyncCallTimeoutMs);
    QDBusReply<bool> reply = settingsIface.call(QStringLiteral("shadersEnabled"));
    return reply.isValid() && reply.value();
}

QVariantList queryAvailableShaders()
{
    QDBusInterface settingsIface = createSettingsInterface();

    if (!settingsIface.isValid()) {
        qCWarning(lcDbus) << "Shader query: daemon D-Bus interface unavailable";
        return QVariantList();
    }

    settingsIface.setTimeout(DBus::SyncCallTimeoutMs);
    QDBusReply<QVariantList> reply = settingsIface.call(QStringLiteral("availableShaders"));
    if (!reply.isValid()) {
        qCWarning(lcDbus) << "D-Bus availableShaders call failed:" << reply.error().message();
        return QVariantList();
    }

    QVariantList result;

    // D-Bus returns nested structures (QVariantMap, QVariantList) as QDBusArgument
    // Use DBusVariantUtils::convertDbusArgument to recursively convert them to proper Qt types
    for (const QVariant& item : reply.value()) {
        QVariant converted = DBusVariantUtils::convertDbusArgument(item);
        if (converted.typeId() == QMetaType::QVariantMap) {
            QVariantMap map = converted.toMap();
            // Validate that required fields exist
            if (map.contains(QLatin1String("id")) && map.contains(QLatin1String("name"))) {
                result.append(map);
            } else {
                qCWarning(lcDbus) << "Shader entry missing required fields (id/name):" << map;
            }
        } else {
            qCWarning(lcDbus) << "Unexpected shader list item type after conversion:" << converted.typeName();
        }
    }

    qCInfo(lcEditor) << "Loaded" << result.size() << "shaders";
    return result;
}

QVariantMap queryShaderInfo(const QString& shaderId)
{
    if (ShaderRegistry::isNoneShader(shaderId)) {
        return QVariantMap();
    }

    QDBusInterface settingsIface = createSettingsInterface();

    if (!settingsIface.isValid()) {
        return QVariantMap();
    }

    settingsIface.setTimeout(DBus::SyncCallTimeoutMs);
    QDBusReply<QVariantMap> reply = settingsIface.call(QStringLiteral("shaderInfo"), shaderId);
    if (reply.isValid()) {
        // D-Bus may return nested structures as QDBusArgument - convert recursively
        QVariant converted = DBusVariantUtils::convertDbusArgument(QVariant::fromValue(reply.value()));
        return converted.toMap();
    }

    qCWarning(lcDbus) << "D-Bus shaderInfo call failed:" << reply.error().message();
    return QVariantMap();
}

QVariantMap queryTranslateShaderParams(const QString& shaderId, const QVariantMap& params)
{
    if (ShaderRegistry::isNoneShader(shaderId)) {
        return QVariantMap();
    }

    QDBusInterface settingsIface = createSettingsInterface();

    if (!settingsIface.isValid()) {
        return QVariantMap();
    }

    // Filter out null/invalid QVariant values before D-Bus marshalling.
    // QML can produce std::nullptr_t variants (type 51) which are not
    // registered with D-Bus and cause a marshalling abort.
    QVariantMap safeParams;
    for (auto it = params.cbegin(); it != params.cend(); ++it) {
        if (it.value().isValid() && !it.value().isNull()) {
            safeParams.insert(it.key(), it.value());
        }
    }

    settingsIface.setTimeout(DBus::SyncCallTimeoutMs);
    QDBusReply<QVariantMap> reply = settingsIface.call(QStringLiteral("translateShaderParams"), shaderId, safeParams);
    if (reply.isValid()) {
        QVariant converted = DBusVariantUtils::convertDbusArgument(QVariant::fromValue(reply.value()));
        return converted.toMap();
    }

    qCWarning(lcDbus) << "D-Bus translateShaderParams call failed:" << reply.error().message();
    return QVariantMap();
}

} // namespace ShaderDbusQueries
} // namespace PlasmaZones
