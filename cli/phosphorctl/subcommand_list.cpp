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

int runList(QStringList args, QString socketPath)
{
    QTextStream out(stdout);
    QTextStream err(stderr);

    // Allow `phosphorctl list --socket /path` in addition to the
    // top-level position, useful when alias-chaining.
    QString socketErr;
    const QString lateSocket = stripSocketFlag(args, &socketErr);
    if (!socketErr.isEmpty()) {
        err << "phosphorctl list: " << socketErr << "\n";
        return 1;
    }
    if (!lateSocket.isEmpty()) {
        socketPath = lateSocket;
    }

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
    req.insert(QLatin1String(PhosphorIpc::Field::Type), QLatin1String(PhosphorIpc::RequestType::List));
    req.insert(QLatin1String(PhosphorIpc::Field::Id), 1);

    const auto resp = client.request(req);
    if (!resp.has_value()) {
        err << "phosphorctl list: " << client.errorMessage() << "\n";
        return 2;
    }
    if (resp->value(QLatin1String(PhosphorIpc::Field::Type)).toString()
        == QLatin1String(PhosphorIpc::ResponseType::Error)) {
        err << "phosphorctl list: "
            << sanitiseForSingleLine(resp->value(QLatin1String(PhosphorIpc::Field::Code)).toString()) << ": "
            << sanitiseForSingleLine(resp->value(QLatin1String(PhosphorIpc::Field::Message)).toString()) << "\n";
        return 3;
    }
    const QJsonArray names = resp->value(QLatin1String(PhosphorIpc::Field::Result)).toArray();
    QStringList sorted;
    sorted.reserve(names.size());
    for (const QJsonValue& v : names) {
        if (!v.isString()) {
            // Server contract is string[]; non-string entries are a
            // protocol violation, log to stderr but keep going so a
            // partial list still surfaces.
            err << "phosphorctl list: ignoring non-string entry in result array\n";
            continue;
        }
        sorted.append(v.toString());
    }
    // Stable output regardless of QHash insertion order.
    sorted.sort();
    for (const QString& name : sorted) {
        // Target names come from server-side QString registrations
        // and are theoretically attacker-controlled in adversarial
        // setups. Use the strict single-line variant so a name
        // containing a stray newline can't break the line-per-
        // target output framing.
        out << sanitiseForSingleLine(name) << "\n";
    }
    return 0;
}

} // namespace Phosphorctl
