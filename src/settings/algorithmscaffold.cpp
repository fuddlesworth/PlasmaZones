// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "algorithmscaffold.h"

#include <PhosphorTiles/AutotileConstants.h>

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

/// The template's own `-- SPDX-*` lines: copyright lines first, then every
/// other SPDX tag (the order this project's files use). The copy keeps the
/// template's code substantially verbatim, so it is a derivative work: it
/// retains the upstream author's copyright and stays under the template's
/// own license.
QStringList templateSpdxLines(const QStringList& lines, int spdxHeaderEnd)
{
    QStringList copyrights;
    QStringList others;
    for (int i = 0; i < spdxHeaderEnd; ++i) {
        const QString t = lines[i].trimmed();
        if (t.startsWith(QLatin1String("-- SPDX-FileCopyrightText:"))) {
            copyrights += t;
        } else if (t.startsWith(QLatin1String("-- SPDX-"))) {
            others += t;
        }
    }
    return copyrights + others;
}

/// Our own blank-scaffold indent. The rewrite path passes the source file's
/// indent instead, so a copy keeps the formatting its author chose.
const QLatin1String ScaffoldFieldIndent("        ");

QString quotedField(const QString& indent, const QString& key, const QString& value)
{
    return indent + key + QStringLiteral(" = \"") + value + QStringLiteral("\",");
}

/// The run of whitespace @p line opens with.
QString leadingWhitespace(const QString& line)
{
    int i = 0;
    while (i < line.size() && line[i].isSpace()) {
        ++i;
    }
    return line.left(i);
}

/// Net brace delta of one line, ignoring braces inside quoted Luau strings
/// (double or single quoted, with backslash escapes) and after a `--` comment
/// marker. Line-scoped: short strings continued across lines
/// (backslash-newline, `\z`) are not handled; the bundled templates' metadata
/// uses none. A long bracket that stays open past the end of the line defeats
/// the scan outright, so opensUnclosedLongBracket() rejects that shape before
/// this delta is trusted.
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

/// True when @p line opens a Luau long bracket (`[[`, as a long string or, via
/// `--[[`, a long comment) that does not close on the same line.
///
/// Such a bracket makes every following line unreadable to a line-scoped scan:
/// its text is data, but braces in it still reach braceDelta(), which then
/// mistracks depth and can end the metadata walk early. A long bracket that
/// opens and closes on one line is harmless and stays supported. Only the
/// unclosed shape is detected, and rewriteMetadataNameId() rejects the file
/// rather than corrupting it.
bool opensUnclosedLongBracket(const QString& line)
{
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
            continue;
        }
        // `[[` opens a long bracket whether or not a `--` precedes it, so this
        // check runs before the comment break below.
        if (c == QLatin1Char('[') && i + 1 < line.size() && line[i + 1] == QLatin1Char('[')) {
            return line.indexOf(QLatin1String("]]"), i + 2) < 0;
        }
        if (c == QLatin1Char('-') && i + 1 < line.size() && line[i + 1] == QLatin1Char('-')) {
            // A `--` comment: only a long-comment opener matters past here.
            const int longOpen = line.indexOf(QLatin1String("[["), i + 2);
            if (longOpen < 0) {
                return false;
            }
            return line.indexOf(QLatin1String("]]"), longOpen + 2) < 0;
        }
    }
    return false;
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
    m += quotedField(ScaffoldFieldIndent, QStringLiteral("name"), displayName) + QLatin1Char('\n');
    m += quotedField(ScaffoldFieldIndent, QStringLiteral("id"), id) + QLatin1Char('\n');
    m += quotedField(ScaffoldFieldIndent, QStringLiteral("description"), QStringLiteral("Custom tiling algorithm"))
        + QLatin1Char('\n');
    m += QStringLiteral("        producesOverlappingZones = ") + b(caps.overlappingZones) + QStringLiteral(",\n");
    m += QStringLiteral("        supportsMasterCount = ") + b(caps.masterCount) + QStringLiteral(",\n");
    m += QStringLiteral("        supportsSplitRatio = ") + b(caps.splitRatio) + QStringLiteral(",\n");
    m += QStringLiteral("        defaultSplitRatio = ")
        + QString::number(PhosphorTiles::AutotileDefaults::DefaultSplitRatio) + QStringLiteral(",\n");
    // Write the same cap a script omitting the field would resolve to, so
    // deleting the line from a generated algorithm changes nothing.
    m += QStringLiteral("        defaultMaxWindows = ")
        + QString::number(PhosphorTiles::AutotileDefaults::ScriptedDefaultMaxWindows) + QStringLiteral(",\n");
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
            + QStringLiteral("    -- ctx.state for the next tile() run. Include a splitRatio key to have\n")
            + QStringLiteral("    -- the engine apply a new master/split ratio. Return nil to do nothing.\n")
            + QStringLiteral("    -- The table you return replaces the whole bag rather than merging\n")
            + QStringLiteral("    -- into it, so return every key you want to keep.\n")
            + QStringLiteral("    onWindowResized = function(state, resize)\n") + QStringLiteral("        return nil\n")
            + QStringLiteral("    end,\n");
    }

    content += QStringLiteral("}\n");
    return content;
}

