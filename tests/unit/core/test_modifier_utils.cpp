// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ModifierUtils: the DragModifier <-> Qt bitmask bridge the trigger editor
// round-trips every captured chord through.
//
// It had no coverage at all until the scroll keys made it load-bearing: the
// editor speaks bitmasks, storage speaks DragModifier enumerators, and a
// combination this file maps wrongly becomes a chord the user never chose.
// The mapping is also written against bare integer literals with no include
// of the enum it mirrors, so nothing but these assertions ties the two
// together.

#include "core/types/enums.h"
#include "core/utils/modifierutils.h"

#include <QTest>
#include <Qt>

using namespace PlasmaZones;
using PlasmaZones::ModifierUtils::bitmaskToDragModifier;
using PlasmaZones::ModifierUtils::dragModifierToBitmask;

namespace {

constexpr int shiftBit = static_cast<int>(Qt::ShiftModifier);
constexpr int ctrlBit = static_cast<int>(Qt::ControlModifier);
constexpr int altBit = static_cast<int>(Qt::AltModifier);
constexpr int metaBit = static_cast<int>(Qt::MetaModifier);

} // namespace

class TestModifierUtils : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // The two enumerators added alongside the scroll keys. Meta+Shift is the
    // one that mattered: before it existed the bitmask fell through to plain
    // Shift, so the stock view chord was literally unstorable and the editor
    // silently degraded any Meta+Shift capture into a different chord.
    void newCombosRoundTripBothWays()
    {
        QCOMPARE(bitmaskToDragModifier(metaBit | shiftBit), static_cast<int>(DragModifier::MetaShift));
        QCOMPARE(bitmaskToDragModifier(ctrlBit | metaBit), static_cast<int>(DragModifier::CtrlMeta));
        QCOMPARE(dragModifierToBitmask(static_cast<int>(DragModifier::MetaShift)), metaBit | shiftBit);
        QCOMPARE(dragModifierToBitmask(static_cast<int>(DragModifier::CtrlMeta)), ctrlBit | metaBit);
    }

    // Every enumerator up to MaxDragModifier must survive a round trip, with
    // the two documented exceptions below. This is the assertion that fails
    // if the enum grows and this mapper is not grown with it.
    void everyEnumeratorRoundTrips()
    {
        for (int mod = 1; mod <= MaxDragModifier; ++mod) {
            if (mod == static_cast<int>(DragModifier::AlwaysActive)) {
                continue; // sentinel, not a chord — see alwaysActiveIsNotAChord
            }
            const int mask = dragModifierToBitmask(mod);
            QVERIFY2(mask != 0, qPrintable(QStringLiteral("enumerator %1 maps to an empty bitmask").arg(mod)));
            QCOMPARE(bitmaskToDragModifier(mask), mod);
        }
    }

    // AlwaysActive is a drag-only sentinel meaning "no modifier required", so
    // it shares a bitmask with Disabled and cannot survive a round trip. The
    // editor must therefore never be handed one — TriggerUtils strips it, and
    // for the wheel lists canonicalWheelTriggerList drops it outright.
    void alwaysActiveIsNotAChord()
    {
        QCOMPARE(dragModifierToBitmask(static_cast<int>(DragModifier::AlwaysActive)), 0);
        QCOMPARE(dragModifierToBitmask(static_cast<int>(DragModifier::Disabled)), 0);
        QCOMPARE(bitmaskToDragModifier(0), static_cast<int>(DragModifier::Disabled));
    }

    // Combinations with no enumerator fall to a "closest match" tail, which
    // necessarily drops a modifier. Pinned because the tail is ORDER
    // dependent: the Meta+Shift arm sits ahead of the Alt+Shift one, so
    // Meta+Alt+Shift resolves to Meta+Shift and not to Alt+Shift. Reordering
    // the tail silently rebinds existing configs, and nothing else catches it.
    void unrepresentableCombosFallToDocumentedNeighbours()
    {
        QCOMPARE(bitmaskToDragModifier(metaBit | altBit | shiftBit), static_cast<int>(DragModifier::MetaShift));
        QCOMPARE(bitmaskToDragModifier(ctrlBit | metaBit | shiftBit), static_cast<int>(DragModifier::MetaShift));
        QCOMPARE(bitmaskToDragModifier(ctrlBit | altBit | metaBit | shiftBit),
                 static_cast<int>(DragModifier::CtrlAltMeta));
    }

    // Out-of-range input must not alias a real chord.
    void outOfRangeEnumeratorsYieldNoModifiers()
    {
        QCOMPARE(dragModifierToBitmask(MaxDragModifier + 1), 0);
        QCOMPARE(dragModifierToBitmask(-1), 0);
    }
};

QTEST_MAIN(TestModifierUtils)
#include "test_modifier_utils.moc"
