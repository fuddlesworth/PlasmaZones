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

/// Index of the first non-comment, non-blank line (skips the template's own
/// SPDX header so the caller can substitute a fresh one).
int firstCodeLine(const QStringList& lines)
{
    int i = 0;
    while (i < lines.size()) {
        const QString t = lines[i].trimmed();
        if (t.isEmpty() || t.startsWith(QLatin1String("--"))) {
            ++i;
            continue;
        }
        break;
    }
    return i;
}

QString quotedField(const QString& key, const QString& value)
{
    return QStringLiteral("        ") + key + QStringLiteral(" = \"") + value + QStringLiteral("\",");
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

QString spliceTemplate(const QString& templateContent, const QString& newHeader, const QString& displayName,
                       const QString& id)
{
    const QStringList lines = templateContent.split(QLatin1Char('\n'));
    const int firstCode = firstCodeLine(lines);

    static const QRegularExpression metaRe(QStringLiteral(R"(^\s*metadata\s*=\s*\{)"));
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

    // Walk the metadata table, tracking brace depth relative to it. Only
    // depth-1 lines are top-level metadata fields — customParams entries nest
    // deeper and must keep their own `name` keys untouched.
    static const QRegularExpression nameRe(QStringLiteral(R"(^\s*name\s*=)"));
    static const QRegularExpression idRe(QStringLiteral(R"(^\s*id\s*=)"));

    QStringList metaLines;
    int depth = 0;
    int metaEnd = -1;
    bool sawName = false;
    bool sawId = false;
    for (int i = metaStart; i < lines.size(); ++i) {
        const int depthAtLineStart = depth;
        for (const QChar c : lines[i]) {
            if (c == QLatin1Char('{')) {
                ++depth;
            } else if (c == QLatin1Char('}')) {
                --depth;
            }
        }

        QString line = lines[i];
        if (i > metaStart && depthAtLineStart == 1) {
            if (nameRe.match(line).hasMatch()) {
                line = quotedField(QStringLiteral("name"), displayName);
                sawName = true;
            } else if (idRe.match(line).hasMatch()) {
                line = quotedField(QStringLiteral("id"), id);
                sawId = true;
            }
        }
        metaLines += line;

        if (depth == 0) {
            metaEnd = i;
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

    QString out = newHeader + QStringLiteral("\n");
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