QString rewriteMetadataNameId(const QString& content, const QString& displayName, const QString& id)
{
    QString normalized = content;
    normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    const QStringList lines = normalized.split(QLatin1Char('\n'));

    static const QRegularExpression metaRe(QStringLiteral(R"(^\s*metadata\s*=\s*\{)"));
    static const QRegularExpression metaOpenLineRe(QStringLiteral(R"(^\s*metadata\s*=\s*\{\s*$)"));
    int metaStart = -1;
    for (int i = 0; i < lines.size(); ++i) {
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
    // unrecognized shape and rejects the rewrite rather than corrupting it.
    static const QRegularExpression nameFieldRe(QStringLiteral(R"(^\s*name\s*=)"));
    static const QRegularExpression idFieldRe(QStringLiteral(R"(^\s*id\s*=)"));
    static const QRegularExpression nameLineRe(QStringLiteral(R"(^\s*name\s*=\s*"[^"]*"\s*,?\s*$)"));
    static const QRegularExpression idLineRe(QStringLiteral(R"(^\s*id\s*=\s*"[^"]*"\s*,?\s*$)"));

    QStringList metaLines;
    int depth = 0;
    int metaEnd = -1;
    bool sawName = false;
    bool sawId = false;
    // Indent to give an inserted name/id, taken from the table's first existing
    // top-level field so the insert matches its siblings. Stays empty for a
    // table with no other field, and the fallback below covers that.
    QString fieldIndent;
    for (int i = metaStart; i < lines.size(); ++i) {
        // A long bracket left open past this line turns the rest of the table
        // into text the line scan would misread as code. Reject rather than
        // rewrite on a depth we know is wrong.
        if (opensUnclosedLongBracket(lines[i])) {
            return QString();
        }
        const int depthAtLineStart = depth;
        depth += braceDelta(lines[i]);

        QString line = lines[i];
        if (i > metaStart && depthAtLineStart == 1) {
            const QString trimmed = line.trimmed();
            if (fieldIndent.isEmpty() && !trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('}'))) {
                fieldIndent = leadingWhitespace(line);
            }
            if (nameFieldRe.match(line).hasMatch()) {
                if (!nameLineRe.match(line).hasMatch()) {
                    return QString();
                }
                line = quotedField(leadingWhitespace(line), QStringLiteral("name"), displayName);
                sawName = true;
            } else if (idFieldRe.match(line).hasMatch()) {
                if (!idLineRe.match(line).hasMatch()) {
                    return QString();
                }
                line = quotedField(leadingWhitespace(line), QStringLiteral("id"), id);
                sawId = true;
            }
        }
        metaLines += line;

        if (depth <= 0) {
            // A negative depth means more closers than openers — malformed
            // input; leave metaEnd at -1 so the rewrite rejects it.
            if (depth == 0) {
                metaEnd = i;
            }
            break;
        }
    }
    if (metaEnd < 0) {
        return QString();
    }

    // A file without its own name/id line still needs ours — insert right
    // after the opening `metadata = {`, indented like the table's other
    // fields. A table with no other field to copy takes the opening line's
    // indent plus one four-space level.
    if (fieldIndent.isEmpty()) {
        fieldIndent = leadingWhitespace(lines[metaStart]) + QStringLiteral("    ");
    }
    if (!sawId) {
        metaLines.insert(1, quotedField(fieldIndent, QStringLiteral("id"), id));
    }
    if (!sawName) {
        metaLines.insert(1, quotedField(fieldIndent, QStringLiteral("name"), displayName));
    }

    QString out;
    for (int i = 0; i < metaStart; ++i) {
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

QString spliceTemplate(const QString& templateContent, const QString& newCopyrightLine, const QString& displayName,
                       const QString& id)
{
    const QString ownCopyright = newCopyrightLine.trimmed();
    // One line only: a multi-line header here would slip past the dedupe below
    // and leave the copy with two license identifiers.
    if (ownCopyright.contains(QLatin1Char('\n'))) {
        return QString();
    }

    const QString rewritten = rewriteMetadataNameId(templateContent, displayName, id);
    if (rewritten.isEmpty()) {
        return QString();
    }
    const QStringList lines = rewritten.split(QLatin1Char('\n'));
    const int firstCode = firstLinePastSpdxHeader(lines);

    // Header: the new owner's copyright, then the template's own SPDX lines
    // (its copyright and its license) — the copy is a derivative work, so it
    // credits the upstream author and stays under the upstream license. A
    // template that already carries the caller's exact copyright line (a copy
    // of a copy) does not get it twice.
    QString out = ownCopyright + QLatin1Char('\n');
    const QStringList upstream = templateSpdxLines(lines, firstCode);
    for (const QString& line : upstream) {
        if (line != ownCopyright) {
            out += line + QLatin1Char('\n');
        }
    }
    out += QLatin1Char('\n');
    for (int i = firstCode; i < lines.size(); ++i) {
        out += lines[i];
        if (i < lines.size() - 1) {
            out += QLatin1Char('\n');
        }
    }
    return out;
}

} // namespace AlgorithmScaffold
} // namespace PlasmaZones
