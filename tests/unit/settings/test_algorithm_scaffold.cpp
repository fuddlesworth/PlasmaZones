// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../../src/settings/algorithmscaffold.h"

#include <QDir>
#include <QFile>
#include <QtTest>

using namespace PlasmaZones::AlgorithmScaffold;

namespace {

// A template shaped like the bundled algorithms: SPDX header, module local,
// metadata with capability flags and a nested customParams table whose
// entries carry their own `name` keys, then the tile function.
const QString kTemplate = QStringLiteral(R"(-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

local pluau = pluau

return pluau.algorithm {
    metadata = {
        name = "Theater",
        id = "theater",
        description = "Spotlight layout",
        supportsSingleWindow = true,
        retileOnFocus = true,
        supportsScriptState = true,
        defaultMaxWindows = 6,
        masterZoneIndex = 0,
        customParams = {
            { name = "widthRatio", type = "number", default = 0.6, min = 0.2, max = 1.0,
                description = "Spotlight width" },
        },
    },

    tile = function(ctx)
        return pluau.fillArea(ctx.area, ctx.windowCount)
    end,
}
)");

// A template copy takes only the new owner's copyright line and inherits the
// template's license; the blank scaffold is our own code and takes both.
const QString kCopyright = QStringLiteral("-- SPDX-FileCopyrightText: 2026 <your name>");
const QString kHeader = kCopyright + QStringLiteral("\n-- SPDX-License-Identifier: GPL-3.0-or-later\n");

} // namespace

class TestAlgorithmScaffold : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void spliceRewritesNameAndId();
    void spliceToleratesTrailingNewlineOnCopyright();
    void spliceRejectsMultiLineCopyright();
    void rewriteKeepsHeaderAndOtherTables();
    void spliceInheritsTemplateLicense();
    void spliceDoesNotRepeatOwnCopyright();
    void spliceCarriesEveryUpstreamCopyright();
    void spliceHandlesTemplateWithoutCopyright();
    void splicePreservesCapabilityMetadata();
    void spliceLeavesCustomParamNamesAlone();
    void spliceInsertsMissingNameAndId();
    void spliceRejectsTemplateWithoutMetadata();
    void spliceRejectsSingleLineMetadata();
    void spliceRejectsInlineFieldsOnOpeningLine();
    void spliceRejectsMultiFieldNameLine();
    void spliceRejectsUnquotedNameValue();
    void spliceIgnoresBracesInsideStrings();
    void splicePreservesLeadingDocComment();
    void spliceNormalizesCrlf();
    void spliceAllBundledAlgorithms();
    void blankScaffoldEmitsRequestedFlags();
    void blankScaffoldOmitsUnsetNewFlags();
    void sanitizeStripsBreakoutCharacters();
};

void TestAlgorithmScaffold::spliceRewritesNameAndId()
{
    const QString out =
        spliceTemplate(kTemplate, kCopyright, QStringLiteral("My Theater"), QStringLiteral("my-theater"));
    QVERIFY(out.contains(QStringLiteral("        name = \"My Theater\",")));
    QVERIFY(out.contains(QStringLiteral("        id = \"my-theater\",")));
    QVERIFY(!out.contains(QStringLiteral("name = \"Theater\"")));
    QVERIFY(!out.contains(QStringLiteral("id = \"theater\",")));
    // The copy is a derivative work: the new owner's copyright leads, the
    // template's copyright follows, and the template's own license applies.
    QCOMPARE(out.count(QStringLiteral("SPDX-License-Identifier")), 1);
    QCOMPARE(out.count(QStringLiteral("SPDX-FileCopyrightText")), 2);
    QVERIFY(
        out.startsWith(QStringLiteral("-- SPDX-FileCopyrightText: 2026 <your name>\n"
                                      "-- SPDX-FileCopyrightText: 2026 fuddlesworth\n"
                                      "-- SPDX-License-Identifier: GPL-3.0-or-later\n"
                                      "\n")));
}

