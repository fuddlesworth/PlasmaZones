// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "subcommands.h"

#include "client.h"

#include <PhosphorIpc/IpcProtocol.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTextStream>

namespace Phosphorctl {

int runSchema(QStringList args, QString socketPath)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    const QString lateSocket = stripSocketFlag(args);
    if (!lateSocket.isEmpty()) {
        socketPath = lateSocket;
    }

    if (args.size() != 1) {
        err << "phosphorctl schema: expects exactly one argument: <target>\n";
        return 1;
    }
    const QString target = args.first();

    Client client;
    if (!client.connectTo(socketPath)) {
        err << "phosphorctl schema: " << client.errorMessage() << "\n";
        return 2;
    }

    QJsonObject req;
    req.insert(QString::fromUtf8(PhosphorIpc::Field::Type), QString::fromUtf8(PhosphorIpc::RequestType::Schema));
    req.insert(QString::fromUtf8(PhosphorIpc::Field::Id), 1);
    req.insert(QString::fromUtf8(PhosphorIpc::Field::Target), target);

    const auto resp = client.request(req);
    if (!resp.has_value()) {
        err << "phosphorctl schema: " << client.errorMessage() << "\n";
        return 2;
    }
    if (resp->value(QString::fromUtf8(PhosphorIpc::Field::Type)).toString()
        == QString::fromUtf8(PhosphorIpc::ResponseType::Error)) {
        err << "phosphorctl schema: server error: "
            << resp->value(QString::fromUtf8(PhosphorIpc::Field::Message)).toString() << "\n";
        return 3;
    }
    const QJsonObject schema = resp->value(QString::fromUtf8(PhosphorIpc::Field::Result)).toObject();
    out << QString::fromUtf8(QJsonDocument(schema).toJson(QJsonDocument::Indented));
    return 0;
}

} // namespace Phosphorctl
