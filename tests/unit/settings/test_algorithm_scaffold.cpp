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
// template's license. kHeader is the multi-line form the splice must refuse.
const QString kCopyright = QStringLiteral("-- SPDX-FileCopyrightText: 2026 <your name>");
const QString kHeader = kCopyright + QStringLiteral("\n-- SPDX-License-Identifier: GPL-3.0-or-later\n");

} // namespace

class TestAlgorithmScaffold : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void spliceRewritesNameAndId();
    void spliceToleratesTrailingNewlineOnCopyright();
    void rewriteKeepsHeaderAndOtherTables();
    void spliceInheritsTemplateLicense();
    void spliceDoesNotRepeatOwnCopyright();
    void spliceCarriesEveryUpstreamCopyright();
    void spliceHandlesTemplateWithoutCopyright();
    void splicePreservesCapabilityMetadata();
    void spliceLeavesCustomParamNamesAlone();
    void spliceInsertsMissingNameAndId();
    void rewriteKeepsSourceIndent();
    void rewriteIndentsInsertIntoEmptyMetadata();
    void rewriteAcceptsHandWrittenFieldPunctuation();
    void spliceIgnoresBracesInsideStrings();
    void spliceHandlesLongBrackets();
    void spliceHandlesMultiLineLongBrackets();
    void spliceHandlesLevelNLongBrackets();
    void spliceKeepsQuotesAndLongBracketsApart();
    void spliceHandlesTrailingNameOrIdField();
    void spliceOrdersSpdxCopyrightBeforeLicense();
    void splicePreservesLeadingDocComment();
    void spliceNormalizesCrlf();
    void spliceAllBundledAlgorithms();
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
    // The nested customParams entry has its own `name` key on a line that
    // opens at depth 2. Only the top-level metadata `name` may be rewritten.
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
    // Inserted immediately after the opening brace, in that order, indented
    // like the field already in the table.
    QVERIFY(
        out.contains(QStringLiteral("    metadata = {\n"
                                    "        name = \"Bare Copy\",\n"
                                    "        id = \"bare-copy\",\n"
                                    "        description = \"Bare\",\n")));
}

void TestAlgorithmScaffold::rewriteKeepsSourceIndent()
{
    // The duplicate path runs over a user's own file, which need not use our
    // eight-space style. Both the rewrite and the insert take the source's
    // indent, so a copy keeps the formatting its author chose.
    const QString twoSpace = QStringLiteral(
        "return pluau.algorithm {\n"
        "  metadata = {\n"
        "    name = \"Src\",\n"
        "    description = \"Two-space\",\n"
        "  },\n"
        "  tile = function(ctx) return {} end,\n"
        "}\n");
    const QString out = rewriteMetadataNameId(twoSpace, QStringLiteral("Copy"), QStringLiteral("copy"));
    // The existing name keeps its own two-space indent...
    QVERIFY(out.contains(QStringLiteral("\n    name = \"Copy\",\n")));
    // ...and the inserted id matches its siblings rather than jumping to eight.
    QVERIFY(out.contains(QStringLiteral("\n    id = \"copy\",\n")));
    QVERIFY(!out.contains(QStringLiteral("        name = \"Copy\",")));
    QVERIFY(!out.contains(QStringLiteral("        id = \"copy\",")));
    // Untouched lines keep their indent too.
    QVERIFY(out.contains(QStringLiteral("\n    description = \"Two-space\",\n")));
}

void TestAlgorithmScaffold::rewriteIndentsInsertIntoEmptyMetadata()
{
    // A metadata table with no field to copy an indent from: the insert falls
    // back to the opening line's indent plus one level.
    const QString empty = QStringLiteral(
        "return pluau.algorithm {\n"
        "  metadata = {\n"
        "  },\n"
        "  tile = function(ctx) return {} end,\n"
        "}\n");
    const QString out = rewriteMetadataNameId(empty, QStringLiteral("Copy"), QStringLiteral("copy"));
    QVERIFY(
        out.contains(QStringLiteral("  metadata = {\n"
                                    "      name = \"Copy\",\n"
                                    "      id = \"copy\",\n"
                                    "  },\n")));
}

