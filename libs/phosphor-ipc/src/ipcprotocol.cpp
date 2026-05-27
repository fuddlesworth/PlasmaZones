// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIpc/IpcProtocol.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLatin1String>

namespace PhosphorIpc {

std::optional<Request> parseRequest(const QByteArray& line, QString* parseError)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
    if (err.error != QJsonParseError::NoError) {
        if (parseError) {
            *parseError = QStringLiteral("malformed JSON at offset %1: %2").arg(err.offset).arg(err.errorString());
        }
        return std::nullopt;
    }
    if (!doc.isObject()) {
        if (parseError) {
            *parseError = QStringLiteral("request root is not a JSON object");
        }
        return std::nullopt;
    }
    const QJsonObject obj = doc.object();

    Request req;
    req.type = obj.value(QLatin1String(Field::Type)).toString();
    if (req.type.isEmpty()) {
        if (parseError) {
            *parseError = QStringLiteral("missing or non-string 'type' field");
        }
        return std::nullopt;
    }
    // `id` is non-negative integer for client-correlated requests.
    // Server-initiated events have no id. JSON's lack of distinct
    // int/double types means we accept any numeric and cast to
    // qint64; non-numeric values become 0.
    const QJsonValue idValue = obj.value(QLatin1String(Field::Id));
    if (idValue.isDouble()) {
        req.id = static_cast<qint64>(idValue.toDouble());
    }
    req.target = obj.value(QLatin1String(Field::Target)).toString();
    req.fn = obj.value(QLatin1String(Field::Fn)).toString();
    req.signal = obj.value(QLatin1String(Field::Signal)).toString();
    const QJsonValue subIdValue = obj.value(QLatin1String(Field::SubscriptionId));
    if (subIdValue.isDouble()) {
        req.subscriptionId = static_cast<qint64>(subIdValue.toDouble());
    }
    // Args may be missing entirely (for no-arg calls) or an array.
    // Anything else is a malformed-request signal.
    const QJsonValue argsValue = obj.value(QLatin1String(Field::Args));
    if (argsValue.isArray()) {
        req.args = argsValue.toArray().toVariantList();
    } else if (!argsValue.isUndefined() && !argsValue.isNull()) {
        if (parseError) {
            *parseError = QStringLiteral("'args' must be an array if present");
        }
        return std::nullopt;
    }
    return req;
}

QJsonObject buildReply(qint64 id, const QJsonValue& result)
{
    QJsonObject obj;
    obj.insert(QLatin1String(Field::Type), QLatin1String(ResponseType::Reply));
    obj.insert(QLatin1String(Field::Id), static_cast<double>(id));
    obj.insert(QLatin1String(Field::Result), result);
    return obj;
}

QJsonObject buildEvent(qint64 subscriptionId, const QJsonValue& args)
{
    QJsonObject obj;
    obj.insert(QLatin1String(Field::Type), QLatin1String(ResponseType::Event));
    obj.insert(QLatin1String(Field::SubscriptionId), static_cast<double>(subscriptionId));
    obj.insert(QLatin1String(Field::Args), args);
    return obj;
}

QJsonObject buildError(qint64 id, const QString& code, const QString& message, const QJsonObject& detail)
{
    QJsonObject obj;
    obj.insert(QLatin1String(Field::Type), QLatin1String(ResponseType::Error));
    if (id != 0) {
        obj.insert(QLatin1String(Field::Id), static_cast<double>(id));
    }
    obj.insert(QLatin1String(Field::Code), code);
    obj.insert(QLatin1String(Field::Message), message);
    if (!detail.isEmpty()) {
        obj.insert(QLatin1String(Field::Detail), detail);
    }
    return obj;
}

QByteArray writeLine(const QJsonObject& obj)
{
    QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    return bytes;
}

} // namespace PhosphorIpc
