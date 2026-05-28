// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "subcommands.h"

#include <QChar>
#include <QLatin1String>

namespace Phosphorctl {

QString sanitiseForTerminal(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        const ushort u = c.unicode();
        // Pass through '\t' and '\n' verbatim, those are
        // structurally meaningful for textual output (tab columns,
        // newlines between lines). Escape everything else below
        // 0x20 (the ASCII control range that includes ESC=0x1B,
        // the lead byte for ANSI/CSI/OSC terminal sequences) as a
        // visible `\xNN` literal. We deliberately do NOT escape
        // 0x7F (DEL): it's a benign legacy control byte that
        // doesn't lead a terminal escape sequence, and matching
        // JSON spec (which only escapes 0x00-0x1F) keeps the
        // string and JSON output paths symmetric.
        if (u == 0x09 || u == 0x0A) {
            out.append(c);
        } else if (u < 0x20) {
            out.append(QStringLiteral("\\x%1").arg(u, 2, 16, QLatin1Char('0')));
        } else {
            out.append(c);
        }
    }
    return out;
}

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
