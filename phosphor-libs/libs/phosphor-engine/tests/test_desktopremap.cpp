// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorEngine/DesktopRemap.h>

#include <QTest>

using PhosphorEngine::DesktopRemap;

namespace {
QStringList ids(const char* letters)
{
    QStringList out;
    for (const char* c = letters; *c; ++c) {
        out.append(QString(QLatin1Char(*c)));
    }
    return out;
}
} // namespace

/// The one old-to-new desktop position map every number-keyed store takes
/// (F787, orchestrator_phaseb_1).
class TestDesktopRemap : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void removalMidList()
    {
        const DesktopRemap remap = DesktopRemap::fromIds(ids("ABCD"), ids("ACD"));
        QVERIFY(!remap.isIdentity());
        QCOMPARE(remap.oldCount(), 4);
        QCOMPARE(remap.newCount(), 3);
        QCOMPARE(remap.map(1), 1);
        QCOMPARE(remap.map(2), 0);
        QCOMPARE(remap.map(3), 2);
        QCOMPARE(remap.map(4), 3);
        // Same answer as the position-only constructor.
        QCOMPARE(remap, DesktopRemap::removal(2, 4));
    }

    void insertMidList()
    {
        const DesktopRemap remap = DesktopRemap::fromIds(ids("ABC"), ids("AXBC"));
        QVERIFY(!remap.isIdentity());
        QCOMPARE(remap.newCount(), 4);
        QCOMPARE(remap.map(1), 1);
        QCOMPARE(remap.map(2), 3);
        QCOMPARE(remap.map(3), 4);
        QCOMPARE(remap.singleRemoval(), 0);
    }

    void reorderSwap()
    {
        const DesktopRemap remap = DesktopRemap::fromIds(ids("ABC"), ids("ACB"));
        QVERIFY(!remap.isIdentity());
        QCOMPARE(remap.map(1), 1);
        QCOMPARE(remap.map(2), 3);
        QCOMPARE(remap.map(3), 2);
        QCOMPARE(remap.singleRemoval(), 0);
    }

    void fourToTwo()
    {
        const DesktopRemap remap = DesktopRemap::fromIds(ids("ABCD"), ids("BD"));
        QCOMPARE(remap.newCount(), 2);
        QCOMPARE(remap.map(1), 0);
        QCOMPARE(remap.map(2), 1);
        QCOMPARE(remap.map(3), 0);
        QCOMPARE(remap.map(4), 2);
        // Two removed: not the single-removal shape.
        QCOMPARE(remap.singleRemoval(), 0);
        QCOMPARE(remap.successor(1), 1);
        QCOMPARE(remap.successor(3), 2);
    }

    void renameOnlyIsIdentity()
    {
        // The desktop manager compares ids; a rename changes only names, so
        // the id lists are equal.
        const DesktopRemap remap = DesktopRemap::fromIds(ids("ABC"), ids("ABC"));
        QVERIFY(remap.isIdentity());
        QCOMPARE(remap.singleRemoval(), 0);
        // Appending after the last desktop moves no stored number either.
        QVERIFY(DesktopRemap::fromIds(ids("ABC"), ids("ABCX")).isIdentity());
        // Nothing to compare against.
        QVERIFY(DesktopRemap::fromIds(QStringList(), ids("ABC")).isIdentity());
        QVERIFY(DesktopRemap().isIdentity());
    }

    void successorOfARemovedDesktop()
    {
        // Mid-list: the next desktop takes the removed number.
        const DesktopRemap mid = DesktopRemap::removal(2, 4);
        QCOMPARE(mid.successor(2), 2);
        QCOMPARE(mid.successor(3), 2);
        QCOMPARE(mid.successor(1), 1);
        // The last one: the new last desktop.
        const DesktopRemap last = DesktopRemap::removal(4, 4);
        QCOMPARE(last.map(4), 0);
        QCOMPARE(last.successor(4), 3);
        // With an insert in the same change, the next SURVIVING desktop.
        const DesktopRemap mixed = DesktopRemap::fromIds(ids("ABC"), ids("AXC"));
        QCOMPARE(mixed.map(2), 0);
        QCOMPARE(mixed.successor(2), 3);
        // Only nothing surviving answers 0.
        QCOMPARE(DesktopRemap::removal(1, 1).successor(1), 0);
    }

    void singleRemovalShape()
    {
        QCOMPARE(DesktopRemap::fromIds(ids("ABCD"), ids("ABD")).singleRemoval(), 3);
        QCOMPARE(DesktopRemap::removal(1, 3).singleRemoval(), 1);
        // A removal plus a reorder of the rest is not the shape.
        QCOMPARE(DesktopRemap::fromIds(ids("ABCD"), ids("ADB")).singleRemoval(), 0);
        // Out of range is the identity, not a removal.
        QVERIFY(DesktopRemap::removal(5, 4).isIdentity());
        QCOMPARE(DesktopRemap::removal(5, 4).singleRemoval(), 0);
    }

    void positionsOutsideTheListAreKept()
    {
        // 0 is the "every desktop" value several stores use; a number the old
        // list never had is not this map's to move.
        const DesktopRemap remap = DesktopRemap::removal(1, 3);
        QCOMPARE(remap.map(0), 0);
        QCOMPARE(remap.successor(0), 0);
        QCOMPARE(remap.map(7), 7);
        QCOMPARE(remap.successor(-1), -1);
    }

    void noSharedIdKeepsPositions()
    {
        // KWin re-created its desktops (fresh ids): positions are kept and the
        // ones past the new count go, instead of reading every desktop removed.
        const DesktopRemap remap = DesktopRemap::fromIds(ids("ABC"), ids("XY"));
        QCOMPARE(remap.map(1), 1);
        QCOMPARE(remap.map(2), 2);
        QCOMPARE(remap.map(3), 0);
        QCOMPARE(remap.successor(3), 2);
    }
};

QTEST_GUILESS_MAIN(TestDesktopRemap)
#include "test_desktopremap.moc"
