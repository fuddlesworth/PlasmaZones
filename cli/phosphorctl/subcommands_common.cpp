// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "subcommands.h"

#include <QLatin1String>

namespace Phosphorctl {

QString stripSocketFlag(QStringList& args)
{
    QString resolved;
    for (int i = 0; i < args.size();) {
        const QString& a = args.at(i);
        if (a == QLatin1String("--socket") || a == QLatin1String("-s")) {
            if (i + 1 >= args.size()) {
                // Caller will reject the request on missing value;
                // we leave the flag in place so the error surfaces
                // through the subcommand parser.
                ++i;
                continue;
            }
            resolved = args.at(i + 1);
            args.removeAt(i + 1);
            args.removeAt(i);
            continue;
        }
        if (a.startsWith(QLatin1String("--socket="))) {
            resolved = a.mid(QStringLiteral("--socket=").size());
            args.removeAt(i);
            continue;
        }
        ++i;
    }
    return resolved;
}

} // namespace Phosphorctl
