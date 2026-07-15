// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The shapes rewriteMetadataNameId() / spliceTemplate() refuse.
//
// Split out of test_algorithm_scaffold.cpp on the same rule that put the blank
// scaffold in its own file: one file for every surface passed the 800-line
// guideline. These share no fixture with the rewrite tests beyond the copyright
// line, and they all assert the same thing — an unrecognized shape returns
// empty rather than a corrupted script.
//
// Every case here is a shape a rewrite COULD plausibly accept and mangle. The
// value is in the refusal, so a test that stops failing is a test that found a
// regression: Luau resolves a duplicate key last-wins, so a splice that inserts
// a pair it should have rewritten yields a copy that silently keeps the
// template's id and collides with it.

#include "../../../src/settings/algorithmscaffold.h"

#include <QtTest>

using namespace PlasmaZones::AlgorithmScaffold;

namespace {

// A template shaped like the bundled algorithms, used where a case needs a
// well-formed script to vary one thing about.
const QString kTemplate = QStringLiteral(R"(-- SPDX-FileCopyrightText: 2026 fuddlesworth
-- SPDX-License-Identifier: GPL-3.0-or-later

return pluau.algorithm {
    metadata = {
        name = "Theater",
        id = "theater",
    },

    tile = function(ctx)
        return pluau.fillArea(ctx.area, ctx.windowCount)
    end,
}
)");

// A copy takes only the new owner's copyright and inherits the template's
// license. kHeader is the multi-line form the splice must refuse.
const QString kCopyright = QStringLiteral("-- SPDX-FileCopyrightText: 2026 <your name>");
const QString kHeader = kCopyright + QStringLiteral("\n-- SPDX-License-Identifier: GPL-3.0-or-later\n");

} // namespace

class TestAlgorithmScaffoldRejects : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void rejectsMultiLineCopyright();
    void rejectsNonCopyrightHeader();
    void rejectsMalformedTemplates();
    void rejectsSingleLineMetadata();
    void rejectsInlineFieldsOnOpeningLine();
    void rejectsMultiFieldNameLine();
    void rejectsUnquotedNameValue();
    void rejectsFieldHiddenBehindLongComment();
    void ignoresMetadataTableInsideLongComment();
    void rejectsStringLeftOpenAtEndOfLine();
};

void TestAlgorithmScaffoldRejects::rejectsMultiLineCopyright()
{
    // Positive control first. Every case in this file asserts an empty return,
    // and spliceTemplate returns empty for a bad header AND for any template
    // shape the rewrite refuses — so if kTemplate ever drifted into a refused
    // shape, the whole file would pass while gating nothing.
    QVERIFY(!spliceTemplate(kTemplate, kCopyright, QStringLiteral("Control"), QStringLiteral("control")).isEmpty());

    // A full header here would slip past the copy-of-a-copy dedupe and leave
    // the copy with two license identifiers, so it is rejected outright.
    QVERIFY(spliceTemplate(kTemplate, kHeader, QStringLiteral("My Copy"), QStringLiteral("my-copy")).isEmpty());
}

void TestAlgorithmScaffoldRejects::rejectsNonCopyrightHeader()
{
    // The dedupe compares this line against the template's own SPDX lines, so a
    // line that is not a copyright at all would yield a copy whose first line is
    // not one either. Empty is the same shape: a blank leading line, no
    // copyright anywhere.
    QVERIFY(spliceTemplate(kTemplate, QStringLiteral("-- just a note"), QStringLiteral("X"), QStringLiteral("x"))
                .isEmpty());
    QVERIFY(spliceTemplate(kTemplate, QString(), QStringLiteral("X"), QStringLiteral("x")).isEmpty());
}

void TestAlgorithmScaffoldRejects::rejectsMalformedTemplates()
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

void TestAlgorithmScaffoldRejects::rejectsSingleLineMetadata()
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

void TestAlgorithmScaffoldRejects::rejectsInlineFieldsOnOpeningLine()
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

void TestAlgorithmScaffoldRejects::rejectsMultiFieldNameLine()
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

void TestAlgorithmScaffoldRejects::rejectsUnquotedNameValue()
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

