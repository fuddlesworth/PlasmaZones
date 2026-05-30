// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceBluetooth/phosphorservicebluetooth_export.h>

#include <QDBusContext>
#include <QObject>
#include <QString>

#include <memory>

class QDBusObjectPath;

namespace PhosphorServiceBluetooth {

/**
 * @brief Implements `org.bluez.Agent1`, the pairing agent BlueZ calls back
 * into during a Pair() handshake.
 *
 * Registered by `BluetoothHost` with `KeyboardDisplay` capability, this is the
 * library's only D-Bus *server* object. The interactive callbacks
 * (RequestPinCode / RequestPasskey / RequestConfirmation / RequestAuthorization
 * / AuthorizeService) cannot answer BlueZ synchronously: they need the user.
 * Each therefore takes a delayed D-Bus reply (`QDBusContext::setDelayedReply`)
 * and emits a Qt request signal carrying a `requestId`; a consumer (the CLI
 * now, a pairing dialog later) answers via `respondPinCode` / `respondPasskey`
 * / `respondConfirmation` / `rejectRequest`, which sends the held reply.
 *
 * The agent is implemented directly on this QObject (exported via
 * `ExportAllSlots`) rather than through a generated `QDBusAbstractAdaptor`,
 * because the adaptor would become QtDBus's dispatch target and
 * `setDelayedReply` would no longer apply to this object's calls. Display-only
 * callbacks (DisplayPinCode / DisplayPasskey) and Release / Cancel return
 * immediately.
 */
class PHOSPHORSERVICEBLUETOOTH_EXPORT BluetoothAgent : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")

public:
    explicit BluetoothAgent(QObject* parent = nullptr);
    ~BluetoothAgent() override;

    /// Object path the agent is exported at and registered under with
    /// `org.bluez.AgentManager1`.
    [[nodiscard]] static QString agentPath();

    /// Number of requests awaiting a consumer response. Primarily for tests
    /// and introspection.
    [[nodiscard]] int pendingRequestCount() const;

    // Consumer responses to the request signals below. Q_INVOKABLE (not slots)
    // so they are callable from QML/CLI but NOT exported on the Agent1
    // interface. A mismatched id or request kind is ignored.
    Q_INVOKABLE void respondPinCode(quint64 requestId, const QString& pinCode);
    Q_INVOKABLE void respondPasskey(quint64 requestId, quint32 passkey);
    /// Accept (or, with @p accept false, reject) a confirmation /
    /// authorization / service-authorization request.
    Q_INVOKABLE void respondConfirmation(quint64 requestId, bool accept);
    /// Reject any pending request with `org.bluez.Error.Rejected`.
    Q_INVOKABLE void rejectRequest(quint64 requestId);

public Q_SLOTS:
    // org.bluez.Agent1, exported to BlueZ via ExportAllSlots. Signatures
    // mirror the BlueZ spec exactly; the interactive ones defer their reply.
    //
    // SECURITY: ONLY org.bluez.Agent1 methods may live in this slot block.
    // ExportAllSlots publishes every public slot on the Agent1 interface, so
    // adding an unrelated slot here would expose it to any bus peer. The
    // consumer-facing respond*/rejectRequest methods are deliberately
    // Q_INVOKABLE (above), NOT slots, so a remote can never answer its own
    // pairing prompt. Every interactive request MUST be answered (respond* /
    // rejectRequest) or BlueZ's Pair() blocks until its own timeout; the
    // host's consumer wires all of the request signals below.
    void Release();
    QString RequestPinCode(const QDBusObjectPath& device);
    void DisplayPinCode(const QDBusObjectPath& device, const QString& pincode);
    uint RequestPasskey(const QDBusObjectPath& device);
    void DisplayPasskey(const QDBusObjectPath& device, uint passkey, ushort entered);
    void RequestConfirmation(const QDBusObjectPath& device, uint passkey);
    void RequestAuthorization(const QDBusObjectPath& device);
    void AuthorizeService(const QDBusObjectPath& device, const QString& uuid);
    void Cancel();

Q_SIGNALS:
    /// A PIN code (legacy) is required for @p devicePath. Answer with
    /// respondPinCode(requestId, ...) or rejectRequest(requestId).
    void pinCodeRequested(const QString& devicePath, quint64 requestId);
    /// A numeric passkey is required. Answer with respondPasskey / rejectRequest.
    void passkeyRequested(const QString& devicePath, quint64 requestId);
    /// Confirm that @p passkey matches the one shown on @p devicePath. Answer
    /// with respondConfirmation(requestId, accept) or rejectRequest.
    void confirmationRequested(const QString& devicePath, quint32 passkey, quint64 requestId);
    /// Authorize an incoming pairing with no MITM protection. Answer with
    /// respondConfirmation / rejectRequest.
    void authorizationRequested(const QString& devicePath, quint64 requestId);
    /// Authorize @p uuid on @p devicePath. Answer with respondConfirmation /
    /// rejectRequest.
    void serviceAuthorizationRequested(const QString& devicePath, const QString& uuid, quint64 requestId);

    /// Display-only: show @p pinCode / @p passkey to the user (no response).
    void pinCodeDisplayed(const QString& devicePath, const QString& pinCode);
    void passkeyDisplayed(const QString& devicePath, quint32 passkey, quint16 entered);

    /// BlueZ withdrew the in-flight request (Cancel); any prompt should close.
    void requestCancelled();
    /// BlueZ released the agent (Release); it is no longer registered.
    void released();

private:
    Q_DISABLE_COPY_MOVE(BluetoothAgent)
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceBluetooth
