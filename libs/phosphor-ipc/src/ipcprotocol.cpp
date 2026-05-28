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

#include <cmath>
#include <limits>

namespace PhosphorIpc {

namespace {
// Cap on the request `args` array size. Realistic shell methods
// take ≤ ~10 positional args; 4096 is far above that ceiling and
// well under any QJsonArray operational limit. Without a cap a
// peer could send a multi-megabyte `args` array and force the
// router to materialise it before any validation runs. Declared
// as qsizetype to match QJsonArray::size()'s return type without
// triggering -Wsign-compare on the bounds check.
constexpr qsizetype MaxArgsLength = 4096;

// Parse a JSON number as an integral qint64. JSON numbers are
// doubles; we reject fractional values and out-of-range magnitudes
// explicitly so the protocol's "id is an integer" contract is
// honored at the parser level. Returns std::nullopt on rejection;
// the caller produces the MALFORMED_REQUEST error frame.
//
// Precision boundary: doubles cover integer values exactly up to
// 2^53. Past that, the representable values are spaced 2 apart
// (then 4, etc.). qint64's positive range extends to 2^63 - 1, but
// that exact integer is NOT representable as a double — the closest
// representable value is 2^63 itself. So the practical accepted
// range is [-2^63, 2^63 - 1024] (the largest exact double below
// 2^63 is 9223372036854774784 = 2^63 - 1024), even though qint64
// formally extends to 2^63 - 1. Negative bound is exact:
// -2^63 = INT64_MIN is exactly representable as a double.
std::optional<qint64> parseIntegralJsonNumber(double d)
{
    if (!std::isfinite(d)) {
        return std::nullopt;
    }
    // 2^63 (Int64MaxExclusiveD) IS exactly representable as a
    // double and equals the rounded value of INT64_MAX; reject it
    // and anything above as "not strictly less than qint64's
    // positive overflow boundary". INT64_MIN is exact as a double
    // (single-bit mantissa at 2^63 with negative sign) so the
    // static_cast lowers without precision loss; using the
    // numeric_limits value rather than a literal sidesteps the
    // "integer literal too large to be represented in any integer
    // type" warning some compilers emit for `-9223372036854775808`.
    constexpr double Int64MinD = static_cast<double>(std::numeric_limits<qint64>::min());
    constexpr double Int64MaxExclusiveD = 9223372036854775808.0;
    if (d < Int64MinD || d >= Int64MaxExclusiveD) {
        return std::nullopt;
    }
    if (std::trunc(d) != d) {
        return std::nullopt;
    }
    return static_cast<qint64>(d);
}
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
        const auto parsed = parseIntegralJsonNumber(idValue.toDouble());
        if (!parsed) {
            if (parseError) {
                *parseError = QStringLiteral("'id' must be a finite integer within JSON-double-precision int64 range");
            }
            return std::nullopt;
        }
        req.id = *parsed;
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
        const auto parsed = parseIntegralJsonNumber(subIdValue.toDouble());
        if (!parsed) {
            if (parseError) {
                *parseError = QStringLiteral(
                    "'subscriptionId' must be a finite integer within JSON-double-precision int64 range");
            }
            return std::nullopt;
        }
        req.subscriptionId = *parsed;
    } else if (!subIdValue.isUndefined() && !subIdValue.isNull()) {
        if (parseError) {
            *parseError = QStringLiteral("'subscriptionId' must be a number if present");
        }
        return std::nullopt;
    }
    // Per-type required-field validation. The router's dispatchers
    // would reject most of these with the correct error code at
    // runtime, but validating at parse-time surfaces them as
    // MALFORMED_REQUEST (the accurate shape) instead of e.g.
    // NO_SUCH_TARGET for an empty target string.
    if ((req.type == QLatin1String(RequestType::Call) || req.type == QLatin1String(RequestType::Schema)
         || req.type == QLatin1String(RequestType::Subscribe))
        && req.target.isEmpty()) {
        if (parseError) {
            *parseError = QStringLiteral("'%1' requires a non-empty 'target'").arg(req.type);
        }
        return std::nullopt;
    }
    if (req.type == QLatin1String(RequestType::Call) && req.fn.isEmpty()) {
        if (parseError) {
            *parseError = QStringLiteral("'call' requires a non-empty 'fn'");
        }
        return std::nullopt;
    }
    if (req.type == QLatin1String(RequestType::Subscribe) && req.signalName.isEmpty()) {
        if (parseError) {
            *parseError = QStringLiteral("'subscribe' requires a non-empty 'signal'");
        }
        return std::nullopt;
    }
    // Unsubscribe requires a non-zero `subscriptionId`. id=0 is the
    // protocol's "absent" sentinel; without this check an unsubscribe
    // with a missing field would parse successfully and reach the
    // dispatcher, which would then report NO_SUCH_SUBSCRIPTION for
    // "unknown subscriptionId 0" — accurate but the actual fault is
    // a malformed request.
    if (req.type == QLatin1String(RequestType::Unsubscribe) && req.subscriptionId == 0) {
        if (parseError) {
            *parseError = QStringLiteral("'unsubscribe' requires a non-zero 'subscriptionId'");
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
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Long:
    case QMetaType::LongLong:
        // Signed integer metatypes (≤64-bit). JSON numbers are IEEE
        // 754 doubles with 53 bits of integer precision; values
        // above 2^53 lose precision on the wire, but values that
        // fit in 53 bits round-trip exactly.
        return static_cast<double>(v.toLongLong());
    case QMetaType::UInt:
    case QMetaType::ULong:
    case QMetaType::ULongLong:
        // Unsigned variants must NOT route through toLongLong: on
        // LP64 systems ULong is 64-bit unsigned, and values above
        // LLONG_MAX wrap to negative when cast to qint64. Route
        // through the unsigned converter so the magnitude survives
        // (subject to the same 2^53 JSON precision ceiling).
        return static_cast<double>(v.toULongLong());
    case QMetaType::Double:
    case QMetaType::Float:
        return v.toDouble();
    case QMetaType::QString:
        return v.toString();
    case QMetaType::QStringList: {
        QJsonArray arr;
        // toStringList() returns an owned copy, so range-for is safe
        // (no detach risk on a temporary).
        for (const QString& s : v.toStringList()) {
            arr.append(s);
        }
        return arr;
    }
    case QMetaType::QVariantList: {
        QJsonArray arr;
        // toList() returns an owned copy; range-for is safe.
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
