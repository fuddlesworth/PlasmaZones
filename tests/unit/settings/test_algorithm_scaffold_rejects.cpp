// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The shapes rewriteMetadataNameId() / spliceTemplate() refuse.
//
// These share no fixture with the rewrite tests in test_algorithm_scaffold.cpp
// beyond the copyright line, and they all assert the same thing. An
// unrecognized shape returns empty rather than a corrupted script.
//
// Not every refusal lives here. spliceHandlesTrailingNameOrIdField, over in
// test_algorithm_scaffold.cpp, keeps its own rejects next to its accepts on
// purpose: it maps one shape family (a name/id field that does not lead its
// line) and the boundary between what that family accepts and refuses is the
// thing it pins, which splitting the two halves across files would hide.
//
// Every case here is a shape a rewrite COULD plausibly accept and mangle. The
// value is in the refusal, so a test that stops failing is a test that found a
// regression: Luau resolves a duplicate key last-wins, so a splice that inserts
// a pair it should have rewritten yields a copy that silently keeps the
// template's id and collides with it.

#include "../../../src/settings/algorithmscaffold.h"

#include <QtTest>

#include <utility>

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
    void rejectsLongBracketNameValue();
    void rejectsFieldHiddenBehindLongComment();
    void ignoresMetadataTableInsideLongComment();
    void rejectsStringLeftOpenAtEndOfLine();
    void rejectsKeyAndValueOnDifferentLines();
    void rejectsBracketedNameOrIdKey();
    void acceptsBracketedKeyThatIsNotNameOrId();
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

void TestAlgorithmScaffoldRejects::rejectsLongBracketNameValue()
{
    // `name = [[x]]` is a perfectly good Luau field, and on one line it even
    // looks rewritable. It is refused anyway, because the value may span lines
    // and the line-anchored read cannot follow it there. Accepting the
    // one-line spelling would mean reading the family by where its closer
    // happened to fall rather than by anything the read knows, so the whole
    // shape goes.
    const QString oneLine = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = [[x]],\n"
        "        id = \"a\",\n"
        "    },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    QVERIFY(spliceTemplate(oneLine, kCopyright, QStringLiteral("X"), QStringLiteral("x")).isEmpty());

    // The multi-line spelling, which is the reason the one-line one cannot be
    // taken on trust.
    const QString spansLines = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = [[opens here\n"
        "        and runs on]],\n"
        "        id = \"a\",\n"
        "    },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    QVERIFY(spliceTemplate(spansLines, kCopyright, QStringLiteral("X"), QStringLiteral("x")).isEmpty());

    // Level-N brackets are the same family, so `[==[` is refused like `[[`.
    const QString levelN = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = [==[x]==],\n"
        "        id = \"a\",\n"
        "    },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    QVERIFY(spliceTemplate(levelN, kCopyright, QStringLiteral("X"), QStringLiteral("x")).isEmpty());

    // Same shape on the id line — the symmetric reject branch.
    const QString longBracketId = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = \"N\",\n"
        "        id = [[x]],\n"
        "    },\n"
        "    tile = function(ctx) return {} end,\n"
        "}\n");
    QVERIFY(spliceTemplate(longBracketId, kCopyright, QStringLiteral("X"), QStringLiteral("x")).isEmpty());
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
    // one runs past where it can follow, and reading on would take the
    // literal's text as code.
    //
    // The first shape below is the one that demonstrably corrupts: its brace
    // lands on a line the scan has already forgotten it is inside a string on,
    // so the table closes early and the copy ends with a duplicate name/id
    // pair. In the others the brace happens to sit on the line where the quote
    // is still tracked, so they miscount nothing today. They are refused all
    // the same, because what makes them safe is an accident of where the brace
    // fell, not anything the scan knows.

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
    // differently: both leave a short string open when the line ends. Each
    // carries its name into the failure message, since the two assertions are
    // otherwise identical and a failure takes the rest of this test with it.
    const std::pair<const char*, QString> continuations[] = {
        {"backslash-newline", QStringLiteral("\\")},
        {"backslash-z", QStringLiteral("\\z")},
    };
    for (const auto& [label, continuation] : continuations) {
        const QString openShortString = QStringLiteral(
                                            "return pluau.algorithm {\n    metadata = {\n"
                                            "        description = \"a }")
            + continuation + QStringLiteral("\nb\",\n        name = \"A\",\n        id = \"a\",\n    },\n}\n");
        QVERIFY2(rewriteMetadataNameId(openShortString, QStringLiteral("New"), QStringLiteral("newid")).isEmpty(),
                 label);
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

void TestAlgorithmScaffoldRejects::rejectsKeyAndValueOnDifferentLines()
{
    // Luau does not care that the `=` is on the next line, and neither read of
    // the line can see the field: the key line has no `=`, and the value line
    // has no key. Taken as "no name/id here", the insert lands a second pair
    // above the real one and last-wins hands the copy its source's id.
    const QString splitName = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name\n"
        "            = \"A\",\n"
        "        id = \"a\",\n"
        "    },\n"
        "}\n");
    QVERIFY(rewriteMetadataNameId(splitName, QStringLiteral("New"), QStringLiteral("newid")).isEmpty());

    // The id side, and the same key trailing a sibling field rather than
    // leading its own line.
    const QString splitId = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = \"A\",\n"
        "        id\n"
        "            = \"a\",\n"
        "    },\n"
        "}\n");
    QVERIFY(rewriteMetadataNameId(splitId, QStringLiteral("New"), QStringLiteral("newid")).isEmpty());

    const QString trailingKey = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = \"A\", id\n"
        "            = \"a\",\n"
        "    },\n"
        "}\n");
    QVERIFY(rewriteMetadataNameId(trailingKey, QStringLiteral("New"), QStringLiteral("newid")).isEmpty());
}