void TestAlgorithmScaffoldRejects::rejectsFieldHiddenBehindLongComment()
{
    // The one shape that proves the trailing-comment match excludes a `--[[`
    // opener. Read as an ordinary line comment the line looks like a lone name
    // field, but the long comment closes mid-line and `id` is live code again,
    // so accepting it would skip the second-field guard and insert a duplicate
    // id. Every other accepted shape behaves identically with or without that
    // exclusion, which makes this the only case that can catch its removal.
    const QString hidden = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = \"A\", --[[ c ]] id = \"x\",\n"
        "    },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    QVERIFY(rewriteMetadataNameId(hidden, QStringLiteral("Copy"), QStringLiteral("copy")).isEmpty());

    // The same trap on the id line, and with a level-N long comment.
    const QString hiddenId = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        id = \"a\", --[==[ c ]==] name = \"X\",\n"
        "    },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    QVERIFY(rewriteMetadataNameId(hiddenId, QStringLiteral("Copy"), QStringLiteral("copy")).isEmpty());
}

void TestAlgorithmScaffoldRejects::ignoresMetadataTableInsideLongComment()
{
    // A doc block showing the metadata shape is text, not the table. Taking it
    // is the quiet failure the whole reject surface exists to avoid: the
    // commented-out table rewrites cleanly, both fields read as seen, and the
    // real table below keeps the source's id for Luau's last-wins to hand back,
    // so the copy collides with what it was copied from.
    const QString docBlock = QStringLiteral(
        "--[[ An example:\n"
        "    metadata = {\n"
        "        name = \"Example\",\n"
        "        id = \"example\",\n"
        "    },\n"
        "]]\n"
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = \"Real\",\n"
        "        id = \"real\",\n"
        "    },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    const QString out = rewriteMetadataNameId(docBlock, QStringLiteral("Copy"), QStringLiteral("copy"));
    QVERIFY(!out.isEmpty());
    // The real table was rewritten, exactly once.
    QVERIFY(out.contains(QStringLiteral("        name = \"Copy\",")));
    QVERIFY(out.contains(QStringLiteral("        id = \"copy\",")));
    QVERIFY(!out.contains(QStringLiteral("id = \"real\"")));
    // The doc block is untouched, so its own name/id still read as they did.
    QVERIFY(out.contains(QStringLiteral("        name = \"Example\",")));
    QVERIFY(out.contains(QStringLiteral("        id = \"example\",")));
}

void TestAlgorithmScaffoldRejects::rejectsStringLeftOpenAtEndOfLine()
{
    // The scan reads one line at a time, so a string still open at the end of
    // one runs past where it can follow. Reading on would take the literal's
    // text as code: the shapes below each hide a brace that way, and a miscount
    // closes the table early, which ends with a duplicate name/id pair and a
    // copy that keeps its source's id.

    // A backtick literal whose interpolation spans lines. The sections cannot
    // cross a newline, but the expression between them is ordinary code and
    // may, so the `{` opening it is the last thing on its line.
    const QString multiLineInterp = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        description = `x{\n"
        "            f(1)\n"
        "        }y`,\n"
        "        name = \"A\",\n"
        "        id = \"a\",\n"
        "    },\n"
        "}\n");
    QVERIFY(rewriteMetadataNameId(multiLineInterp, QStringLiteral("New"), QStringLiteral("newid")).isEmpty());

    // A backslash-newline continuation, and `\z`, which is the same idea spelled
    // differently: both leave a short string open when the line ends.
    for (const QString& continuation : {QStringLiteral("\\"), QStringLiteral("\\z")}) {
        const QString openShortString = QStringLiteral(
                                            "return pluau.algorithm {\n    metadata = {\n"
                                            "        description = \"a }")
            + continuation + QStringLiteral("\nb\",\n        name = \"A\",\n        id = \"a\",\n    },\n}\n");
        QVERIFY(rewriteMetadataNameId(openShortString, QStringLiteral("New"), QStringLiteral("newid")).isEmpty());
    }

    // Ahead of the table counts too: the search would otherwise read the
    // literal's own text looking for `metadata = {`.
    const QString openBeforeTable = QStringLiteral(
        "local doc = \"unterminated }\\\n"
        "still the string\"\n"
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = \"A\",\n"
        "        id = \"a\",\n"
        "    },\n"
        "}\n");
    QVERIFY(rewriteMetadataNameId(openBeforeTable, QStringLiteral("New"), QStringLiteral("newid")).isEmpty());
}

QTEST_GUILESS_MAIN(TestAlgorithmScaffoldRejects)
#include "test_algorithm_scaffold_rejects.moc"
