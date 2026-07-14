// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "algorithmscaffold.h"

#include <QLatin1Char>
#include <QLatin1String>
#include <QRegularExpression>
#include <QStringList>

namespace PlasmaZones {
namespace AlgorithmScaffold {

namespace {

/// Index of the first line past the template's own SPDX header (leading blank
/// lines and `-- SPDX-*` comment lines), so the caller can substitute a fresh
/// header. Other leading comments — a template's explanatory doc block — are
/// NOT skipped; they carry over into the personalized copy.
int firstLinePastSpdxHeader(const QStringList& lines)
{
    int i = 0;
    while (i < lines.size()) {
        const QString t = lines[i].trimmed();
        if (t.isEmpty() || t.startsWith(QLatin1String("-- SPDX-"))) {
            ++i;
            continue;
        }
        break;
    }
    return i;
}

/// The template's own `-- SPDX-*` lines, copyright lines first and license
/// lines after (the order this project's files use). The copy keeps the
/// template's code substantially verbatim, so it is a derivative work: it
/// retains the upstream author's copyright and stays under the template's
/// own license.
QStringList templateSpdxLines(const QStringList& lines, int spdxHeaderEnd)
{
    QStringList copyrights;
    QStringList licenses;
    for (int i = 0; i < spdxHeaderEnd && i < lines.size(); ++i) {
        const QString t = lines[i].trimmed();
        if (t.startsWith(QLatin1String("-- SPDX-FileCopyrightText:"))) {
            copyrights += t;
        } else if (t.startsWith(QLatin1String("-- SPDX-"))) {
            licenses += t;
        }
    }
    return copyrights + licenses;
}

QString quotedField(const QString& key, const QString& value)
{
    return QStringLiteral("        ") + key + QStringLiteral(" = \"") + value + QStringLiteral("\",");
}

/// Net brace delta of one line, ignoring braces inside quoted Luau strings
/// (double or single quoted, with backslash escapes) and after a `--` comment
/// marker. Line-scoped: long strings/comments (`[[...]]`) and short strings
/// continued across lines (backslash-newline, `\z`) are not handled; the
/// bundled templates' metadata uses neither.
int braceDelta(const QString& line)
{
    int delta = 0;
    QChar quote;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (!quote.isNull()) {
            if (c == QLatin1Char('\\')) {
                ++i;
            } else if (c == quote) {
                quote = QChar();
            }
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
        } else if (c == QLatin1Char('-') && i + 1 < line.size() && line[i + 1] == QLatin1Char('-')) {
            break;
        } else if (c == QLatin1Char('{')) {
            ++delta;
        } else if (c == QLatin1Char('}')) {
            --delta;
        }
    }
    return delta;
}

} // anonymous namespace

QString sanitizeMetadataString(QString value)
{
    value.replace(QLatin1Char('\n'), QLatin1Char(' '));
    value.replace(QLatin1Char('\r'), QLatin1Char(' '));
    value.replace(QLatin1Char('\\'), QLatin1Char('/'));
    value.replace(QLatin1Char('"'), QLatin1Char('\''));
    return value;
}

QString buildBlankScaffold(const QString& header, const QString& displayName, const QString& id,
                           const Capabilities& caps)
{
    const auto b = [](bool v) {
        return v ? QStringLiteral("true") : QStringLiteral("false");
    };

    QString m;
    m += QStringLiteral("    metadata = {\n");
    m += quotedField(QStringLiteral("name"), displayName) + QLatin1Char('\n');
    m += quotedField(QStringLiteral("id"), id) + QLatin1Char('\n');
    m += quotedField(QStringLiteral("description"), QStringLiteral("Custom tiling algorithm")) + QLatin1Char('\n');
    m += QStringLiteral("        producesOverlappingZones = ") + b(caps.overlappingZones) + QStringLiteral(",\n");
    m += QStringLiteral("        supportsMasterCount = ") + b(caps.masterCount) + QStringLiteral(",\n");
    m += QStringLiteral("        supportsSplitRatio = ") + b(caps.splitRatio) + QStringLiteral(",\n");
    m += QStringLiteral("        defaultSplitRatio = 0.5,\n");
    m += QStringLiteral("        defaultMaxWindows = 6,\n");
    m += QStringLiteral("        minimumWindows = 1,\n");
    m += QStringLiteral("        zoneNumberDisplay = \"all\",\n");
    m += QStringLiteral("        supportsMemory = ") + b(caps.memory) + QStringLiteral(",\n");
    // The newer opt-in flags are omitted when unset, matching the bundled
    // algorithms' style (absent means false).
    if (caps.scriptState) {
        m += QStringLiteral("        supportsScriptState = true,\n");
    }
    if (caps.singleWindow) {
        m += QStringLiteral("        supportsSingleWindow = true,\n");
    }
    if (caps.retileOnFocus) {
        m += QStringLiteral("        retileOnFocus = true,\n");
    }
    m += QStringLiteral("    },");

    QString content = header + QStringLiteral("\nlocal pluau = pluau\n\nreturn pluau.algorithm {\n") + m
        + QStringLiteral("\n\n") + QStringLiteral("    tile = function(ctx)\n")
        + QStringLiteral("        if ctx.windowCount <= 0 then return {} end\n")
        + QStringLiteral("        -- Fall back to a plain fill when the work area is too small to split.\n")
        + QStringLiteral("        local early = pluau.guardArea(ctx.area, ctx.windowCount)\n")
        + QStringLiteral("        if early then return early end\n")
        + QStringLiteral("        return pluau.fillArea(ctx.area, ctx.windowCount)\n") + QStringLiteral("    end,\n");

    if (caps.scriptState) {
        content += QStringLiteral("\n")
            + QStringLiteral("    -- Called after an interactive resize. Return a table to persist as\n")
            + QStringLiteral("    -- ctx.state for the next tile() run; include a splitRatio key to have\n")
            + QStringLiteral("    -- the engine apply a new master/split ratio. Return nil to do nothing.\n")
            + QStringLiteral("    onWindowResized = function(state, resize)\n") + QStringLiteral("        return nil\n")
            + QStringLiteral("    end,\n");
    }

    content += QStringLiteral("}\n");
    return content;
}

QString spliceTemplate(const QString& templateContent, const QString& newCopyrightLine, const QString& displayName,
                       const QString& id)
{
    QString normalized = templateContent;
    normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    const QStringList lines = normalized.split(QLatin1Char('\n'));
    const int firstCode = firstLinePastSpdxHeader(lines);

    static const QRegularExpression metaRe(QStringLiteral(R"(^\s*metadata\s*=\s*\{)"));
    static const QRegularExpression metaOpenLineRe(QStringLiteral(R"(^\s*metadata\s*=\s*\{\s*$)"));
    int metaStart = -1;
    for (int i = firstCode; i < lines.size(); ++i) {
        if (metaRe.match(lines[i]).hasMatch()) {
            metaStart = i;
            break;
        }
    }
    if (metaStart < 0) {
        return QString();
    }
    // The opening line must end at the `{`: a table that opens and closes on
    // one line, or opens with inline fields, has name/id in positions the
    // per-line rewrite cannot reach — unsupported shape, reject.
    if (!metaOpenLineRe.match(lines[metaStart]).hasMatch()) {
        return QString();
    }

    // Walk the metadata table, tracking brace depth relative to it. Only
    // depth-1 lines are top-level metadata fields — customParams entries nest
    // deeper and must keep their own `name` keys untouched. A depth-1 name/id
    // line is rewritten only when it is a whole single-field line; anything
    // else (a second field on the same line, an unquoted value) is an
    // unrecognized shape and rejects the splice rather than corrupting it.
    static const QRegularExpression nameFieldRe(QStringLiteral(R"(^\s*name\s*=)"));
    static const QRegularExpression idFieldRe(QStringLiteral(R"(^\s*id\s*=)"));
    static const QRegularExpression nameLineRe(QStringLiteral(R"(^\s*name\s*=\s*"[^"]*"\s*,?\s*$)"));
    static const QRegularExpression idLineRe(QStringLiteral(R"(^\s*id\s*=\s*"[^"]*"\s*,?\s*$)"));

    QStringList metaLines;
    int depth = 0;
    int metaEnd = -1;
    bool sawName = false;
    bool sawId = false;
    for (int i = metaStart; i < lines.size(); ++i) {
        const int depthAtLineStart = depth;
        depth += braceDelta(lines[i]);

        QString line = lines[i];
        if (i > metaStart && depthAtLineStart == 1) {
            if (nameFieldRe.match(line).hasMatch()) {
                if (!nameLineRe.match(line).hasMatch()) {
                    return QString();
                }
                line = quotedField(QStringLiteral("name"), displayName);
                sawName = true;
            } else if (idFieldRe.match(line).hasMatch()) {
                if (!idLineRe.match(line).hasMatch()) {
                    return QString();
                }
                line = quotedField(QStringLiteral("id"), id);
                sawId = true;
            }
        }
        metaLines += line;

        if (depth <= 0) {
            // A negative depth means more closers than openers — malformed
            // input; leave metaEnd at -1 so the splice rejects it.
            if (depth == 0) {
                metaEnd = i;
            }
            break;
        }
    }
    if (metaEnd < 0) {
        return QString();
    }

    // A template without its own name/id line still needs ours — insert
    // right after the opening `metadata = {`.
    if (!sawId) {
        metaLines.insert(1, quotedField(QStringLiteral("id"), id));
    }
    if (!sawName) {
        metaLines.insert(1, quotedField(QStringLiteral("name"), displayName));
    }

    // Header: the new owner's copyright, then the template's own SPDX lines
    // (its copyright and its license) — the copy is a derivative work, so it
    // credits the upstream author and stays under the upstream license. A
    // template that already carries the caller's exact copyright line (a copy
    // of a copy) does not get it twice.
    QString out = newCopyrightLine.trimmed() + QLatin1Char('\n');
    const QStringList upstream = templateSpdxLines(lines, firstCode);
    for (const QString& line : upstream) {
        if (line != newCopyrightLine.trimmed()) {
            out += line + QLatin1Char('\n');
        }
    }
    out += QLatin1Char('\n');
    for (int i = firstCode; i < metaStart; ++i) {
        out += lines[i] + QLatin1Char('\n');
    }
    out += metaLines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    for (int i = metaEnd + 1; i < lines.size(); ++i) {
        out += lines[i];
        if (i < lines.size() - 1) {
            out += QLatin1Char('\n');
        }
    }
    return out;
}

} // namespace AlgorithmScaffold
} // namespace PlasmaZones
