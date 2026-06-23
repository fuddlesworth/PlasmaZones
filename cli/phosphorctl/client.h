// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QtCore/qtclasshelpermacros.h>

#include <optional>

namespace Phosphorctl {

// Single-shot QLocalSocket wrapper that speaks the phosphor-ipc
// wire protocol (NDJSON, line-framed). Synchronous request/response:
// connect, send one request line, wait for one response line, close.
// Streaming subscribe lives on a longer-lived API path; see
// SubscribeClient in subcommand_subscribe.cpp.
//
// Resolves the socket path in priority order:
//   1. --socket <path> CLI flag (when set, takes precedence)
//   2. PHOSPHOR_SOCKET environment variable (lets demos override
//      without ambiguous CLI arg juggling)
//   3. $XDG_RUNTIME_DIR/phosphor.sock (production default)
//
// Exit-code semantics live on the subcommand handlers; Client just
// reports success/failure + optional error message.
class Client : public QObject
{
    Q_OBJECT
public:
    explicit Client(QObject* parent = nullptr);
    Q_DISABLE_COPY_MOVE(Client)

    // Resolve the socket path per the three-tier rule above.
    // Empty cliArg means "no --socket flag supplied". Returns
    // empty if all three sources are exhausted (process logs and
    // exits with code 2 in that case).
    [[nodiscard]] static QString resolveSocketPath(const QString& cliArg);

    // Connect to socketPath. Returns false on failure (timeout,
    // refused). errorMessage populated. Aborts any prior open
    // socket first so retry-after-failure paths don't leak a
    // half-open connection.
    [[nodiscard]] bool connectTo(const QString& socketPath, int timeoutMs = 2000);

    // Send a request, await a single response line with a matching
    // `id`. Frames whose id doesn't match (e.g. a stray event from
    // an unrelated subscription on the same connection) are NOT
    // discarded; they stay in the internal read buffer so the
    // streaming-subscribe path can recover them via takePendingBytes.
    // Without that handoff a server that broadcasts an event in the
    // same readyRead burst as the subscribe ack would silently drop
    // that first event. Returns the parsed response object, or
    // std::nullopt on read timeout / disconnect / parse error.
    // errorMessage populated on failure.
    [[nodiscard]] std::optional<QJsonObject> request(const QJsonObject& req, int timeoutMs = 2000);

    [[nodiscard]] QString errorMessage() const;

    // Expose the underlying socket for the streaming-subscribe
    // path; ownership stays with Client.
    [[nodiscard]] QLocalSocket* socket();

    // Return any leftover read-buffer bytes that arrived AFTER the
    // matching response for the most recent request() but before
    // request() returned. The subscribe loop prepends these to its
    // own buffer so events that piggy-backed onto the subscribe
    // ack's readyRead are recovered cleanly. Clears the internal
    // buffer as a side effect, call exactly once at the seam
    // between request() and the streaming reader.
    [[nodiscard]] QByteArray takePendingBytes();

private:
    QLocalSocket m_socket;
    QByteArray m_readBuffer;
    QString m_errorMessage;
};

} // namespace Phosphorctl
