// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceBluetooth/BluetoothAgent.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QHash>

#include <optional>

namespace {
constexpr auto kAgentPath = "/org/phosphor/bluetooth/agent";
constexpr auto kRejectedError = "org.bluez.Error.Rejected";
constexpr auto kCanceledError = "org.bluez.Error.Canceled";
} // namespace

namespace PhosphorServiceBluetooth {

class BluetoothAgent::Private
{
public:
    enum class RequestKind {
        PinCode,
        Passkey,
        Confirmation,
        Authorization,
        ServiceAuthorization
    };

    struct PendingRequest
    {
        quint64 id = 0;
        RequestKind kind = RequestKind::Confirmation;
        QDBusMessage message; // the held method call; type() is Invalid when not from D-Bus
        QDBusConnection connection = QDBusConnection::sessionBus();
    };

    quint64 nextId = 0;
    QHash<quint64, PendingRequest> pending;

    quint64 add(RequestKind kind, const QDBusMessage& message, const QDBusConnection& connection)
    {
        const quint64 id = ++nextId;
        pending.insert(id, PendingRequest{id, kind, message, connection});
        return id;
    }

    std::optional<PendingRequest> take(quint64 id, RequestKind expected)
    {
        auto it = pending.find(id);
        if (it == pending.end() || it.value().kind != expected)
            return std::nullopt;
        const PendingRequest request = it.value();
        pending.erase(it);
        return request;
    }

    std::optional<PendingRequest> takeAny(quint64 id)
    {
        auto it = pending.find(id);
        if (it == pending.end())
            return std::nullopt;
        const PendingRequest request = it.value();
        pending.erase(it);
        return request;
    }

