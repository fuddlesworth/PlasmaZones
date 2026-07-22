// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings/services/algorithmscaffold.h"

#include <PhosphorTiles/AutotileConstants.h>

#include <QtTest>

using namespace PlasmaZones::AlgorithmScaffold;

// Which Capabilities field a data row drives. A member pointer rather than an
// index into a parallel array: the row names the field it means, so the two
// cannot drift apart and an added row cannot index out of range.
using CapFlag = bool PlasmaZones::AlgorithmScaffold::Capabilities::*;
Q_DECLARE_METATYPE(CapFlag)

namespace {

// The blank scaffold is our own code, so its copy takes both SPDX lines.
const QString kHeader = QStringLiteral(
    "-- SPDX-FileCopyrightText: 2026 <your name>\n"
    "-- SPDX-License-Identifier: GPL-3.0-or-later\n");

} // namespace

// buildBlankScaffold() and sanitizeMetadataString(). The template-splice half of
// AlgorithmScaffold lives in test_algorithm_scaffold.cpp. The two surfaces share
// no fixture.
class TestAlgorithmBlankScaffold : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void blankScaffoldEmitsRequestedFlags();
    void blankScaffoldMapsEachFlagToItsOwnField_data();
    void blankScaffoldMapsEachFlagToItsOwnField();
    void blankScaffoldOmitsUnsetNewFlags();
    void sanitizedNameCannotBreakOutOfTheLiteral();
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
    // The header's SPDX lines lead the generated algorithm. Its surrounding
    // blank lines are normalized rather than required, so a caller that omits
    // the trailing newline gets the same file as one that supplies it — the
    // separator is the scaffold's to place.
    QVERIFY(out.startsWith(kHeader));
    QString headerNoNewline = kHeader;
    while (headerNoNewline.endsWith(QLatin1Char('\n'))) {
        headerNoNewline.chop(1);
    }
    QCOMPARE(buildBlankScaffold(headerNoNewline, QStringLiteral("Mine"), QStringLiteral("mine"), caps), out);
    QVERIFY(out.contains(QStringLiteral("name = \"Mine\"")));
    QVERIFY(out.contains(QStringLiteral("id = \"mine\"")));
    QVERIFY(out.contains(QStringLiteral("supportsMasterCount = true")));
    QVERIFY(out.contains(QStringLiteral("supportsSplitRatio = true")));
    QVERIFY(out.contains(QStringLiteral("producesOverlappingZones = true")));
    QVERIFY(out.contains(QStringLiteral("supportsMemory = true")));
    QVERIFY(out.contains(QStringLiteral("supportsScriptState = true")));
    QVERIFY(out.contains(QStringLiteral("supportsSingleWindow = true")));
    QVERIFY(out.contains(QStringLiteral("retileOnFocus = true")));
    // Script state and split ratio each bring the resize-hook stub along, and
    // with both set the stub documents both halves of its return contract: the
    // reserved splitRatio control key, and the persistent bag.
    QVERIFY(out.contains(QStringLiteral("onWindowResized = function(state, resize)")));
    QVERIFY(out.contains(QStringLiteral("Return { splitRatio = n }")));
    QVERIFY(out.contains(QStringLiteral("persisted as ctx.state")));
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
    QTest::addColumn<CapFlag>("flag");
    QTest::addColumn<QString>("field");

    // One row per flag, each setting only that flag. Any two flags therefore
    // hold different values in at least one row, so swapping a pair of gates
    // fails here. A pair of mixed permutations does not have that property: two
    // flags on the same side of every permutation stay indistinguishable.
    QTest::newRow("masterCount") << &Capabilities::masterCount << QStringLiteral("supportsMasterCount");
    QTest::newRow("splitRatio") << &Capabilities::splitRatio << QStringLiteral("supportsSplitRatio");
    QTest::newRow("overlappingZones") << &Capabilities::overlappingZones << QStringLiteral("producesOverlappingZones");
    QTest::newRow("memory") << &Capabilities::memory << QStringLiteral("supportsMemory");
    QTest::newRow("scriptState") << &Capabilities::scriptState << QStringLiteral("supportsScriptState");
    QTest::newRow("singleWindow") << &Capabilities::singleWindow << QStringLiteral("supportsSingleWindow");
    QTest::newRow("retileOnFocus") << &Capabilities::retileOnFocus << QStringLiteral("retileOnFocus");
}

void TestAlgorithmBlankScaffold::blankScaffoldMapsEachFlagToItsOwnField()
{
    QFETCH(CapFlag, flag);
    QFETCH(QString, field);

    // The four always-written flags, then the three opt-in ones.
    const QStringList alwaysWritten = {QStringLiteral("supportsMasterCount"), QStringLiteral("supportsSplitRatio"),
                                       QStringLiteral("producesOverlappingZones"), QStringLiteral("supportsMemory")};
    const QStringList optIn = {QStringLiteral("supportsScriptState"), QStringLiteral("supportsSingleWindow"),
                               QStringLiteral("retileOnFocus")};

    Capabilities caps;
    caps.*flag = true;
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
    // The resize-hook stub rides on script state OR split ratio. The engine
    // applies a splitRatio returned from onWindowResized whether or not the
    // algorithm persists state (only the bag write is gated on
    // supportsScriptState), so a split-ratio author wants the hook too.
    const bool wantsResizeHook =
        field == QLatin1String("supportsScriptState") || field == QLatin1String("supportsSplitRatio");
    QCOMPARE(out.contains(QStringLiteral("onWindowResized")), wantsResizeHook);
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

void TestAlgorithmBlankScaffold::sanitizedNameCannotBreakOutOfTheLiteral()
{
    // The header's contract is that displayName arrives sanitized, because
    // buildBlankScaffold embeds it in a Luau string literal with no further
    // escaping. Pin the two halves together: a hostile name that has been
    // through the sanitizer must not be able to close its own quote and inject
    // a second field. Without this, the sanitizer and the embedding are each
    // tested in isolation and the contract BETWEEN them is not.
    const QString hostile = QStringLiteral("x\", supportsMemory = true, name = \"pwned");
    const QString out =
        buildBlankScaffold(kHeader, sanitizeMetadataString(hostile), QStringLiteral("mine"), Capabilities{});
    // Every double quote in the value became an apostrophe, so the whole
    // injection stays inert content of ONE name literal on ONE line. Asserting
    // the exact line is the point: `supportsMemory = true` and a second
    // `name =` DO both appear in the output as substrings of that literal, so
    // an absence check would fail here while proving nothing. What matters is
    // that they are inside the quotes rather than fields of their own.
    QVERIFY(out.contains(QStringLiteral("        name = \"x', supportsMemory = true, name = 'pwned\",")));
    // The real flag is untouched by the injected text above it.
    QVERIFY(out.contains(QStringLiteral("supportsMemory = false")));
    // The metadata table still has exactly one name field and one id field.
    const QStringList outLines = out.split(QLatin1Char('\n'));
    int nameFields = 0;
    int idFields = 0;
    for (const QString& line : outLines) {
        if (line.startsWith(QStringLiteral("        name = "))) {
            ++nameFields;
        }
        if (line.startsWith(QStringLiteral("        id = "))) {
            ++idFields;
        }
    }
    QCOMPARE(nameFields, 1);
    QCOMPARE(idFields, 1);
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
