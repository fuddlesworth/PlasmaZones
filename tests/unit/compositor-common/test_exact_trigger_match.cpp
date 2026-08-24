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

    // A named mouse button must be held as well as the modifiers, matching
    // the drag half. See buttonAxisIsSubsetNotExact for what "as well as"
    // does NOT mean here.
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

    // Keypad and group-switch modifiers ride along on real events. Folding
    // them into the comparison would make every chord fail under Num Lock.
    // (Caps/Num Lock STATE is not carried in Qt::KeyboardModifiers at all, so
    // there is no lock flag to test here — only the keypad and group bits.)
    void nonChordModifiersAreIgnored()
    {
        const QVector<ParsedTrigger> focus{trigger(ModMeta)};
        QVERIFY(TriggerParser::anyTriggerHeldExact(focus, Qt::MetaModifier | Qt::KeypadModifier, Qt::NoButton));
        QVERIFY(TriggerParser::anyTriggerHeldExact(focus, Qt::MetaModifier | Qt::GroupSwitchModifier, Qt::NoButton));
        QVERIFY(TriggerParser::anyTriggerHeldExact(
            focus, Qt::MetaModifier | Qt::KeypadModifier | Qt::GroupSwitchModifier, Qt::NoButton));
    }

    // An empty list matches nothing. The loop body never runs, and both
    // matchers must fall through to false rather than to a default-true.
    void emptyListMatchesNothing()
    {
        const QVector<ParsedTrigger> none;
        QVERIFY(!TriggerParser::anyTriggerHeldExact(none, Qt::MetaModifier, Qt::NoButton));
        QVERIFY(!TriggerParser::anyTriggerHeld(none, Qt::MetaModifier, Qt::NoButton));
    }

    // A malformed entry must not stop the scan: the all-zero skip is a
    // `continue`, so a good entry AFTER it still has to match. A one-element
    // list cannot tell that apart from falling out of the loop.
    void malformedEntryDoesNotShadowLaterOnes()
    {
        const QVector<ParsedTrigger> list{trigger(0), trigger(ModMeta)};
        QVERIFY(TriggerParser::anyTriggerHeldExact(list, Qt::MetaModifier, Qt::NoButton));
        // And a multi-entry list matches on either member, not just the first.
        const QVector<ParsedTrigger> pair{trigger(ModMeta), trigger(ModCtrlMeta)};
        QVERIFY(TriggerParser::anyTriggerHeldExact(pair, Qt::MetaModifier, Qt::NoButton));
        QVERIFY(TriggerParser::anyTriggerHeldExact(pair, Qt::ControlModifier | Qt::MetaModifier, Qt::NoButton));
    }

    // AlwaysActive means "match whatever is held" to the subset matcher, but
    // modifierMaskFor has no case for it, so under exact matching it folds to
    // NoModifier and comes to mean the OPPOSITE: match only when nothing is
    // held. That inversion is why canonicalWheelTriggerList drops the
    // sentinel from any list read with this matcher. Pinned here so the
    // divergence cannot be "fixed" in the matcher by accident.
    void alwaysActiveInvertsUnderExactMatching()
    {
        constexpr int ModAlwaysActive = 8;
        const QVector<ParsedTrigger> sentinel{trigger(ModAlwaysActive)};

        QVERIFY(TriggerParser::anyTriggerHeld(sentinel, Qt::MetaModifier, Qt::NoButton));
        QVERIFY(!TriggerParser::anyTriggerHeldExact(sentinel, Qt::MetaModifier, Qt::NoButton));
        QVERIFY(TriggerParser::anyTriggerHeldExact(sentinel, Qt::NoModifier, Qt::NoButton));
    }

    // The button axis is SUBSET-matched even here, unlike the modifier axis.
    // A modifier-only chord therefore shadows the same modifier plus a
    // button, which is the reason the scroll-key rows offer modifiers only.
    void buttonAxisIsSubsetNotExact()
    {
        const QVector<ParsedTrigger> modifierOnly{trigger(ModMeta)};
        QVERIFY(TriggerParser::anyTriggerHeldExact(modifierOnly, Qt::MetaModifier, Qt::MiddleButton));

        // A button-only entry demands no chord modifier under exact matching,
        // where the subset peer reads modifier 0 as "any modifier".
        const QVector<ParsedTrigger> buttonOnly{trigger(0, Qt::RightButton)};
        QVERIFY(TriggerParser::anyTriggerHeldExact(buttonOnly, Qt::NoModifier, Qt::RightButton));
        QVERIFY(!TriggerParser::anyTriggerHeldExact(buttonOnly, Qt::MetaModifier, Qt::RightButton));
        QVERIFY(TriggerParser::anyTriggerHeld(buttonOnly, Qt::MetaModifier, Qt::RightButton));
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