// duplicateAlgorithm() runs this rewrite over scripts a user wrote by hand, not
// only over the bundled templates. Luau accepts a single-quoted value, a `;`
// field separator, and a trailing comment, so rejecting any of them would fail
// the whole copy over punctuation.
void TestAlgorithmScaffold::rewriteAcceptsHandWrittenFieldPunctuation()
{
    const QString handWritten = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = 'My Layout',\n"
        "        id = \"mine\"; -- kept stable across renames\n"
        "        description = 'Mine',\n"
        "    },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    // Both quote styles reach both fields: the id line's own single-quoted
    // form is the name line's mirror and has its own regex alternation.
    const QString singleQuotedId =
        rewriteMetadataNameId(QStringLiteral("return pluau.algorithm {\n    metadata = {\n"
                                             "        name = \"A\",\n        id = 'a',\n    },\n}\n"),
                              QStringLiteral("Copy"), QStringLiteral("copy"));
    QVERIFY(singleQuotedId.contains(QStringLiteral("id = \"copy\",")));
    QVERIFY(!singleQuotedId.contains(QStringLiteral("'a'")));
    const QString out = rewriteMetadataNameId(handWritten, QStringLiteral("Copy"), QStringLiteral("copy"));
    QVERIFY(!out.isEmpty());
    QVERIFY(out.contains(QStringLiteral("name = \"Copy\",")));
    // The id line's own `;` and its comment survive: only the value changes.
    QVERIFY(out.contains(QStringLiteral("id = \"copy\"; -- kept stable across renames")));
    // The rewrite is confined to name/id — a single-quoted sibling is untouched.
    QVERIFY(out.contains(QStringLiteral("description = 'Mine',")));
    // Neither original value survives anywhere.
    QVERIFY(!out.contains(QStringLiteral("My Layout")));
    QVERIFY(!out.contains(QStringLiteral("\"mine\"")));

    // Only a real long-bracket opener is excluded from the trailing comment.
    // `--[` that opens no long bracket runs to the end of the line like any
    // other comment, so the field is still a whole line and still rewritable.
    const QString bracketComment =
        rewriteMetadataNameId(QStringLiteral("return pluau.algorithm {\n    metadata = {\n"
                                             "        name = \"A\", --[see docs]\n        id = \"a\",\n    },\n}\n"),
                              QStringLiteral("Copy"), QStringLiteral("copy"));
    QVERIFY(bracketComment.contains(QStringLiteral("name = \"Copy\", --[see docs]")));
}

namespace {

// Wrap @p metadataBody in a module whose metadata table carries a name/id pair
// after it, so a scan that mistracks depth ends the walk early and inserts a
// SECOND pair rather than rewriting the existing one.
QString moduleWithMetadataBody(const QString& metadataBody)
{
    return QStringLiteral("return pluau.algorithm {\n    metadata = {\n") + metadataBody
        + QStringLiteral(
               "        name = \"A\",\n"
               "        id = \"a\",\n"
               "    },\n"
               "    tile = function(ctx) return {} end,\n"
               "}\n");
}

// Assert the metadata name/id were rewritten exactly once. A duplicate pair is
// the silent-corruption shape: Luau is last-wins, so a copy would keep the
// source's id and collide with it.
void verifyRewrittenOnce(const QString& out)
{
    QVERIFY(!out.isEmpty());
    QCOMPARE(out.count(QStringLiteral("name = ")), 1);
    QCOMPARE(out.count(QStringLiteral("id = ")), 1);
    QVERIFY(out.contains(QStringLiteral("        name = \"B\",")));
    QVERIFY(out.contains(QStringLiteral("        id = \"b\",")));
}

} // namespace

void TestAlgorithmScaffold::spliceHandlesLongBrackets()
{
    // Braces inside a long bracket are text, not code. A `}` is the dangerous
    // direction: counted as code it closes the table early.
    const QString closingBrace = moduleWithMetadataBody(QStringLiteral("        description = [[a } brace]],\n"));
    QString out = spliceTemplate(closingBrace, kCopyright, QStringLiteral("B"), QStringLiteral("b"));
    verifyRewrittenOnce(out);
    QVERIFY(out.contains(QStringLiteral("        description = [[a } brace]],")));

    // An opening brace is the mirror case.
    out = spliceTemplate(moduleWithMetadataBody(QStringLiteral("        description = [[a { brace]],\n")), kCopyright,
                         QStringLiteral("B"), QStringLiteral("b"));
    verifyRewrittenOnce(out);

    // A long comment on one line, and a closed long bracket followed by a
    // second one that opens later on the SAME line.
    out = spliceTemplate(moduleWithMetadataBody(QStringLiteral("        --[[ note } here ]]\n")), kCopyright,
                         QStringLiteral("B"), QStringLiteral("b"));
    verifyRewrittenOnce(out);

    out = spliceTemplate(moduleWithMetadataBody(QStringLiteral("        description = [[x]] .. [[} y]],\n")),
                         kCopyright, QStringLiteral("B"), QStringLiteral("b"));
    verifyRewrittenOnce(out);
}