    // Reply helpers. Each is a no-op unless the request actually came from a
    // live D-Bus caller; a directly-invoked request (tests) holds an Invalid
    // message, and building a reply from one would warn, so the guard precedes
    // createReply/createErrorReply.
    static void replyValue(const PendingRequest& request, const QVariant& value)
    {
        if (request.message.type() == QDBusMessage::MethodCallMessage)
            request.connection.send(request.message.createReply(value));
    }
    static void replyEmpty(const PendingRequest& request)
    {
        if (request.message.type() == QDBusMessage::MethodCallMessage)
            request.connection.send(request.message.createReply());
    }
    static void replyError(const PendingRequest& request, const QString& name, const QString& text)
    {
        if (request.message.type() == QDBusMessage::MethodCallMessage)
            request.connection.send(request.message.createErrorReply(name, text));
    }
};

BluetoothAgent::BluetoothAgent(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
}

BluetoothAgent::~BluetoothAgent() = default;

QString BluetoothAgent::agentPath()
{
    return QLatin1String(kAgentPath);
}

int BluetoothAgent::pendingRequestCount() const
{
    return static_cast<int>(d->pending.size());
}

void BluetoothAgent::Release()
{
    // BlueZ no longer needs this agent (e.g. it was unregistered). Drop any
    // in-flight requests; they can no longer be answered meaningfully.
    d->pending.clear();
    Q_EMIT released();
}

QString BluetoothAgent::RequestPinCode(const QDBusObjectPath& device)
{
    QDBusMessage held;
    QDBusConnection connection = QDBusConnection::sessionBus();
    if (calledFromDBus()) {
        setDelayedReply(true);
        held = message();
        connection = this->connection();
    }
    const quint64 id = d->add(Private::RequestKind::PinCode, held, connection);
    Q_EMIT pinCodeRequested(device.path(), id);
    return {}; // suppressed: the real reply is sent via respondPinCode
}

void BluetoothAgent::DisplayPinCode(const QDBusObjectPath& device, const QString& pincode)
{
    Q_EMIT pinCodeDisplayed(device.path(), pincode);
}

uint BluetoothAgent::RequestPasskey(const QDBusObjectPath& device)
{
    QDBusMessage held;
    QDBusConnection connection = QDBusConnection::sessionBus();
    if (calledFromDBus()) {
        setDelayedReply(true);
        held = message();
        connection = this->connection();
    }
    const quint64 id = d->add(Private::RequestKind::Passkey, held, connection);
    Q_EMIT passkeyRequested(device.path(), id);
    return 0; // suppressed: the real reply is sent via respondPasskey
}

void BluetoothAgent::DisplayPasskey(const QDBusObjectPath& device, uint passkey, ushort entered)
{
    Q_EMIT passkeyDisplayed(device.path(), passkey, entered);
}

void BluetoothAgent::RequestConfirmation(const QDBusObjectPath& device, uint passkey)
{
    QDBusMessage held;
    QDBusConnection connection = QDBusConnection::sessionBus();
    if (calledFromDBus()) {
        setDelayedReply(true);
        held = message();
        connection = this->connection();
    }
    const quint64 id = d->add(Private::RequestKind::Confirmation, held, connection);
    Q_EMIT confirmationRequested(device.path(), passkey, id);
}

void BluetoothAgent::RequestAuthorization(const QDBusObjectPath& device)
{
    QDBusMessage held;
    QDBusConnection connection = QDBusConnection::sessionBus();
    if (calledFromDBus()) {
        setDelayedReply(true);
        held = message();
        connection = this->connection();
    }
    const quint64 id = d->add(Private::RequestKind::Authorization, held, connection);
    Q_EMIT authorizationRequested(device.path(), id);
}

void BluetoothAgent::AuthorizeService(const QDBusObjectPath& device, const QString& uuid)
{
    QDBusMessage held;
    QDBusConnection connection = QDBusConnection::sessionBus();
    if (calledFromDBus()) {
        setDelayedReply(true);
        held = message();
        connection = this->connection();
    }
    const quint64 id = d->add(Private::RequestKind::ServiceAuthorization, held, connection);
    Q_EMIT serviceAuthorizationRequested(device.path(), uuid, id);
}

void BluetoothAgent::Cancel()
{
    // BlueZ aborted the pairing; answer every pending request with Canceled
    // so the daemon's state machine unwinds, and tell any open prompt to close.
    const auto requests = d->pending;
    d->pending.clear();
    for (const auto& request : requests)
        Private::replyError(request, QLatin1String(kCanceledError), QStringLiteral("Pairing cancelled"));
    Q_EMIT requestCancelled();
}

void BluetoothAgent::respondPinCode(quint64 requestId, const QString& pinCode)
{
    const auto request = d->take(requestId, Private::RequestKind::PinCode);
    if (!request)
        return;
    Private::replyValue(*request, pinCode);
}

void BluetoothAgent::respondPasskey(quint64 requestId, quint32 passkey)
{
    const auto request = d->take(requestId, Private::RequestKind::Passkey);
    if (!request)
        return;
    Private::replyValue(*request, QVariant::fromValue<uint>(passkey));
}

void BluetoothAgent::respondConfirmation(quint64 requestId, bool accept)
{
    auto it = d->pending.find(requestId);
    if (it == d->pending.end())
        return;
    const Private::RequestKind kind = it.value().kind;
    if (kind != Private::RequestKind::Confirmation && kind != Private::RequestKind::Authorization
        && kind != Private::RequestKind::ServiceAuthorization) {
        return;
    }
    const Private::PendingRequest request = it.value();
    d->pending.erase(it);
    if (accept)
        Private::replyEmpty(request);
    else
        Private::replyError(request, QLatin1String(kRejectedError), QStringLiteral("Rejected by user"));
}

void BluetoothAgent::rejectRequest(quint64 requestId)
{
    const auto request = d->takeAny(requestId);
    if (!request)
        return;
    Private::replyError(*request, QLatin1String(kRejectedError), QStringLiteral("Rejected by user"));
}

} // namespace PhosphorServiceBluetooth
