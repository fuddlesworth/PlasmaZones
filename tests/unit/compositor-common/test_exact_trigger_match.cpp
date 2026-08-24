// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Exact trigger matching: the half of TriggerParser the scrolling "scroll
// keys" are read with. The drag half (anyTriggerHeld) is deliberately subset-
// matching; this half is deliberately not, and the two must not drift into
// each other — a subset-matching wheel chord makes the longer of any two
// bound chords unreachable, which is the exact bug this suite pins.
//
// Nothing distinguishes the two kinds of list in storage. The caller picks
// the matcher by which SETTING the list came from, so these tests are about
// the matchers, not about any marker on a trigger.

#include <PhosphorCompositor/TriggerParser.h>

#include <QTest>

using namespace PhosphorCompositor;

namespace {

// DragModifier wire values, spelled here for the same reason TriggerParser
// spells them: the enum lives in a daemon header a library test must not pull.
constexpr int ModShift = 1;
constexpr int ModAlt = 3;
constexpr int ModMeta = 4;
constexpr int ModMetaShift = 11;
constexpr int ModCtrlMeta = 12;

ParsedTrigger trigger(int modifier, int mouseButton = 0)
{
    return ParsedTrigger{modifier, mouseButton};
}

} // namespace

class TestExactTriggerMatch : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // The stock pair. Meta focuses columns, Meta+Shift pans the view, and the
    // whole point is that holding Shift as well picks the SECOND chord rather
    // than also satisfying the first.
    void stockPairDoesNotOverlap()
    {
        const QVector<ParsedTrigger> focus{trigger(ModMeta)};
        const QVector<ParsedTrigger> view{trigger(ModMetaShift)};

        QVERIFY(TriggerParser::anyTriggerHeldExact(focus, Qt::MetaModifier, Qt::NoButton));
        QVERIFY(!TriggerParser::anyTriggerHeldExact(view, Qt::MetaModifier, Qt::NoButton));

        const Qt::KeyboardModifiers metaShift = Qt::MetaModifier | Qt::ShiftModifier;
        QVERIFY(TriggerParser::anyTriggerHeldExact(view, metaShift, Qt::NoButton));
        // The regression this whole matcher exists for: under the drag half's
        // subset rule, Meta+Shift would ALSO satisfy the plain-Meta chord and
        // the view binding could never be reached.
        QVERIFY(!TriggerParser::anyTriggerHeldExact(focus, metaShift, Qt::NoButton));
        QVERIFY(TriggerParser::anyTriggerHeld(focus, metaShift, Qt::NoButton));
    }

    // An all-zero trigger is malformed config, and both matchers skip it.
    // Honouring it would claim every unmodified event in the session, which
    // for the scroll keys means swallowing every plain scroll.
    void allZeroTriggerFailsClosed()
    {
        const QVector<ParsedTrigger> empty{trigger(0)};
        QVERIFY(!TriggerParser::anyTriggerHeldExact(empty, Qt::NoModifier, Qt::NoButton));
        QVERIFY(!TriggerParser::anyTriggerHeld(empty, Qt::NoModifier, Qt::NoButton));
    }

    // A mouse button is ANDed with the modifiers, matching the drag half.
    void mouseButtonMustAlsoBeHeld()
    {
        const QVector<ParsedTrigger> chord{trigger(ModAlt, Qt::RightButton)};
        QVERIFY(!TriggerParser::anyTriggerHeldExact(chord, Qt::AltModifier, Qt::NoButton));
        QVERIFY(!TriggerParser::anyTriggerHeldExact(chord, Qt::NoModifier, Qt::RightButton));
        QVERIFY(TriggerParser::anyTriggerHeldExact(chord, Qt::AltModifier, Qt::RightButton));
        // Extra buttons do not disqualify: the button half is a containment
        // test, and a user resting on another button is not rebinding.
        QVERIFY(TriggerParser::anyTriggerHeldExact(chord, Qt::AltModifier, Qt::RightButton | Qt::MiddleButton));
    }

    // Lock and keypad modifiers ride along on real events. Folding them into
    // the comparison would make every chord fail under Num Lock.
    void nonChordModifiersAreIgnored()
    {
        const QVector<ParsedTrigger> focus{trigger(ModMeta)};
        QVERIFY(TriggerParser::anyTriggerHeldExact(focus, Qt::MetaModifier | Qt::KeypadModifier, Qt::NoButton));
    }

    // The two combos added alongside the scroll keys. Meta+Shift used to have
    // no enumerator at all and degraded to plain Shift on its way through the
    // editor, which is what made the stock view chord unstorable.
    void metaBearingCombosRoundTrip()
    {
        QVERIFY(TriggerParser::exactModifierMatch(ModMetaShift, Qt::MetaModifier | Qt::ShiftModifier));
        QVERIFY(TriggerParser::exactModifierMatch(ModCtrlMeta, Qt::ControlModifier | Qt::MetaModifier));
        QVERIFY(!TriggerParser::exactModifierMatch(ModMetaShift, Qt::ShiftModifier));
        QVERIFY(!TriggerParser::exactModifierMatch(ModShift, Qt::MetaModifier | Qt::ShiftModifier));
        // checkModifier is the subset peer and must agree on the same combos.
        QVERIFY(TriggerParser::checkModifier(ModMetaShift, Qt::MetaModifier | Qt::ShiftModifier));
        QVERIFY(TriggerParser::checkModifier(ModCtrlMeta, Qt::ControlModifier | Qt::MetaModifier));
    }

    // A scroll-key list is stored and parsed exactly like a drag list, so the
    // ordinary two-field parse is what feeds the exact matcher. Nothing on
    // the trigger says "wheel"; the setting it came from does.
    void scrollKeyListParsesLikeAnyOther()
    {
        QVariantMap entry;
        entry[QStringLiteral("modifier")] = ModMetaShift;
        entry[QStringLiteral("mouseButton")] = 0;

        const auto parsed = TriggerParser::parseTriggers(QVariantList{entry}, QStringLiteral("modifier"),
                                                         QStringLiteral("mouseButton"));
        QCOMPARE(parsed.size(), 1);
        QCOMPARE(parsed.first().modifier, ModMetaShift);
        QVERIFY(TriggerParser::anyTriggerHeldExact(parsed, Qt::MetaModifier | Qt::ShiftModifier, Qt::NoButton));
    }
};

QTEST_MAIN(TestExactTriggerMatch)
#include "test_exact_trigger_match.moc"
