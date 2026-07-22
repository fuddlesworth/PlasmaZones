// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ShaderDbusQueries.h"
#include "core/utils/dbusvariantutils.h"
#include "core/interfaces/shaderregistry.h"
#include "core/platform/logging.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <QDBusError>
#include <QDBusMessage>

namespace PlasmaZones {
namespace ShaderDbusQueries {

namespace {

QDBusMessage callSettings(const QString& method, const QVariantList& args = {})
{
    return PhosphorProtocol::ClientHelpers::syncCall(PhosphorProtocol::Service::Interface::Settings, method, args);
}

QString errorMessage(const QDBusMessage& reply)
{
    return reply.type() == QDBusMessage::ErrorMessage ? QDBusError(reply).message() : QString();
}

} // namespace

bool queryShadersEnabled()
{
    const QDBusMessage reply = callSettings(QStringLiteral("shadersEnabled"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        qCWarning(lcDbus) << "Shader query: shadersEnabled D-Bus call failed:" << errorMessage(reply);
        return false;
    }
    return reply.arguments().constFirst().toBool();
}

QVariantList queryAvailableShaders()
{
    const QDBusMessage reply = callSettings(QStringLiteral("availableShaders"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        qCWarning(lcDbus) << "D-Bus availableShaders call failed:" << errorMessage(reply);
        return QVariantList();
    }

    QVariantList result;

    // The reply carries a single D-Bus `av` argument (array of variant). Unlike
    // `a{sv}`, QtDBus does NOT auto-demarshal a bare `av` into a QVariantList —
    // it hands it back wrapped in a QDBusArgument, so calling `.toList()` on it
    // directly yields an empty list and every shader is silently dropped. Run
    // the top-level argument through convertDbusArgument first (which also
    // recursively converts the nested `a{sv}` entries), exactly as
    // queryShaderInfo() / queryTranslateShaderParams() already do.
    const QVariant converted = DBusVariantUtils::convertDbusArgument(reply.arguments().constFirst());
    const QVariantList payload = converted.toList();
    for (const QVariant& item : payload) {
        if (item.typeId() == QMetaType::QVariantMap) {
            QVariantMap map = item.toMap();
            // Validate that required fields exist
            if (map.contains(QLatin1String("id")) && map.contains(QLatin1String("name"))) {
                result.append(map);
            } else {
                qCWarning(lcDbus) << "Shader entry missing required fields (id/name):" << map;
            }
        } else {
            qCWarning(lcDbus) << "Unexpected shader list item type after conversion:" << item.typeName();
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

    const QDBusMessage reply = callSettings(QStringLiteral("shaderInfo"), {shaderId});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        qCWarning(lcDbus) << "D-Bus shaderInfo call failed:" << errorMessage(reply);
        return QVariantMap();
    }
    // D-Bus may return nested structures as QDBusArgument - convert recursively
    QVariant converted = DBusVariantUtils::convertDbusArgument(reply.arguments().constFirst());
    return converted.toMap();
}

QVariantMap queryTranslateShaderParams(const QString& shaderId, const QVariantMap& params)
{
    if (ShaderRegistry::isNoneShader(shaderId)) {
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

    const QDBusMessage reply = callSettings(QStringLiteral("translateShaderParams"), {shaderId, safeParams});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        qCWarning(lcDbus) << "D-Bus translateShaderParams call failed:" << errorMessage(reply);
        return QVariantMap();
    }
    QVariant converted = DBusVariantUtils::convertDbusArgument(reply.arguments().constFirst());
    return converted.toMap();
}

} // namespace ShaderDbusQueries
} // namespace PlasmaZones
