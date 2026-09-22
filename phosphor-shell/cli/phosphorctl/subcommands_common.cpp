// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "subcommands.h"

#include <QChar>
#include <QLatin1String>

namespace Phosphorctl {

namespace {
// Common control-byte escape shared by both sanitisers. Returns
// `\xNN` for any input that should be escaped; the caller decides
// whether to escape a given QChar.
QString escapeControl(ushort u)
{
    return QStringLiteral("\\x%1").arg(u, 2, 16, QLatin1Char('0'));
}
} // namespace

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
            out.append(escapeControl(u));
        } else {
            out.append(c);
        }
    }
    return out;
}

QString sanitiseForSingleLine(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        const ushort u = c.unicode();
        // Strict variant: escape every ASCII control byte INCLUDING
        // tab, newline, and carriage return so a server-supplied
        // string can't visually break out of the surrounding
        // single-line diagnostic frame on stderr.
        if (u < 0x20) {
            out.append(escapeControl(u));
        } else {
            out.append(c);
        }
    }
    return out;
}

QString stripSocketFlag(QStringList& args, QString* errorMessage)
{
    QString resolved;
    for (int i = 0; i < args.size();) {
        const QString& a = args.at(i);
        if (a == QLatin1String("--socket") || a == QLatin1String("-s")) {
            if (i + 1 >= args.size()) {
                // Dangling flag with no value. Surface as a usage
                // error rather than leaving the bare `--socket` in
                // args (where the subcommand handler would otherwise
                // treat it as a positional argument — schema would
                // try to query target "--socket", call would error
                // on the dot-form check, etc.).
                if (errorMessage) {
                    *errorMessage = QStringLiteral("'%1' requires a path argument").arg(a);
                }
                args.removeAt(i);
                return {};
            }
            resolved = args.at(i + 1);
            args.removeAt(i + 1);
            args.removeAt(i);
            continue;
        }
        if (a.startsWith(QLatin1String("--socket="))) {
            resolved = a.mid(QStringLiteral("--socket=").size());
            args.removeAt(i);
            if (resolved.isEmpty()) {
                // `--socket=` with empty value: same usage-error
                // surface as the dangling-flag case above. Don't
                // silently fall through to PHOSPHOR_SOCKET / XDG
                // defaults — the user expected the override to take
                // effect.
                if (errorMessage) {
                    *errorMessage = QStringLiteral("'--socket=' requires a non-empty path");
                }
                return {};
            }
            continue;
        }
        ++i;
    }
    return resolved;
}

} // namespace Phosphorctl