void TestAlgorithmScaffold::spliceToleratesTrailingNewlineOnCopyright()
{
    // The contract accepts the copyright line with or without a trailing
    // newline, and both must produce identical output.
    const QString bare = spliceTemplate(kTemplate, kCopyright, QStringLiteral("My Copy"), QStringLiteral("my-copy"));
    const QString withNewline = spliceTemplate(kTemplate, kCopyright + QStringLiteral("\n"), QStringLiteral("My Copy"),
                                               QStringLiteral("my-copy"));
    QVERIFY(!bare.isEmpty());
    QCOMPARE(withNewline, bare);
}

void TestAlgorithmScaffold::spliceRejectsMultiLineCopyright()
{
    // A full header here would slip past the copy-of-a-copy dedupe and leave
    // the copy with two license identifiers, so it is rejected outright.
    QVERIFY(spliceTemplate(kTemplate, kHeader, QStringLiteral("My Copy"), QStringLiteral("my-copy")).isEmpty());
}

void TestAlgorithmScaffold::rewriteKeepsHeaderAndOtherTables()
{
    // The duplicate path rewrites metadata in place: the header stays put, and
    // a same-shaped table elsewhere in the file must not be mistaken for it.
    const QString withOtherTable = QStringLiteral(
        "-- SPDX-FileCopyrightText: 2026 someone\n"
        "-- SPDX-License-Identifier: MIT\n"
        "\n"
        "local presets = {\n"
        "    name = \"wide\",\n"
        "    id = \"wide\",\n"
        "}\n"
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = \"Real\",\n"
        "        id = \"real\",\n"
        "    },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    const QString out =
        rewriteMetadataNameId(withOtherTable, QStringLiteral("Real (Copy)"), QStringLiteral("real-copy"));
    // The unrelated table is untouched.
    QVERIFY(
        out.contains(QStringLiteral("local presets = {\n"
                                    "    name = \"wide\",\n"
                                    "    id = \"wide\",\n"
                                    "}")));
    // The metadata table is rewritten.
    QVERIFY(out.contains(QStringLiteral("        name = \"Real (Copy)\",")));
    QVERIFY(out.contains(QStringLiteral("        id = \"real-copy\",")));
    // The source's own header carries over verbatim: a duplicate is the same
    // author's code, so nothing about its licensing changes.
    QVERIFY(
        out.startsWith(QStringLiteral("-- SPDX-FileCopyrightText: 2026 someone\n"
                                      "-- SPDX-License-Identifier: MIT\n")));
    QCOMPARE(out.count(QStringLiteral("SPDX-FileCopyrightText")), 1);
}

void TestAlgorithmScaffold::spliceInheritsTemplateLicense()
{
    // The copy carries the template's code, so it stays under the template's
    // license rather than being restamped with the caller's.
    QString lgpl = kTemplate;
    lgpl.replace(QStringLiteral("GPL-3.0-or-later"), QStringLiteral("LGPL-2.1-or-later"));
    const QString out = spliceTemplate(lgpl, kCopyright, QStringLiteral("My Copy"), QStringLiteral("my-copy"));
    QVERIFY(out.contains(QStringLiteral("-- SPDX-License-Identifier: LGPL-2.1-or-later")));
    QCOMPARE(out.count(QStringLiteral("SPDX-License-Identifier")), 1);
}

void TestAlgorithmScaffold::spliceDoesNotRepeatOwnCopyright()
{
    // A copy of a copy already carries the caller's exact copyright line; it
    // must not accumulate a second one per generation.
    const QString gen1 = spliceTemplate(kTemplate, kCopyright, QStringLiteral("Gen One"), QStringLiteral("gen-one"));
    QCOMPARE(gen1.count(QStringLiteral("SPDX-FileCopyrightText")), 2);
    const QString gen2 = spliceTemplate(gen1, kCopyright, QStringLiteral("Gen Two"), QStringLiteral("gen-two"));
    QCOMPARE(gen2.count(QStringLiteral("SPDX-FileCopyrightText")), 2);
    const QString gen3 = spliceTemplate(gen2, kCopyright, QStringLiteral("Gen Three"), QStringLiteral("gen-three"));
    QCOMPARE(gen3.count(QStringLiteral("SPDX-FileCopyrightText")), 2);
    QCOMPARE(gen3.count(QStringLiteral("SPDX-License-Identifier")), 1);
}

void TestAlgorithmScaffold::spliceCarriesEveryUpstreamCopyright()
{
    QString coAuthored = kTemplate;
    coAuthored.replace(QStringLiteral("-- SPDX-FileCopyrightText: 2026 fuddlesworth"),
                       QStringLiteral("-- SPDX-FileCopyrightText: 2026 fuddlesworth\n"
                                      "-- SPDX-FileCopyrightText: 2026 someone else"));
    const QString out = spliceTemplate(coAuthored, kCopyright, QStringLiteral("My Copy"), QStringLiteral("my-copy"));
    QVERIFY(
        out.startsWith(QStringLiteral("-- SPDX-FileCopyrightText: 2026 <your name>\n"
                                      "-- SPDX-FileCopyrightText: 2026 fuddlesworth\n"
                                      "-- SPDX-FileCopyrightText: 2026 someone else\n"
                                      "-- SPDX-License-Identifier: GPL-3.0-or-later\n")));
}

void TestAlgorithmScaffold::spliceHandlesTemplateWithoutCopyright()
{
    QString noCopyright = kTemplate;
    noCopyright.replace(QStringLiteral("-- SPDX-FileCopyrightText: 2026 fuddlesworth\n"), QString());
    const QString out = spliceTemplate(noCopyright, kCopyright, QStringLiteral("My Copy"), QStringLiteral("my-copy"));
    QCOMPARE(out.count(QStringLiteral("SPDX-FileCopyrightText")), 1);
    QVERIFY(
        out.startsWith(QStringLiteral("-- SPDX-FileCopyrightText: 2026 <your name>\n"
                                      "-- SPDX-License-Identifier: GPL-3.0-or-later\n"
                                      "\n")));
}

void TestAlgorithmScaffold::splicePreservesCapabilityMetadata()
{
    const QString out =
        spliceTemplate(kTemplate, kCopyright, QStringLiteral("My Theater"), QStringLiteral("my-theater"));
    // The regression this guards: the old splicer replaced the whole metadata
    // table with one rebuilt from four wizard bools, stripping the flags the
    // template's code depends on.
    QVERIFY(out.contains(QStringLiteral("supportsSingleWindow = true")));
    QVERIFY(out.contains(QStringLiteral("retileOnFocus = true")));
    QVERIFY(out.contains(QStringLiteral("supportsScriptState = true")));
    QVERIFY(out.contains(QStringLiteral("defaultMaxWindows = 6")));
    QVERIFY(out.contains(QStringLiteral("masterZoneIndex = 0")));
    QVERIFY(out.contains(QStringLiteral("description = \"Spotlight layout\"")));
    // Body untouched.
    QVERIFY(out.contains(QStringLiteral("tile = function(ctx)")));
    QVERIFY(out.contains(QStringLiteral("local pluau = pluau")));
}

void TestAlgorithmScaffold::spliceLeavesCustomParamNamesAlone()
{
    const QString out =
        spliceTemplate(kTemplate, kCopyright, QStringLiteral("My Theater"), QStringLiteral("my-theater"));
    // The nested customParams entry has its own `name` key at depth 2 and 3;
    // only the top-level metadata `name` may be rewritten.
    QVERIFY(out.contains(QStringLiteral("{ name = \"widthRatio\", type = \"number\"")));
}

void TestAlgorithmScaffold::spliceInsertsMissingNameAndId()
{
    const QString noIdent = QStringLiteral(
        "local pluau = pluau\n"
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        description = \"Bare\",\n"
        "    },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    const QString out = spliceTemplate(noIdent, kCopyright, QStringLiteral("Bare Copy"), QStringLiteral("bare-copy"));
    QVERIFY(out.contains(QStringLiteral("        name = \"Bare Copy\",")));
    QVERIFY(out.contains(QStringLiteral("        id = \"bare-copy\",")));
    // Inserted inside the metadata table, before its closing brace.
    QVERIFY(out.indexOf(QStringLiteral("name = \"Bare Copy\"")) < out.indexOf(QStringLiteral("description")));
}

void TestAlgorithmScaffold::spliceRejectsTemplateWithoutMetadata()
{
    QVERIFY(
        spliceTemplate(QStringLiteral("return {}"), kCopyright, QStringLiteral("X"), QStringLiteral("x")).isEmpty());
    QVERIFY(spliceTemplate(QString(), kCopyright, QStringLiteral("X"), QStringLiteral("x")).isEmpty());
    // Unterminated metadata table.
    QVERIFY(spliceTemplate(QStringLiteral("metadata = {\n name = \"a\",\n"), kCopyright, QStringLiteral("X"),
                           QStringLiteral("x"))
                .isEmpty());
    // Malformed table whose depth goes negative (more closers than openers).
    QVERIFY(spliceTemplate(QStringLiteral("metadata = {\n    }},\n    name = \"a\",\n}\n"), kCopyright,
                           QStringLiteral("X"), QStringLiteral("x"))
                .isEmpty());
}

void TestAlgorithmScaffold::spliceRejectsSingleLineMetadata()
{
    // A table that opens and closes on one line has no per-field lines; the
    // splice must reject it rather than append name/id after the closing
    // brace (which would be corrupt Luau).
    const QString oneLiner = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = { name = \"A\", id = \"a\" },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    QVERIFY(spliceTemplate(oneLiner, kCopyright, QStringLiteral("X"), QStringLiteral("x")).isEmpty());
}

void TestAlgorithmScaffold::spliceRejectsInlineFieldsOnOpeningLine()
{
    // A multi-line table whose opening line carries inline fields would keep
    // the original name/id strings; the splice must reject the shape.
    const QString hybrid = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = { name = \"A\",\n"
        "        id = \"a\",\n"
        "    },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    QVERIFY(spliceTemplate(hybrid, kCopyright, QStringLiteral("X"), QStringLiteral("x")).isEmpty());
}

void TestAlgorithmScaffold::spliceRejectsMultiFieldNameLine()
{
    // A depth-1 name/id line carrying a second field would lose that field
    // if rewritten wholesale; the splice must reject the shape instead.
    const QString multiField = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = \"A\", description = \"D\",\n"
        "    },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    QVERIFY(spliceTemplate(multiField, kCopyright, QStringLiteral("X"), QStringLiteral("x")).isEmpty());

    // Same shape on the id line — the symmetric reject branch.
    const QString multiFieldId = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        id = \"a\", description = \"D\",\n"
        "    },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    QVERIFY(spliceTemplate(multiFieldId, kCopyright, QStringLiteral("X"), QStringLiteral("x")).isEmpty());
}

void TestAlgorithmScaffold::spliceRejectsUnquotedNameValue()
{
    const QString unquoted = QStringLiteral(
        "local n = \"N\"\n"
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = n,\n"
        "        id = \"x\",\n"
        "    },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    QVERIFY(spliceTemplate(unquoted, kCopyright, QStringLiteral("X"), QStringLiteral("x")).isEmpty());

    // Same shape on the id line — the symmetric reject branch.
    const QString unquotedId = QStringLiteral(
        "local n = \"N\"\n"
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = \"N\",\n"
        "        id = n,\n"
        "    },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    QVERIFY(spliceTemplate(unquotedId, kCopyright, QStringLiteral("X"), QStringLiteral("x")).isEmpty());
}

void TestAlgorithmScaffold::spliceIgnoresBracesInsideStrings()
{
    // Braces inside quoted values (or comments) must not desync the depth
    // scan; the name/id lines after them still get rewritten.
    const QString braced = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        description = \"a } weird { description\",\n"
        "        summary = \"escaped \\\" and a } brace\",\n"
        "        hint = 'single } quoted { too',\n"
        "        -- a comment mentioning a } brace\n"
        "        name = \"Braced\",\n"
        "        id = \"braced\",\n"
        "    },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    const QString out = spliceTemplate(braced, kCopyright, QStringLiteral("My Braced"), QStringLiteral("my-braced"));
    QVERIFY(out.contains(QStringLiteral("        name = \"My Braced\",")));
    QVERIFY(out.contains(QStringLiteral("        id = \"my-braced\",")));
    QVERIFY(out.contains(QStringLiteral("description = \"a } weird { description\"")));
    QVERIFY(out.contains(QStringLiteral("summary = \"escaped \\\" and a } brace\"")));
    QVERIFY(out.contains(QStringLiteral("hint = 'single } quoted { too'")));
    QVERIFY(out.contains(QStringLiteral("tile = function(ctx) return {} end,")));
}

void TestAlgorithmScaffold::splicePreservesLeadingDocComment()
{
    const QString documented = QStringLiteral(
        "-- SPDX-FileCopyrightText: 2026 fuddlesworth\n"
        "-- SPDX-License-Identifier: LGPL-2.1-or-later\n"
        "\n"
        "-- How this layout works, in two lines\n"
        "-- of explanatory prose a user needs.\n"
        "local pluau = pluau\n"
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = \"Doc\",\n"
        "        id = \"doc\",\n"
        "    },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    const QString out = spliceTemplate(documented, kCopyright, QStringLiteral("My Doc"), QStringLiteral("my-doc"));
    // The upstream copyright, the template's license, and the doc block all
    // survive.
    QVERIFY(out.startsWith(kCopyright));
    QCOMPARE(out.count(QStringLiteral("SPDX-License-Identifier")), 1);
    QVERIFY(out.contains(QStringLiteral("LGPL-2.1-or-later")));
    QCOMPARE(out.count(QStringLiteral("SPDX-FileCopyrightText")), 2);
    QVERIFY(
        out.contains(QStringLiteral("-- How this layout works, in two lines\n"
                                    "-- of explanatory prose a user needs.")));
}

void TestAlgorithmScaffold::spliceNormalizesCrlf()
{
    QString crlf = kTemplate;
    crlf.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    const QString out = spliceTemplate(crlf, kCopyright, QStringLiteral("My Theater"), QStringLiteral("my-theater"));
    QVERIFY(!out.isEmpty());
    QVERIFY(!out.contains(QLatin1Char('\r')));
    QVERIFY(out.contains(QStringLiteral("        name = \"My Theater\",")));
}

void TestAlgorithmScaffold::spliceAllBundledAlgorithms()
{
    // Pin the load-bearing formatting assumption against the real bundled
    // set: every algorithm must splice cleanly, get the new name/id, and
    // keep every non-header, non-name/non-id line verbatim (doc comments,
    // capability flags, customParams, the whole body).
    const QDir dir(QStringLiteral(P_SOURCE_DIR "/data/algorithms"));
    const QStringList files = dir.entryList({QStringLiteral("*.luau")}, QDir::Files);
    QVERIFY(!files.isEmpty());

    static const QRegularExpression topLevelNameOrId(QStringLiteral(R"(^\s*(name|id)\s*=)"));
    for (const QString& fileName : files) {
        QFile f(dir.filePath(fileName));
        QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(fileName));
        const QString content = QString::fromUtf8(f.readAll());

        const QString out = spliceTemplate(content, kCopyright, QStringLiteral("My Copy"), QStringLiteral("my-copy"));
        QVERIFY2(!out.isEmpty(), qPrintable(fileName));
        QVERIFY2(out.contains(QStringLiteral("        name = \"My Copy\",")), qPrintable(fileName));
        QVERIFY2(out.contains(QStringLiteral("        id = \"my-copy\",")), qPrintable(fileName));
        // The new owner's line plus the bundled author's carried-over one.
        QCOMPARE(out.count(QStringLiteral("SPDX-FileCopyrightText")), 2);
        QCOMPARE(out.count(QStringLiteral("SPDX-License-Identifier")), 1);

        // Every original line survives except the SPDX header and the
        // top-level name/id lines. The anchored regex is a conservative
        // filter: it exempts any line that starts with `name =` / `id =` at
        // any depth (bundled customParams entries open with `{ name = ...`,
        // which it does not match), so it can only under-check a nested
        // own-line name, never fail on a preserved one.
        const QStringList lines = content.split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(QLatin1String("-- SPDX-")) || topLevelNameOrId.match(line).hasMatch()) {
                continue;
            }
            QVERIFY2(out.contains(line), qPrintable(fileName + QStringLiteral(": lost line: ") + line));
        }
    }
}

void TestAlgorithmScaffold::blankScaffoldEmitsRequestedFlags()
{
    Capabilities caps;
    caps.masterCount = true;
    caps.splitRatio = true;
    caps.overlappingZones = true;
    caps.memory = true;
    caps.scriptState = true;
    caps.singleWindow = true;
    caps.retileOnFocus = true;
    const QString out = buildBlankScaffold(kHeader, QStringLiteral("Mine"), QStringLiteral("mine"), caps);
    QVERIFY(out.contains(QStringLiteral("name = \"Mine\"")));
    QVERIFY(out.contains(QStringLiteral("id = \"mine\"")));
    QVERIFY(out.contains(QStringLiteral("supportsMasterCount = true")));
    QVERIFY(out.contains(QStringLiteral("supportsSplitRatio = true")));
    QVERIFY(out.contains(QStringLiteral("producesOverlappingZones = true")));
    QVERIFY(out.contains(QStringLiteral("supportsMemory = true")));
    QVERIFY(out.contains(QStringLiteral("supportsScriptState = true")));
    QVERIFY(out.contains(QStringLiteral("supportsSingleWindow = true")));
    QVERIFY(out.contains(QStringLiteral("retileOnFocus = true")));
    // Script state brings the resize-hook stub along.
    QVERIFY(out.contains(QStringLiteral("onWindowResized = function(state, resize)")));
    // Tile body guards tiny areas.
    QVERIFY(out.contains(QStringLiteral("pluau.guardArea(ctx.area, ctx.windowCount)")));
}

void TestAlgorithmScaffold::blankScaffoldOmitsUnsetNewFlags()
{
    const QString out = buildBlankScaffold(kHeader, QStringLiteral("Mine"), QStringLiteral("mine"), Capabilities{});
    // The legacy four are always written out explicitly; the newer opt-in
    // flags are omitted when unset (absent means false).
    QVERIFY(out.contains(QStringLiteral("supportsMemory = false")));
    QVERIFY(!out.contains(QStringLiteral("supportsScriptState")));
    QVERIFY(!out.contains(QStringLiteral("supportsSingleWindow")));
    QVERIFY(!out.contains(QStringLiteral("retileOnFocus")));
    QVERIFY(!out.contains(QStringLiteral("onWindowResized")));
}

void TestAlgorithmScaffold::sanitizeStripsBreakoutCharacters()
{
    QCOMPARE(sanitizeMetadataString(QStringLiteral("a\"b\\c\nd\re")), QStringLiteral("a'b/c d e"));
    // Braces are legal in display names; the metadata-rewrite depth scan ignores
    // braces inside quoted strings, so sanitize leaves them alone.
    QCOMPARE(sanitizeMetadataString(QStringLiteral("My {Fancy} Grid")), QStringLiteral("My {Fancy} Grid"));
}

QTEST_GUILESS_MAIN(TestAlgorithmScaffold)
#include "test_algorithm_scaffold.moc"