void TestAlgorithmScaffold::spliceHandlesMultiLineLongBrackets()
{
    // A long comment spanning lines: the continuation carries no `--`, so a
    // line-scoped scan would read its brace as code.
    QString out = spliceTemplate(moduleWithMetadataBody(QStringLiteral("        --[[ note\n"
                                                                       "        } still inside the comment ]]\n")),
                                 kCopyright, QStringLiteral("B"), QStringLiteral("b"));
    verifyRewrittenOnce(out);

    // The same hazard as a long string value.
    out = spliceTemplate(moduleWithMetadataBody(QStringLiteral("        description = [[opens here\n"
                                                               "        } and runs on ]],\n")),
                         kCopyright, QStringLiteral("B"), QStringLiteral("b"));
    verifyRewrittenOnce(out);

    // A name/id line INSIDE a long string is that string's text, so it must be
    // left alone rather than rewritten.
    out = spliceTemplate(moduleWithMetadataBody(QStringLiteral("        description = [[\n"
                                                               "        name = \"decoy\",\n"
                                                               "        ]],\n")),
                         kCopyright, QStringLiteral("B"), QStringLiteral("b"));
    QVERIFY(!out.isEmpty());
    QVERIFY(out.contains(QStringLiteral("        name = \"decoy\",")));
    QVERIFY(out.contains(QStringLiteral("        name = \"B\",")));
    QVERIFY(out.contains(QStringLiteral("        id = \"b\",")));
}

void TestAlgorithmScaffold::spliceHandlesLevelNLongBrackets()
{
    // Luau long brackets nest by level: `[=[` closes only on `]=]`, so a `]]`
    // inside one is text. A scan matching only `[[` misses these entirely.
    QString out = spliceTemplate(moduleWithMetadataBody(QStringLiteral("        --[==[ note\n"
                                                                       "        } still inside ]==]\n")),
                                 kCopyright, QStringLiteral("B"), QStringLiteral("b"));
    verifyRewrittenOnce(out);

    out = spliceTemplate(moduleWithMetadataBody(QStringLiteral("        description = [=[a } brace]=],\n")), kCopyright,
                         QStringLiteral("B"), QStringLiteral("b"));
    verifyRewrittenOnce(out);

    // A level-0 closer inside a level-1 bracket does not end it.
    out = spliceTemplate(moduleWithMetadataBody(QStringLiteral("        description = [=[ has ]] inside } ]=],\n")),
                         kCopyright, QStringLiteral("B"), QStringLiteral("b"));
    verifyRewrittenOnce(out);
}

