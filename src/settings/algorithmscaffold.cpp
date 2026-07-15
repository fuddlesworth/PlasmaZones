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
constexpr QLatin1String kScaffoldFieldIndent{"        "};

/// A `key = "value"` field line closed by @p terminator and trailing @p comment.
/// The rewrite path passes the source line's own terminator and comment back so
/// a copy keeps the punctuation and the note its author wrote; only the value
/// changes. @p value reaches here through sanitizeMetadataString, which strips
/// the quote and backslash characters that could otherwise escape the literal.
QString quotedField(const QString& indent, const QString& key, const QString& value, const QString& terminator,
                    const QString& comment)
{
    return indent + key + QStringLiteral(" = \"") + value + QLatin1Char('"') + terminator + comment;
}

QString quotedField(const QString& indent, const QString& key, const QString& value)
{
    return quotedField(indent, key, value, QStringLiteral(","), QString());
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

/// No Luau long bracket is open.
constexpr int kNoLongBracket = -1;

/// If a Luau long-bracket opener (`[`, N `=`, `[`) starts at @p pos in @p line,
/// return its level N and set @p pastOpener to the index just past it.
/// Otherwise return kNoLongBracket. An out-of-range @p pos simply does not
/// match, so callers need not bounds-check before asking.
int matchLongBracketOpen(const QString& line, int pos, int& pastOpener)
{
    if (pos < 0 || pos >= line.size() || line[pos] != QLatin1Char('[')) {
        return kNoLongBracket;
    }
    int i = pos + 1;
    int level = 0;
    while (i < line.size() && line[i] == QLatin1Char('=')) {
        ++level;
        ++i;
    }
    if (i >= line.size() || line[i] != QLatin1Char('[')) {
        return kNoLongBracket;
    }
    pastOpener = i + 1;
    return level;
}

/// Index just past the `]`, N `=`, `]` closer for a level-@p level long bracket
/// at or after @p from, or -1 when the line does not close it.
int findLongBracketClose(const QString& line, int level, int from)
{
    const QString closer = QLatin1Char(']') + QString(level, QLatin1Char('=')) + QLatin1Char(']');
    const int at = line.indexOf(closer, from);
    return at < 0 ? -1 : at + closer.size();
}

/// Net brace delta of the CODE on one line, given the long-bracket state on
/// entry, updating @p longBracketLevel for the next line.
///
/// Braces are counted only where they are code. Text is skipped in all three
/// forms Luau has: quoted strings (double, single, or backtick interpolated,
/// with backslash escapes), `--` line comments, and long brackets at any level
/// (`[[`, `[=[`, ...), as long strings or, spelled `--[[`, long comments. A
/// long bracket may open and close on one line, span lines, or repeat, and
/// @p longBracketLevel carries an open one across the line boundary.
///
/// @p startDepth is the brace depth the line opens at, so the scan can speak in
/// depths the caller shares rather than line-relative ones. When @p codeOnly is
/// non-null it collects the line's code at depth 1 exactly — the metadata
/// table's own fields, wherever on the line they fall. Text is elided, so is
/// anything nested deeper, and so is anything past the table's own closer. A
/// caller can ask "is there a field of THIS table here" without a string, a
/// comment, a nested table's keys, or the enclosing table's code answering.
///
/// @p openString, when non-null, is set if a short or interpolated string is
/// still open at the end of the line. That string runs past where this scan can
/// follow, so a caller reads it as "unrecognized shape" rather than resume
/// mid-literal. One shape this does NOT catch, because it is not detectable
/// from one line: a backtick literal reached from inside its own interpolation
/// (`` `{"`"}` ``), which ends the literal early here. No bundled template's
/// metadata uses one.
int braceDelta(const QString& line, int& longBracketLevel, int startDepth = 0, QString* codeOnly = nullptr,
               bool* openString = nullptr)
{
    int depth = startDepth;
    // Set once the metadata table closes on this line. Everything after its
    // closer belongs to the enclosing table, so it is not ours to collect even
    // though it sits at depth 1 again.
    bool closed = false;
    int i = 0;

    // A long bracket left open by an earlier line: everything up to its closer
    // is text.
    if (longBracketLevel != kNoLongBracket) {
        const int past = findLongBracketClose(line, longBracketLevel, 0);
        if (past < 0) {
            return 0;
        }
        longBracketLevel = kNoLongBracket;
        i = past;
    }

    QChar quote;
    for (; i < line.size(); ++i) {
        const QChar c = line[i];
        if (!quote.isNull()) {
            if (c == QLatin1Char('\\')) {
                ++i;
            } else if (c == quote) {
                quote = QChar();
            }
            continue;
        }
        // Luau has three string forms, and a backtick interpolated string is
        // the one that looks least like text: `a } b` carries a literal `}`
        // that would otherwise close the table early. An interpolation that
        // opens and closes on this line is skipped with it, braces and all. One
        // that spans lines cannot be, and the openString report below is how
        // that shape gets refused instead of miscounted.
        if (c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QLatin1Char('`')) {
            quote = c;
            continue;
        }
        if (c == QLatin1Char('-') && i + 1 < line.size() && line[i + 1] == QLatin1Char('-')) {
            // `--[[` opens a long comment, which can close and let code resume.
            // A plain `--` runs to end of line.
            int pastOpener = 0;
            const int level = matchLongBracketOpen(line, i + 2, pastOpener);
            if (level == kNoLongBracket) {
                return depth - startDepth;
            }
            const int past = findLongBracketClose(line, level, pastOpener);
            if (past < 0) {
                longBracketLevel = level;
                return depth - startDepth;
            }
            i = past - 1;
            continue;
        }
        int pastOpener = 0;
        const int level = matchLongBracketOpen(line, i, pastOpener);
        if (level != kNoLongBracket) {
            const int past = findLongBracketClose(line, level, pastOpener);
            if (past < 0) {
                longBracketLevel = level;
                return depth - startDepth;
            }
            i = past - 1;
            continue;
        }
        // Record only what sits at depth 1: the metadata table's own fields. A
        // nested table's contents are deeper and elided, so
        // `customParams = { name = "x" }` cannot read as a trailing `name`
        // field, while a real one after that table's closer still does.
        if (c == QLatin1Char('{')) {
            if (codeOnly && depth == 1 && !closed) {
                codeOnly->append(c);
            }
            ++depth;
        } else if (c == QLatin1Char('}')) {
            --depth;
            if (depth < 1) {
                closed = true;
            }
            if (codeOnly && depth == 1 && !closed) {
                codeOnly->append(c);
            }
        } else if (codeOnly && depth == 1 && !closed) {
            codeOnly->append(c);
        }
    }
    // A short or interpolated string still open here runs past this line, and
    // this scan reads one line at a time. Report it so the caller can refuse
    // the file rather than resume in the middle of a string literal reading
    // text as code. Three shapes land here and they are one shape physically:
    // a backslash-newline continuation, a `\z` continuation, and a backtick
    // literal whose interpolation spans lines (the string sections cannot, but
    // the expression between them is lexed as ordinary code and may).
    if (openString && !quote.isNull()) {
        *openString = true;
    }
    return depth - startDepth;
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
    m += quotedField(kScaffoldFieldIndent, QStringLiteral("name"), displayName) + QLatin1Char('\n');
    m += quotedField(kScaffoldFieldIndent, QStringLiteral("id"), id) + QLatin1Char('\n');
    m += quotedField(kScaffoldFieldIndent, QStringLiteral("description"), QStringLiteral("Custom tiling algorithm"))
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

    // Normalize rather than require a trailing newline: the separator is this
    // function's to get right, and a caller that omits one should not silently
    // produce a scaffold whose header runs into its first line of code.
    QString content = header.trimmed() + QStringLiteral("\n\nlocal pluau = pluau\n\nreturn pluau.algorithm {\n") + m
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
    // Find the table, carrying long-bracket state so a `metadata = {` written
    // inside a `--[[ ]]` doc block is read as the text it is. Taking it would be
    // the quiet kind of wrong: the commented-out table rewrites clean, both saw
    // flags come back true, and the real table below keeps the source's id for
    // Luau's last-wins to hand back. The `--` forms need no such care, since
    // this pattern only matches `metadata` leading its line.
    int metaStart = -1;
    int scanLongBracket = kNoLongBracket;
    for (int i = 0; i < lines.size(); ++i) {
        const bool lineStartsInText = scanLongBracket != kNoLongBracket;
        bool openString = false;
        braceDelta(lines[i], scanLongBracket, 0, nullptr, &openString);
        if (openString) {
            return QString();
        }
        if (!lineStartsInText && metaRe.match(lines[i]).hasMatch()) {
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
    //
    // The value may use either quote style, and the line may close with either
    // of Luau's field separators and carry a trailing comment: the bundled
    // templates all use the one canonical form, but duplicateAlgorithm runs
    // this over scripts a user wrote by hand, and rejecting their punctuation
    // fails the whole copy. Terminator and comment are captured so the rewrite
    // puts them back.
    //
    // Only a long-bracket opener (`--[[`, `--[=[`, ...) is excluded, because
    // that comment can close mid-line and leave a second field live where this
    // line-anchored read would never look for one. A `--[` that opens no long
    // bracket is an ordinary line comment and runs to the end of the line, so
    // it is accepted like any other.
    static const QRegularExpression nameFieldRe(QStringLiteral(R"(^\s*name\s*=)"));
    static const QRegularExpression idFieldRe(QStringLiteral(R"(^\s*id\s*=)"));
    static const QRegularExpression nameLineRe(
        QStringLiteral(R"(^\s*name\s*=\s*(?:"[^"]*"|'[^']*')\s*([,;]?)((?:\s*--(?!\[=*\[).*)?)$)"));
    static const QRegularExpression idLineRe(
        QStringLiteral(R"(^\s*id\s*=\s*(?:"[^"]*"|'[^']*')\s*([,;]?)((?:\s*--(?!\[=*\[).*)?)$)"));
    // A name/id key anywhere on the line, not just at its start. The two
    // anchored patterns above only see a field that leads its line, so without
    // this a trailing `a = 1, name = "x",` would be neither rewritten nor
    // rejected: we would insert our own pair and Luau, which takes the last
    // duplicate key, would keep the template's. Both of Luau's field separators
    // count, since `a = 1; name = "x"` is the same trap as the comma form.
    // Matched against the line's top-level code only, so neither a
    // `description = "name = x"` value nor a nested table's own `name` key can
    // trip it. A bracketed key (`["name"] = ...`) is the same Luau key as the
    // bare one, and its string is elided from the match subject, so any
    // bracketed key of this table is rejected rather than read: no bundled
    // template uses one, and guessing is how the pair silently duplicates.
    static const QRegularExpression anyNameOrIdFieldRe(QStringLiteral(R"((?:^|[,;])\s*(?:(?:name|id)\s*=|\[))"));

    QStringList metaLines;
    int depth = 0;
    int metaEnd = -1;
    bool sawName = false;
    bool sawId = false;
    // Indent to give an inserted name/id, taken from the table's first existing
    // top-level field so the insert matches its siblings. Stays empty for a
    // table with no other field, and the fallback below covers that.
    QString fieldIndent;
    // Carries an unclosed long bracket across the line boundary, so the text
    // inside a multi-line long string or comment is never read as code.
    int longBracketLevel = kNoLongBracket;
    for (int i = metaStart; i < lines.size(); ++i) {
        const int depthAtLineStart = depth;
        // A line that opens inside a long bracket is that bracket's text, so
        // a `name = "..."` in it is content rather than a field to rewrite.
        const bool lineStartsInText = longBracketLevel != kNoLongBracket;
        QString codeOnly;
        bool openString = false;
        depth += braceDelta(lines[i], longBracketLevel, depthAtLineStart, &codeOnly, &openString);
        if (openString) {
            return QString();
        }

        QString line = lines[i];
        bool rewrote = false;
        // The anchored rewrite only fits a field that LEADS its line, in code.
        if (i > metaStart && depthAtLineStart == 1 && !lineStartsInText) {
            const QString trimmed = line.trimmed();
            if (fieldIndent.isEmpty() && !trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('}'))) {
                fieldIndent = leadingWhitespace(line);
            }
            if (nameFieldRe.match(line).hasMatch()) {
                const QRegularExpressionMatch m = nameLineRe.match(line);
                if (!m.hasMatch()) {
                    return QString();
                }
                line = quotedField(leadingWhitespace(line), QStringLiteral("name"), displayName, m.captured(1),
                                   m.captured(2));
                sawName = true;
                rewrote = true;
            } else if (idFieldRe.match(line).hasMatch()) {
                const QRegularExpressionMatch m = idLineRe.match(line);
                if (!m.hasMatch()) {
                    return QString();
                }
                line = quotedField(leadingWhitespace(line), QStringLiteral("id"), id, m.captured(1), m.captured(2));
                sawId = true;
                rewrote = true;
            }
        }
        // Any OTHER field of the table on this line, wherever it falls: after a
        // sibling, after a nested table's closer, after a long bracket's
        // closer, on a line that opened deeper, or ahead of the table's own
        // closer. None can be rewritten in place, and leaving one lets Luau's
        // last-wins keep the template's value.
        if (i > metaStart && !rewrote && anyNameOrIdFieldRe.match(codeOnly).hasMatch()) {
            return QString();
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
    // and leave the copy with two license identifiers. It must also be the
    // copyright line it claims to be — the dedupe compares it against the
    // template's own SPDX lines, and anything else silently yields a copy whose
    // first line is not a copyright at all.
    if (ownCopyright.contains(QLatin1Char('\n'))
        || !ownCopyright.startsWith(QLatin1String("-- SPDX-FileCopyrightText:"))) {
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
