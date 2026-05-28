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
    QString socketErr;
    const QString lateSocket = stripSocketFlag(args, &socketErr);
    if (!socketErr.isEmpty()) {
        err << "phosphorctl schema: " << socketErr << "\n";
        return 1;
    }
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
    req.insert(QLatin1String(PhosphorIpc::Field::Type), QLatin1String(PhosphorIpc::RequestType::Schema));
    req.insert(QLatin1String(PhosphorIpc::Field::Id), 1);
    req.insert(QLatin1String(PhosphorIpc::Field::Target), target);

    const auto resp = client.request(req);
    if (!resp.has_value()) {
        err << "phosphorctl schema: " << client.errorMessage() << "\n";
        return 2;
    }
    if (resp->value(QLatin1String(PhosphorIpc::Field::Type)).toString()
        == QLatin1String(PhosphorIpc::ResponseType::Error)) {
        err << "phosphorctl schema: "
            << sanitiseForSingleLine(resp->value(QLatin1String(PhosphorIpc::Field::Code)).toString()) << ": "
            << sanitiseForSingleLine(resp->value(QLatin1String(PhosphorIpc::Field::Message)).toString()) << "\n";
        return 3;
    }
    const QJsonValue resultValue = resp->value(QLatin1String(PhosphorIpc::Field::Result));
    if (!resultValue.isObject()) {
        // Defensive: server contract is `result` is the schema
        // object. A non-object result is a protocol violation that
        // would otherwise pretty-print as `{}` with exit 0,
        // silently misleading. Exit 3 so scripts can tell.
        err << "phosphorctl schema: protocol error: server returned non-object result\n";
        return 3;
    }
    // QJsonDocument::toJson escapes ASCII control bytes per the JSON
    // spec, so the indented output is terminal-safe without further
    // sanitisation. Indented mode already appends a trailing '\n',
    // so no explicit newline suffix is needed (Compact would
    // require one, but we use Indented here).
    out << QString::fromUtf8(QJsonDocument(resultValue.toObject()).toJson(QJsonDocument::Indented));
    return 0;
}

} // namespace Phosphorctl
