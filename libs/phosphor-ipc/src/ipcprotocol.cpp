// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIpc/IpcProtocol.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLatin1String>
#include <QMetaType>
#include <QVariantList>
#include <QVariantMap>

namespace PhosphorIpc {

namespace {
// Cap on the request `args` array size. Realistic shell methods
// take ≤ ~10 positional args; 4096 is far above that ceiling and
// well under any QJsonArray operational limit. Without a cap a
// peer could send a multi-megabyte `args` array and force the
// router to materialise it before any validation runs.
constexpr int MaxArgsLength = 4096;
} // namespace

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
    // `id` correlates request to response. Absent → 0 (clients
    // that don't care about correlation can omit it, e.g. one-
    // shot CLI invocations). Present-but-non-numeric is a
    // malformed request, not a silent fallback to 0, otherwise
    // clients that send a bogus id type get error frames with
    // mysteriously-missing ids (id=0 collides with the omission
    // sentinel in buildError).
    const QJsonValue idValue = obj.value(QLatin1String(Field::Id));
    if (idValue.isDouble()) {
        req.id = static_cast<qint64>(idValue.toDouble());
    } else if (!idValue.isUndefined() && !idValue.isNull()) {
        if (parseError) {
            *parseError = QStringLiteral("'id' must be a number if present");
        }
        return std::nullopt;
    }
    req.target = obj.value(QLatin1String(Field::Target)).toString();
    req.fn = obj.value(QLatin1String(Field::Fn)).toString();
    req.signalName = obj.value(QLatin1String(Field::Signal)).toString();
    const QJsonValue subIdValue = obj.value(QLatin1String(Field::SubscriptionId));
    if (subIdValue.isDouble()) {
        req.subscriptionId = static_cast<qint64>(subIdValue.toDouble());
    } else if (!subIdValue.isUndefined() && !subIdValue.isNull()) {
        if (parseError) {
            *parseError = QStringLiteral("'subscriptionId' must be a number if present");
        }
        return std::nullopt;
    }
    // Args may be missing entirely (for no-arg calls) or an array.
    // Anything else is a malformed-request signal.
    const QJsonValue argsValue = obj.value(QLatin1String(Field::Args));
    if (argsValue.isArray()) {
        const QJsonArray argsArray = argsValue.toArray();
        if (argsArray.size() > MaxArgsLength) {
            if (parseError) {
                *parseError =
                    QStringLiteral("'args' length %1 exceeds limit of %2").arg(argsArray.size()).arg(MaxArgsLength);
            }
            return std::nullopt;
        }
        req.args = argsArray.toVariantList();
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

QJsonValue variantToJson(const QVariant& v)
{
    if (!v.isValid()) {
        return QJsonValue::Null;
    }
    switch (v.typeId()) {
    case QMetaType::Bool:
        return v.toBool();
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::LongLong:
        return static_cast<double>(v.toLongLong());
    case QMetaType::ULongLong:
        return static_cast<double>(v.toULongLong());
    case QMetaType::Double:
    case QMetaType::Float:
        return v.toDouble();
    case QMetaType::QString:
        return v.toString();
    case QMetaType::QStringList: {
        QJsonArray arr;
        for (const QString& s : v.toStringList()) {
            arr.append(s);
        }
        return arr;
    }
    case QMetaType::QVariantList: {
        QJsonArray arr;
        for (const QVariant& item : v.toList()) {
            arr.append(variantToJson(item));
        }
        return arr;
    }
    case QMetaType::QVariantMap: {
        QJsonObject obj;
        const QVariantMap map = v.toMap();
        // cbegin/cend (not begin/end) so the implicitly-shared
        // QVariantMap doesn't detach on each event broadcast in the
        // hot path.
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            obj.insert(it.key(), variantToJson(it.value()));
        }
        return obj;
    }
    case QMetaType::QJsonValue:
        return v.toJsonValue();
    case QMetaType::QJsonObject:
        return v.toJsonObject();
    case QMetaType::QJsonArray:
        return v.toJsonArray();
    default:
        // Unknown type, emit the string-shaped fallback so the
        // wire doesn't gain undocumented JSON shapes per metatype.
        return v.toString();
    }
}

} // namespace PhosphorIpc
