// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../../../src/settings/algorithmscaffold.h"

#include <PhosphorTiles/AutotileConstants.h>

#include <QtTest>

using namespace PlasmaZones::AlgorithmScaffold;

namespace {

// The blank scaffold is our own code, so its copy takes both SPDX lines.
const QString kHeader = QStringLiteral(
    "-- SPDX-FileCopyrightText: 2026 <your name>\n"
    "-- SPDX-License-Identifier: GPL-3.0-or-later\n");

} // namespace

// buildBlankScaffold() and sanitizeMetadataString(). The template-splice half of
// AlgorithmScaffold lives in test_algorithm_scaffold.cpp; the two surfaces share
// no fixture, and together they exceed the 800-line file guideline.
class TestAlgorithmBlankScaffold : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void blankScaffoldEmitsRequestedFlags();
    void blankScaffoldMapsEachFlagToItsOwnField_data();
    void blankScaffoldMapsEachFlagToItsOwnField();
    void blankScaffoldOmitsUnsetNewFlags();
    void sanitizeStripsBreakoutCharacters();
};

void TestAlgorithmBlankScaffold::blankScaffoldEmitsRequestedFlags()
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
    // The header is passed through verbatim, so a generated algorithm carries
    // its SPDX lines.
    QVERIFY(out.startsWith(kHeader));
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
    // The unconditional metadata fields. Stripping these from a template was
    // the bug this file exists to pin, so the blank path pins them too.
    QVERIFY(out.contains(QStringLiteral("description = \"Custom tiling algorithm\"")));
    QVERIFY(out.contains(QStringLiteral("minimumWindows = 1,")));
    QVERIFY(out.contains(QStringLiteral("zoneNumberDisplay = \"all\",")));
    // The two defaults are expected THROUGH the engine's constants rather than
    // as literals. That does not catch hardcoding today, since the constants
    // still read 0.5 and 6. It earns its keep on the next bump: a scaffold that
    // hardcodes the old value then fails here on its own, with no test to edit.
    QVERIFY(out.contains(QStringLiteral("defaultSplitRatio = ")
                         + QString::number(PhosphorTiles::AutotileDefaults::DefaultSplitRatio) + QStringLiteral(",")));
    QVERIFY(out.contains(QStringLiteral("defaultMaxWindows = ")
                         + QString::number(PhosphorTiles::AutotileDefaults::ScriptedDefaultMaxWindows)
                         + QStringLiteral(",")));
}

void TestAlgorithmBlankScaffold::blankScaffoldMapsEachFlagToItsOwnField_data()
{
    QTest::addColumn<int>("index");
    QTest::addColumn<QString>("field");

    // One row per flag, each setting only that flag. Any two flags therefore
    // hold different values in at least one row, so swapping a pair of gates
    // fails here. A pair of mixed permutations does not have that property: two
    // flags on the same side of every permutation stay indistinguishable.
    QTest::newRow("masterCount") << 0 << QStringLiteral("supportsMasterCount");
    QTest::newRow("splitRatio") << 1 << QStringLiteral("supportsSplitRatio");
    QTest::newRow("overlappingZones") << 2 << QStringLiteral("producesOverlappingZones");
    QTest::newRow("memory") << 3 << QStringLiteral("supportsMemory");
    QTest::newRow("scriptState") << 4 << QStringLiteral("supportsScriptState");
    QTest::newRow("singleWindow") << 5 << QStringLiteral("supportsSingleWindow");
    QTest::newRow("retileOnFocus") << 6 << QStringLiteral("retileOnFocus");
}

void TestAlgorithmBlankScaffold::blankScaffoldMapsEachFlagToItsOwnField()
{
    QFETCH(int, index);
    QFETCH(QString, field);

    // The four always-written flags, then the three opt-in ones.
    const QStringList alwaysWritten = {QStringLiteral("supportsMasterCount"), QStringLiteral("supportsSplitRatio"),
                                       QStringLiteral("producesOverlappingZones"), QStringLiteral("supportsMemory")};
    const QStringList optIn = {QStringLiteral("supportsScriptState"), QStringLiteral("supportsSingleWindow"),
                               QStringLiteral("retileOnFocus")};

    Capabilities caps;
    bool* const flags[] = {&caps.masterCount, &caps.splitRatio,   &caps.overlappingZones, &caps.memory,
                           &caps.scriptState, &caps.singleWindow, &caps.retileOnFocus};
    *flags[index] = true;
    const QString out = buildBlankScaffold(kHeader, QStringLiteral("Mine"), QStringLiteral("mine"), caps);

    // The one set flag turns its own field true.
    QVERIFY2(out.contains(field + QStringLiteral(" = true")), qPrintable(field));
    // Every other always-written field is false, and every other opt-in field
    // is absent rather than written false.
    for (const QString& other : alwaysWritten) {
        if (other != field) {
            QVERIFY2(out.contains(other + QStringLiteral(" = false")), qPrintable(other));
        }
    }
    for (const QString& other : optIn) {
        if (other != field) {
            QVERIFY2(!out.contains(other), qPrintable(other));
        }
    }
    // The resize-hook stub rides on script state alone.
    QCOMPARE(out.contains(QStringLiteral("onWindowResized")), field == QLatin1String("supportsScriptState"));
}

void TestAlgorithmBlankScaffold::blankScaffoldOmitsUnsetNewFlags()
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

void TestAlgorithmBlankScaffold::sanitizeStripsBreakoutCharacters()
{
    QCOMPARE(sanitizeMetadataString(QStringLiteral("a\"b\\c\nd\re")), QStringLiteral("a'b/c d e"));
    // Braces are legal in display names; the metadata-rewrite depth scan ignores
    // braces inside quoted strings, so sanitize leaves them alone.
    QCOMPARE(sanitizeMetadataString(QStringLiteral("My {Fancy} Grid")), QStringLiteral("My {Fancy} Grid"));
}

QTEST_GUILESS_MAIN(TestAlgorithmBlankScaffold)

#include "test_algorithm_blank_scaffold.moc"
