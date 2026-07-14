// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../../src/settings/algorithmscaffold.h"

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

const QString kHeader = QStringLiteral(
    "-- SPDX-FileCopyrightText: 2026 <your name>\n"
    "-- SPDX-License-Identifier: GPL-3.0-or-later\n");

} // namespace

class TestAlgorithmScaffold : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void spliceRewritesNameAndId();
    void splicePreservesCapabilityMetadata();
    void spliceLeavesCustomParamNamesAlone();
    void spliceInsertsMissingNameAndId();
    void spliceRejectsTemplateWithoutMetadata();
    void blankScaffoldEmitsRequestedFlags();
    void blankScaffoldOmitsUnsetNewFlags();
    void sanitizeStripsBreakoutCharacters();
};

void TestAlgorithmScaffold::spliceRewritesNameAndId()
{
    const QString out = spliceTemplate(kTemplate, kHeader, QStringLiteral("My Theater"), QStringLiteral("my-theater"));
    QVERIFY(out.contains(QStringLiteral("        name = \"My Theater\",")));
    QVERIFY(out.contains(QStringLiteral("        id = \"my-theater\",")));
    QVERIFY(!out.contains(QStringLiteral("name = \"Theater\"")));
    QVERIFY(!out.contains(QStringLiteral("id = \"theater\",")));
    // The fresh header replaced the template's own SPDX block.
    QVERIFY(out.startsWith(kHeader));
    QCOMPARE(out.count(QStringLiteral("SPDX-FileCopyrightText")), 1);
}

void TestAlgorithmScaffold::splicePreservesCapabilityMetadata()
{
    const QString out = spliceTemplate(kTemplate, kHeader, QStringLiteral("My Theater"), QStringLiteral("my-theater"));
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
    const QString out = spliceTemplate(kTemplate, kHeader, QStringLiteral("My Theater"), QStringLiteral("my-theater"));
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
    const QString out = spliceTemplate(noIdent, kHeader, QStringLiteral("Bare Copy"), QStringLiteral("bare-copy"));
    QVERIFY(out.contains(QStringLiteral("        name = \"Bare Copy\",")));
    QVERIFY(out.contains(QStringLiteral("        id = \"bare-copy\",")));
    // Inserted inside the metadata table, before its closing brace.
    QVERIFY(out.indexOf(QStringLiteral("name = \"Bare Copy\"")) < out.indexOf(QStringLiteral("description")));
}

void TestAlgorithmScaffold::spliceRejectsTemplateWithoutMetadata()
{
    QVERIFY(spliceTemplate(QStringLiteral("return {}"), kHeader, QStringLiteral("X"), QStringLiteral("x")).isEmpty());
    QVERIFY(spliceTemplate(QString(), kHeader, QStringLiteral("X"), QStringLiteral("x")).isEmpty());
    // Unterminated metadata table.
    QVERIFY(spliceTemplate(QStringLiteral("metadata = {\n name = \"a\",\n"), kHeader, QStringLiteral("X"),
                           QStringLiteral("x"))
                .isEmpty());
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
}

QTEST_GUILESS_MAIN(TestAlgorithmScaffold)
#include "test_algorithm_scaffold.moc"