void TestAlgorithmScaffold::spliceOrdersSpdxCopyrightBeforeLicense()
{
    // templateSpdxLines emits copyrights first, then every other SPDX tag. No
    // bundled algorithm is license-first, so only a hand-authored template
    // reaches the reorder.
    const QString licenseFirst = QStringLiteral(
        "-- SPDX-License-Identifier: MIT\n"
        "-- SPDX-FileCopyrightText: 2026 upstream\n"
        "\n"
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = \"A\",\n"
        "        id = \"a\",\n"
        "    },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    const QString out = spliceTemplate(licenseFirst, kCopyright, QStringLiteral("B"), QStringLiteral("b"));
    QVERIFY(!out.isEmpty());
    // Both copyrights lead, in new-owner-then-upstream order, and the
    // template's license follows them.
    QVERIFY(
        out.startsWith(QStringLiteral("-- SPDX-FileCopyrightText: 2026 <your name>\n"
                                      "-- SPDX-FileCopyrightText: 2026 upstream\n"
                                      "-- SPDX-License-Identifier: MIT\n")));
}

void TestAlgorithmScaffold::spliceKeepsQuotesAndLongBracketsApart()
{
    // A `[[` inside a quoted string is that string's text, not a bracket. Were
    // the long-bracket check to run before the quote check, this would open a
    // bracket that never closes and swallow the rest of the table.
    QString out = spliceTemplate(moduleWithMetadataBody(QStringLiteral("        description = \"see [[ docs\",\n")),
                                 kCopyright, QStringLiteral("B"), QStringLiteral("b"));
    verifyRewrittenOnce(out);

    // The mirror: a lone quote inside a long bracket is that bracket's text,
    // and must not open a string.
    out = spliceTemplate(moduleWithMetadataBody(QStringLiteral("        description = [[it's a } thing]],\n")),
                         kCopyright, QStringLiteral("B"), QStringLiteral("b"));
    verifyRewrittenOnce(out);
}

void TestAlgorithmScaffold::spliceHandlesTrailingNameOrIdField()
{
    // A name/id that does not lead its line is invisible to the anchored
    // rewrite. Inserting our own pair anyway would leave two, and Luau takes
    // the last, so the copy would silently keep the template's name and id.
    const QString trailingName = moduleWithMetadataBody(QStringLiteral("        a = 1, name = \"x\",\n"));
    QVERIFY(spliceTemplate(trailingName, kCopyright, QStringLiteral("B"), QStringLiteral("b")).isEmpty());

    const QString trailingId = moduleWithMetadataBody(QStringLiteral("        a = 1, id = \"x\",\n"));
    QVERIFY(spliceTemplate(trailingId, kCopyright, QStringLiteral("B"), QStringLiteral("b")).isEmpty());

    // Luau separates fields with `;` as readily as `,`, and the trap is the
    // same either way.
    const QString semicolon = moduleWithMetadataBody(QStringLiteral("        a = 1; name = \"x\";\n"));
    QVERIFY(spliceTemplate(semicolon, kCopyright, QStringLiteral("B"), QStringLiteral("b")).isEmpty());

    // A nested table's closer does not end the line's own fields. These are the
    // reject mirror of the inline-customParams accept below: together they pin
    // that the scan elides a nested table's contents without also losing the
    // real field after it.
    const QString afterInlineNested =
        moduleWithMetadataBody(QStringLiteral("        customParams = { key = \"g\" }, name = \"x\",\n"));
    QVERIFY(spliceTemplate(afterInlineNested, kCopyright, QStringLiteral("B"), QStringLiteral("b")).isEmpty());

    const QString afterInlineNestedSemi =
        moduleWithMetadataBody(QStringLiteral("        customParams = { a = 1 }; id = \"x\",\n"));
    QVERIFY(spliceTemplate(afterInlineNestedSemi, kCopyright, QStringLiteral("B"), QStringLiteral("b")).isEmpty());

    // The same field trailing a MULTI-line nested table's closer. This line
    // opens at depth 2, so a scan that only looks at lines opening at depth 1
    // never sees it and lets the pair duplicate.
    const QString afterBlockNested =
        moduleWithMetadataBody(QStringLiteral("        customParams = {\n"
                                              "            { a = 1 },\n"
                                              "        }, name = \"x\",\n"));
    QVERIFY(spliceTemplate(afterBlockNested, kCopyright, QStringLiteral("B"), QStringLiteral("b")).isEmpty());

    const QString afterBlockNestedId =
        moduleWithMetadataBody(QStringLiteral("        customParams = {\n"
                                              "            { a = 1 },\n"
                                              "        }, id = \"x\",\n"));
    QVERIFY(spliceTemplate(afterBlockNestedId, kCopyright, QStringLiteral("B"), QStringLiteral("b")).isEmpty());

    // A field AHEAD of the table's own closer is still the table's field.
    const QString onCloserLine = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        description = \"d\",\n"
        "        foo = 1, name = \"Real\" },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    QVERIFY(spliceTemplate(onCloserLine, kCopyright, QStringLiteral("B"), QStringLiteral("b")).isEmpty());

    // A bracketed key is the same Luau key as the bare one, and its string is
    // elided from the match subject, so the shape is rejected rather than read.
    const QString bracketedKey = moduleWithMetadataBody(QStringLiteral("        [\"name\"] = \"Real\",\n"));
    QVERIFY(spliceTemplate(bracketedKey, kCopyright, QStringLiteral("B"), QStringLiteral("b")).isEmpty());

    // A line that opens inside a long bracket can still close it and carry a
    // real field. Skipping the whole line would leave this to duplicate.
    const QString afterCloser =
        moduleWithMetadataBody(QStringLiteral("        doc = [[\n"
                                              "text\n"
                                              "]], name = \"x\",\n"));
    QVERIFY(spliceTemplate(afterCloser, kCopyright, QStringLiteral("B"), QStringLiteral("b")).isEmpty());

    // A nested table's own keys are not second fields, so an inline
    // customParams must still splice, keeping its nested `name` untouched. This
    // is the false-reject the probe has to stay clear of. (verifyRewrittenOnce
    // does not fit the rest of this test: its substring count would also see
    // the `name = ` inside the surviving lines.)
    QString out = spliceTemplate(
        moduleWithMetadataBody(QStringLiteral("        customParams = { key = \"g\", name = \"Gap\" },\n")), kCopyright,
        QStringLiteral("B"), QStringLiteral("b"));
    QVERIFY(!out.isEmpty());
    QVERIFY(out.contains(QStringLiteral("        name = \"B\",")));
    QVERIFY(out.contains(QStringLiteral("        id = \"b\",")));
    QVERIFY(!out.contains(QStringLiteral("name = \"A\"")));
    QVERIFY(out.contains(QStringLiteral("        customParams = { key = \"g\", name = \"Gap\" },")));

    // Past the table's own closer the code belongs to the enclosing table, so a
    // `name` there is not metadata's and must not reject.
    out = spliceTemplate(QStringLiteral("return pluau.algorithm {\n"
                                        "    metadata = {\n"
                                        "        name = \"A\",\n"
                                        "        id = \"a\",\n"
                                        "    }, foo = { name = \"x\" },\n"
                                        "    tile = function(ctx) return {} end,\n"
                                        "}\n"),
                         kCopyright, QStringLiteral("B"), QStringLiteral("b"));
    QVERIFY(!out.isEmpty());
    QVERIFY(out.contains(QStringLiteral("        name = \"B\",")));
    QVERIFY(!out.contains(QStringLiteral("name = \"A\"")));
    QVERIFY(out.contains(QStringLiteral("    }, foo = { name = \"x\" },")));

    // A nested table written across lines keeps its own name key too.
    out = spliceTemplate(moduleWithMetadataBody(QStringLiteral("        customParams = {\n"
                                                               "            { name = \"Gap\", d = 1 },\n"
                                                               "        },\n")),
                         kCopyright, QStringLiteral("B"), QStringLiteral("b"));
    QVERIFY(!out.isEmpty());
    QVERIFY(out.contains(QStringLiteral("        name = \"B\",")));
    QVERIFY(!out.contains(QStringLiteral("name = \"A\"")));
    QVERIFY(out.contains(QStringLiteral("            { name = \"Gap\", d = 1 },")));

    // An identifier that merely ends in `name` is not a name field.
    out = spliceTemplate(moduleWithMetadataBody(QStringLiteral("        a = 1, myname = \"x\",\n")), kCopyright,
                         QStringLiteral("B"), QStringLiteral("b"));
    QVERIFY(!out.isEmpty());
    QVERIFY(out.contains(QStringLiteral("        name = \"B\",")));
    QVERIFY(!out.contains(QStringLiteral("name = \"A\"")));
    QVERIFY(out.contains(QStringLiteral("        a = 1, myname = \"x\",")));

    // A `name =` inside a string value is text, not a field. (verifyRewrittenOnce
    // does not fit: its substring count would also see the one in the value.)
    const QString inString = moduleWithMetadataBody(QStringLiteral("        description = \"name = x, id = y\",\n"));
    out = spliceTemplate(inString, kCopyright, QStringLiteral("B"), QStringLiteral("b"));
    QVERIFY(!out.isEmpty());
    QVERIFY(out.contains(QStringLiteral("        name = \"B\",")));
    QVERIFY(out.contains(QStringLiteral("        id = \"b\",")));
    QVERIFY(!out.contains(QStringLiteral("name = \"A\"")));
    QVERIFY(out.contains(QStringLiteral("        description = \"name = x, id = y\",")));
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

QTEST_GUILESS_MAIN(TestAlgorithmScaffold)
#include "test_algorithm_scaffold.moc"