void TestAlgorithmScaffoldRejects::rejectsBracketedNameOrIdKey()
{
    // `["name"]` is the same Luau key as the bare one, and the anchored line
    // patterns cannot rewrite it. Taken as "no name here", the insert lands a
    // second pair and last-wins hands the copy its source's name.
    const QString bracketName = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        [\"name\"] = \"A\",\n"
        "        id = \"a\",\n"
        "    },\n"
        "}\n");
    QVERIFY(rewriteMetadataNameId(bracketName, QStringLiteral("New"), QStringLiteral("newid")).isEmpty());

    const QString bracketId = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = \"A\",\n"
        "        ['id'] = \"a\",\n"
        "    },\n"
        "}\n");
    QVERIFY(rewriteMetadataNameId(bracketId, QStringLiteral("New"), QStringLiteral("newid")).isEmpty());

    // A key this cannot read back as a plain literal could still spell `name`,
    // so it is refused rather than assumed innocent. Concatenation first.
    const QString computedKey = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        [\"na\" .. \"me\"] = \"A\",\n"
        "        id = \"a\",\n"
        "    },\n"
        "}\n");
    QVERIFY(rewriteMetadataNameId(computedKey, QStringLiteral("New"), QStringLiteral("newid")).isEmpty());

    // Then a variable key, whose value this cannot see at all.
    const QString variableKey = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        [someKey] = \"A\",\n"
        "        id = \"a\",\n"
        "    },\n"
        "}\n");
    QVERIFY(rewriteMetadataNameId(variableKey, QStringLiteral("New"), QStringLiteral("newid")).isEmpty());

    // And an escaped quote inside the key, which the literal read stops at.
    const QString escapedKey = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        [\"na\\\"me\"] = \"A\",\n"
        "        id = \"a\",\n"
        "    },\n"
        "}\n");
    QVERIFY(rewriteMetadataNameId(escapedKey, QStringLiteral("New"), QStringLiteral("newid")).isEmpty());
}

void TestAlgorithmScaffoldRejects::acceptsBracketedKeyThatIsNotNameOrId()
{
    // A script is free to spell any other metadata key in bracketed form. That
    // is none of this rewrite's business, and refusing it would fail the whole
    // copy over a key it never needed to touch. `['namex']` is included because
    // it shares a prefix with `name` without being it.
    const QString bracketedSibling = QStringLiteral(
        "return pluau.algorithm {\n"
        "    metadata = {\n"
        "        name = \"A\",\n"
        "        id = \"a\",\n"
        "        [\"description\"] = \"d\",\n"
        "        ['namex'] = 1,\n"
        "    },\n"
        "}\n");
    const QString out = rewriteMetadataNameId(bracketedSibling, QStringLiteral("New"), QStringLiteral("newid"));
    QVERIFY(!out.isEmpty());
    QVERIFY(out.contains(QStringLiteral("name = \"New\",")));
    QVERIFY(out.contains(QStringLiteral("id = \"newid\",")));
    // The bracketed siblings are carried through untouched.
    QVERIFY(out.contains(QStringLiteral("[\"description\"] = \"d\",")));
    QVERIFY(out.contains(QStringLiteral("['namex'] = 1,")));
}

QTEST_GUILESS_MAIN(TestAlgorithmScaffoldRejects)
#include "test_algorithm_scaffold_rejects.moc"
