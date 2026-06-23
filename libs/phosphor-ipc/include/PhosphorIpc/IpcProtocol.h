// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorIpc/phosphoripc_export.h>

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVariant>
#include <QVariantList>

#include <optional>

// Wire-format definitions for the JSON-over-Unix-socket protocol the
// IpcRouter speaks. Shared between libs/phosphor-ipc (server) and
// bin/phosphorctl (client); both link this library so the constants
// and parsers stay in lockstep. Mirrors the
// libs/phosphor-protocol/{WindowTypes.h,WindowMarshalling.h} split
// precedent, JSON is self-describing so we ship the type constants
// only, no Qt-marshalling boilerplate.
//
// Wire shape: one JSON object per line, '\n'-terminated, UTF-8.
// (Same NDJSON shape as niri-ipc, hyprland-ipc.)
//
// CMake mirrors the protocol-version constant via the
// PHOSPHOR_IPC_PROTOCOL_VERSION compile definition so this header
// and the build agree at compile time. Bumped whenever the
// JSON shape on the wire changes incompatibly.

namespace PhosphorIpc {

constexpr int ProtocolVersion = 1;

#ifdef PHOSPHOR_IPC_PROTOCOL_VERSION
static_assert(PHOSPHOR_IPC_PROTOCOL_VERSION == ProtocolVersion,
              "CMake's PHOSPHOR_IPC_PROTOCOL_VERSION must match IpcProtocol.h's ProtocolVersion");
#endif

// Field names used across the wire. Declared as constexpr string
// literals so usage sites can pass them to QJsonObject without
// constructing QString, and the compiler catches typos at the
// definition site instead of letting them silently land in the
// wire format.
namespace Field {
constexpr auto Type = "type";
constexpr auto Id = "id";
constexpr auto Target = "target";
constexpr auto Fn = "fn";
constexpr auto Args = "args";
constexpr auto Signal = "signal";
constexpr auto SubscriptionId = "subscriptionId";
constexpr auto Result = "result";
constexpr auto Code = "code";
constexpr auto Message = "message";
constexpr auto Detail = "detail";
} // namespace Field

// Request `type` discriminator values. Sent by the client.
namespace RequestType {
constexpr auto Call = "call";
constexpr auto List = "list";
constexpr auto Schema = "schema";
constexpr auto Subscribe = "subscribe";
constexpr auto Unsubscribe = "unsubscribe";
} // namespace RequestType

// Response `type` discriminator values. Sent by the server.
namespace ResponseType {
constexpr auto Reply = "reply"; // sync call / list / schema result
constexpr auto Event = "event"; // pushed signal payload to a subscriber
constexpr auto Error = "error"; // request-correlated failure
} // namespace ResponseType

// Error `code` strings the server returns. Stable across protocol
// version 1, adding new codes is safe; renaming or removing one
// is a protocol break.
namespace ErrorCode {
constexpr auto NoSuchTarget = "NO_SUCH_TARGET";
constexpr auto NoSuchFn = "NO_SUCH_FN";
constexpr auto NoSuchSignal = "NO_SUCH_SIGNAL";
constexpr auto NoSuchSubscription = "NO_SUCH_SUBSCRIPTION";
constexpr auto InvalidArg = "INVALID_ARG";
constexpr auto InvocationFailed = "INVOCATION_FAILED";
constexpr auto MalformedRequest = "MALFORMED_REQUEST";
} // namespace ErrorCode

// Parsed request shape, populated by parseRequest from a single
// JSON line. All fields are optional and only set when present;
// callers should inspect type and validate the corresponding fields.
struct PHOSPHORIPC_EXPORT Request
{
    QString type;
    qint64 id = 0;
    QString target;
    QString fn;
    // Spelled `signalName` (not just `signal`) because `signal` is
    // a Qt macro that vanishes under `-DQT_NO_KEYWORDS` builds; the
    // rename keeps the field accessible regardless of keyword mode
    // and avoids confusing Qt-pseudo-keyword shadowing.
    QString signalName;
    qint64 subscriptionId = 0;
    QVariantList args;
};

// Parse one NDJSON line into a Request. Returns std::nullopt and
// populates parseError on malformed input: invalid JSON, missing
// required `type` field, missing per-type required fields (`target`
// for call/schema/subscribe, `fn` for call, `signal` for subscribe,
// non-zero `subscriptionId` for unsubscribe), an `id` or
// `subscriptionId` that isn't a finite integer in the range
// exactly representable as both a JSON double and a qint64, or an
// `args` value that isn't an array of size ≤ 4096. Callers are
// expected to reply with a MALFORMED_REQUEST error.
[[nodiscard]] PHOSPHORIPC_EXPORT std::optional<Request> parseRequest(const QByteArray& line, QString* parseError);

// Encoders, build the QJsonObject body for each response variant.
// The router serialises the object to bytes via writeLine() below.
[[nodiscard]] PHOSPHORIPC_EXPORT QJsonObject buildReply(qint64 id, const QJsonValue& result);
[[nodiscard]] PHOSPHORIPC_EXPORT QJsonObject buildEvent(qint64 subscriptionId, const QJsonValue& args);
[[nodiscard]] PHOSPHORIPC_EXPORT QJsonObject buildError(qint64 id, const QString& code, const QString& message,
                                                        const QJsonObject& detail = {});

// Serialise a QJsonObject to a single NDJSON line, compact JSON
// followed by '\n'. Output is UTF-8 bytes ready to push into a
// QLocalSocket.
[[nodiscard]] PHOSPHORIPC_EXPORT QByteArray writeLine(const QJsonObject& obj);

// Convert a QVariant to a JSON value. Recursive on QVariantList /
// QVariantMap. Numeric / bool / string / list / map types map
// naturally; QJsonValue / QJsonObject / QJsonArray pass through;
// invalid QVariant maps to null; any other metatype degrades to
// the toString() representation. Shared between the router's
// call-return path and IpcTarget's emitEvent broadcast path so
// the wire JSON shape stays identical across the two.
[[nodiscard]] PHOSPHORIPC_EXPORT QJsonValue variantToJson(const QVariant& v);

} // namespace PhosphorIpc
