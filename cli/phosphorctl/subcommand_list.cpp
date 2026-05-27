// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "subcommands.h"

#include "client.h"

#include <PhosphorIpc/IpcProtocol.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QTextStream>

namespace Phosphorctl {

int runList(const QStringList& args, const QString& socketPath)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    if (!args.isEmpty()) {
        err << "phosphorctl list: takes no arguments\n";
        return 1;
    }

    Client client;
    if (!client.connectTo(socketPath)) {
        err << "phosphorctl list: " << client.errorMessage() << "\n";
        return 2;
    }

    QJsonObject req;
    req.insert(QStringLiteral("type"), QString::fromUtf8(PhosphorIpc::RequestType::List));
    req.insert(QStringLiteral("id"), 1);

    const auto resp = client.request(req);
    if (!resp.has_value()) {
        err << "phosphorctl list: " << client.errorMessage() << "\n";
        return 2;
    }
    if (resp->value(QStringLiteral("type")).toString() == QString::fromUtf8(PhosphorIpc::ResponseType::Error)) {
        err << "phosphorctl list: server error: " << resp->value(QStringLiteral("message")).toString() << "\n";
        return 3;
    }
    const QJsonArray names = resp->value(QStringLiteral("result")).toArray();
    QStringList sorted;
    for (const QJsonValue& v : names) {
        sorted.append(v.toString());
    }
    // Stable output regardless of QHash insertion order.
    sorted.sort();
    for (const QString& name : sorted) {
        out << name << "\n";
    }
    return 0;
}

} // namespace Phosphorctl
