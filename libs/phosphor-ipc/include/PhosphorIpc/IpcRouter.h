// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorIpc/phosphoripc_export.h>

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QtCore/qtclasshelpermacros.h>

#include <memory>

QT_BEGIN_NAMESPACE
class QLocalServer;
class QLocalSocket;
QT_END_NAMESPACE

namespace PhosphorIpc {

// Central JSON-over-Unix-socket dispatcher. Owns one QLocalServer and
// the registry of named callable QObject "targets". Speaks the
// NDJSON wire protocol defined in IpcProtocol.h. Single-threaded —
// must be constructed and used on the GUI thread (Qt's QLocalServer
// + the QMetaObject::invokeMethod path both assume that).
//
// Lifetime
//   - Application owns one IpcRouter (typically constructed in main()
//     alongside the QQmlApplicationEngine).
//   - Targets register their backing QObjects via registerTarget;
//     unregisterTarget drops them. Target QObjects must outlive their
//     registration (QPointer tracking handles dangling-pointer paths
//     defensively, but the contract is "drop the registration before
//     destroying the QObject").
//   - Subscribers are tracked per-(target, signal-index, socket)
//     tuple. Socket disconnect auto-prunes the subscriber set.
//
// Errors
//   - start() returns false on bind failure (path conflict, perms,
//     missing XDG dir). The application logs the reason and
//     continues without IPC; failure is non-fatal.
//   - All wire-level errors are reported back to the client as
//     {"type":"error",...} responses (see IpcProtocol::ErrorCode).
class PHOSPHORIPC_EXPORT IpcRouter : public QObject
{
    Q_OBJECT
public:
    explicit IpcRouter(QObject* parent = nullptr);
    ~IpcRouter() override;
    Q_DISABLE_COPY_MOVE(IpcRouter)

    // Bind the listening socket. Defaults to
    // $XDG_RUNTIME_DIR/phosphor.sock when socketPath is empty.
    // Returns false on bind failure; safe to retry after stop().
    bool start(const QString& socketPath = QString());

    // Close the listening socket. Existing connections drain on
    // their own (subscribers stop receiving events when the
    // socket disconnects). Idempotent.
    void stop();

    // The resolved socket path (after XDG fallback). Empty if
    // start() has not been called or failed.
    [[nodiscard]] QString socketPath() const;

    // Register / unregister target QObjects. Re-registering an
    // existing target name is a no-op + qWarning (first registration
    // wins).
    void registerTarget(const QString& name, QObject* object);
    void unregisterTarget(const QString& name);

    [[nodiscard]] QStringList listTargets() const;

    // Look up the live QObject backing a target. Returns nullptr if
    // the target is unknown or its backing QObject was destroyed
    // out from under us (QPointer auto-clears).
    [[nodiscard]] QObject* target(const QString& name) const;

    // Generate the JSON Schema for one target. Walks the target
    // QObject's metaobject; see IpcSchemaGenerator.h for the
    // QMetaType → JSON Schema mapping. Returns {} for unknown
    // targets.
    [[nodiscard]] QJsonObject schemaFor(const QString& target) const;

    // Synchronously invoke target.fn(args). Returns the function's
    // return value as a QVariant; on failure, populates errorOut
    // with a human-readable diagnostic and returns an invalid
    // QVariant. errorOut may be nullptr if the caller doesn't need
    // the message.
    QVariant invoke(const QString& target, const QString& fn, const QVariantList& args, QString* errorOut);

Q_SIGNALS:
    void targetRegistered(const QString& name);
    void targetUnregistered(const QString& name);

private:
    struct Subscription
    {
        QPointer<QLocalSocket> socket;
        qint64 subscriptionId = 0;
        QString target;
        int signalIndex = -1;
    };

    void handleNewConnection();
    void handleClientReadyRead(QLocalSocket* socket);
    void handleClientDisconnected(QLocalSocket* socket);
    void dispatch(QLocalSocket* socket, const QByteArray& line);

    std::unique_ptr<QLocalServer> m_server;
    QString m_socketPath;
    QHash<QString, QPointer<QObject>> m_targets;
    // Per-socket subscription list; lets disconnect cleanup walk one
    // socket's subscriptions without scanning the whole router.
    QHash<QLocalSocket*, QList<Subscription>> m_subscriptionsBySocket;
};

} // namespace PhosphorIpc
