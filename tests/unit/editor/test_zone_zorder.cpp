// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_zone_zorder.cpp
 * @brief Unit tests for the ZoneManager zOrder and zoneNumber invariants, and the stacking undo must preserve
 *
 * Three contracts are pinned here.
 *
 * The zOrder density invariant: zOrder is the zone's index in the list, so after
 * any mutation the zOrder values must be exactly 0..count-1 with zone i carrying
 * zOrder i. EditorWindow.qml stacks zones at zoneBaseZ + zOrder and derives
 * zonesTopZ from the zone count, which DividerManager uses to decide when it can
 * win a hit test. A hole or a tie in the zOrder run breaks both.
 *
 * The zoneNumber invariant: a zone's number is user-owned and UNIQUE in 1..99,
 * but not positional. Gaps are allowed (1, 4, 7 is valid), so no mutation may
 * force the numbers dense. A zone that needs a fresh number takes the next
 * highest, max(existing) + 1. zoneNumber is serialized and drives the
 * user-facing "Zone N" labels, and EditorController::validateZoneNumber rejects
 * a duplicate outright when the user types one.
 *
 * The stacking contract for undo: deleting a zone and undoing must put it back
 * at its original height, not on top. Paste and duplicate deliberately land on
 * top, which is why they are pinned here too.
 */

#include <QTest>
#include <QSignalSpy>
#include <QSet>
#include <QUndoStack>

#include <algorithm>

#include "core/constants.h"
#include "editor/services/ZoneManager.h"
#include "editor/undo/commands/DeleteZoneCommand.h"
#include <PhosphorZones/Zone.h>

using namespace PlasmaZones;

class TestZoneZOrder : public QObject
{
    Q_OBJECT

private:
    /**
     * @brief The whole invariant: zone at index i carries zOrder i, for every i.
     *
     * Deliberately stronger than "the zOrder values form a set 0..n-1": the list
     * order IS the z-order, so a permutation that renumbered zones without
     * reordering them would satisfy a set check and still paint wrongly.
     */
    static void verifyDense(const ZoneManager& manager, const char* context)
    {
        const QVariantList zones = manager.zones();
        for (int i = 0; i < zones.size(); ++i) {
            const QVariantMap zone = zones[i].toMap();
            const QVariant zOrder = zone.value(::PhosphorZones::ZoneJsonKeys::ZOrder);
            QVERIFY2(zOrder.isValid(),
                     qPrintable(QStringLiteral("%1: zone at index %2 carries no zOrder at all")
                                    .arg(QLatin1String(context))
                                    .arg(i)));
            QVERIFY2(zOrder.toInt() == i,
                     qPrintable(QStringLiteral("%1: zone at index %2 has zOrder %3, expected %2")
                                    .arg(QLatin1String(context))
                                    .arg(i)
                                    .arg(zOrder.toInt())));
        }
    }

    /**
     * @brief The number invariant: every zone has a UNIQUE number in 1..99.
     *
     * Numbers are user-owned, so they are NOT tied to list position and gaps are
     * allowed: 1, 4, 7 is a valid state. This checks only that the numbers are
     * distinct and each in range, never that they are contiguous.
     */
    static void verifyNumbersUnique(const ZoneManager& manager, const char* context)
    {
        const QVariantList zones = manager.zones();
        QSet<int> seen;
        for (int i = 0; i < zones.size(); ++i) {
            const QVariantMap zone = zones[i].toMap();
            const int number = zone.value(::PhosphorZones::ZoneJsonKeys::ZoneNumber).toInt();
            QVERIFY2(number >= 1 && number <= 99,
                     qPrintable(QStringLiteral("%1: zone at index %2 has zoneNumber %3, out of the 1..99 range")
                                    .arg(QLatin1String(context))
                                    .arg(i)
                                    .arg(number)));
            QVERIFY2(!seen.contains(number),
                     qPrintable(QStringLiteral("%1: zoneNumber %2 is used by more than one zone")
                                    .arg(QLatin1String(context))
                                    .arg(number)));
            seen.insert(number);
        }
    }

    /// Zone names in list order, i.e. bottom of the stack first.
    static QStringList stackingOrder(const ZoneManager& manager)
    {
        QStringList names;
        const QVariantList zones = manager.zones();
        for (const QVariant& zoneVar : zones) {
            names.append(zoneVar.toMap().value(::PhosphorZones::ZoneJsonKeys::Name).toString());
        }
        return names;
    }

    /// Three non-overlapping zones named A, B, C, stacked bottom-to-top in that order.
    static QStringList addThreeZones(ZoneManager& manager)
    {
        QStringList ids;
        ids << manager.addZone(0.0, 0.0, 0.2, 0.2);
        ids << manager.addZone(0.3, 0.0, 0.2, 0.2);
        ids << manager.addZone(0.6, 0.0, 0.2, 0.2);
        manager.updateZoneName(ids[0], QStringLiteral("A"));
        manager.updateZoneName(ids[1], QStringLiteral("B"));
        manager.updateZoneName(ids[2], QStringLiteral("C"));
        return ids;
    }

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════
    // Density across every mutation
    // ═══════════════════════════════════════════════════════════════════════

    void testAddZone_isDense()
    {
        ZoneManager manager;
        addThreeZones(manager);
        QCOMPARE(manager.zoneCount(), 3);
        verifyDense(manager, "after addZone");
    }

    /// Deleting the BOTTOM zone shifts every zone above it down one.
    void testDeleteZone_recompactsAfterRemovingBottom()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager);

        manager.deleteZone(ids[0]);

        QCOMPARE(manager.zoneCount(), 2);
        verifyDense(manager, "after deleting the bottom zone");
        QCOMPARE(stackingOrder(manager), QStringList({QStringLiteral("B"), QStringLiteral("C")}));
    }

    /// Deleting a MIDDLE zone is the case that used to leave a hole in the run.
    void testDeleteZone_recompactsAfterRemovingMiddle()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager);

        manager.deleteZone(ids[1]);

        QCOMPARE(manager.zoneCount(), 2);
        verifyDense(manager, "after deleting a middle zone");
        QCOMPARE(stackingOrder(manager), QStringList({QStringLiteral("A"), QStringLiteral("C")}));
    }

    /// deleteZoneWithFill removes from the list like any delete, whatever the fill does after.
    void testDeleteZoneWithFill_isDense()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager);

        manager.deleteZoneWithFill(ids[1], true);

        QCOMPARE(manager.zoneCount(), 2);
        verifyDense(manager, "after deleteZoneWithFill");
        verifyNumbersUnique(manager, "after deleteZoneWithFill");
        QCOMPARE(stackingOrder(manager), QStringList({QStringLiteral("A"), QStringLiteral("C")}));
    }

    /// Deleting the ONLY zone: the boundary the three-zone cases never reach.
    void testDeleteZone_theOnlyZoneLeavesAnEmptyList()
    {
        ZoneManager manager;
        const QString id = manager.addZone(0.0, 0.0, 0.2, 0.2);
        QVERIFY(!id.isEmpty());

        manager.deleteZone(id);

        // verifyDense is a no-op loop on an empty list, so the count carries the
        // assertion here.
        QCOMPARE(manager.zoneCount(), 0);
        verifyDense(manager, "after deleting the only zone");
        verifyNumbersUnique(manager, "after deleting the only zone");
    }

    /// clearAllZones mutates the list too, and an empty list is trivially dense.
    void testClearAllZones_leavesAnEmptyList()
    {
        ZoneManager manager;
        addThreeZones(manager);

        manager.clearAllZones();

        QCOMPARE(manager.zoneCount(), 0);
        verifyDense(manager, "after clearAllZones");
        verifyNumbersUnique(manager, "after clearAllZones");

        // Adding after a clear restarts the run at 0/1 rather than continuing it.
        const QString id = manager.addZone(0.0, 0.0, 0.2, 0.2);
        QCOMPARE(manager.zoneCount(), 1);
        verifyDense(manager, "after adding to a cleared list");
        verifyNumbersUnique(manager, "after adding to a cleared list");
        QCOMPARE(manager.getZoneById(id).value(::PhosphorZones::ZoneJsonKeys::ZOrder).toInt(), 0);
    }

    /**
     * @brief setZones stamps zOrder from list order.
     *
     * The saved layout format does not persist zOrder, so a parsed layout carries
     * none at all. Without the stamp every zone reads zOrder 0 and the whole
     * stack collapses to one z.
     */
    void testSetZones_stampsZOrderOnZoneDataThatHasNone()
    {
        ZoneManager manager;

        QVariantList zones;
        for (int i = 0; i < 3; ++i) {
            QVariantMap zone;
            zone[::PhosphorZones::ZoneJsonKeys::Id] = QUuid::createUuid().toString();
            zone[::PhosphorZones::ZoneJsonKeys::Name] = QStringLiteral("Zone %1").arg(i);
            zone[::PhosphorZones::ZoneJsonKeys::X] = 0.1 * i;
            zone[::PhosphorZones::ZoneJsonKeys::Y] = 0.0;
            zone[::PhosphorZones::ZoneJsonKeys::Width] = 0.05;
            zone[::PhosphorZones::ZoneJsonKeys::Height] = 0.05;
            // Deliberately no ZOrder key, exactly as a parsed layout arrives.
            zones.append(zone);
        }
        manager.setZones(zones);

        QCOMPARE(manager.zoneCount(), 3);
        verifyDense(manager, "after setZones on zone data carrying no zOrder");
    }

    /**
     * @brief restoreZones stamps zOrder from LIST ORDER, not from the carried values.
     *
     * This is what ChangeZOrderCommand rests on: it stores whole reordered lists
     * and replays them, so the zOrder each map carries was numbered against the
     * list it came from and is stale by definition. Feeding a reversed list whose
     * carried zOrder still describes the OLD stacking pins that the list wins.
     */
    void testRestoreZones_stampsZOrderFromListOrderNotFromTheCarriedValues()
    {
        ZoneManager manager;
        addThreeZones(manager);
        QVariantList snapshot = manager.zones(); // A(0), B(1), C(2)

        // Reverse the list but leave the stale zOrder in place, so the carried
        // values (2, 1, 0 after reversal) contradict the new list order.
        std::reverse(snapshot.begin(), snapshot.end());
        manager.restoreZones(snapshot);

        QCOMPARE(manager.zoneCount(), 3);
        verifyDense(manager, "after restoreZones on a reordered list");
        // C is now at the bottom because it is first in the list, regardless of
        // the zOrder 2 its map still carried.
        QCOMPARE(stackingOrder(manager), QStringList({QStringLiteral("C"), QStringLiteral("B"), QStringLiteral("A")}));
    }

    /// A snapshot whose maps carry no zOrder at all, as a parsed layout arrives.
    void testRestoreZones_isDenseWhenZoneDataCarriesNoZOrder()
    {
        ZoneManager manager;
        addThreeZones(manager);
        QVariantList snapshot = manager.zones();

        for (QVariant& zoneVar : snapshot) {
            QVariantMap zone = zoneVar.toMap();
            zone.remove(QString::fromLatin1(::PhosphorZones::ZoneJsonKeys::ZOrder));
            zoneVar = zone;
        }
        manager.restoreZones(snapshot);

        QCOMPARE(manager.zoneCount(), 3);
        verifyDense(manager, "after restoreZones on zone data carrying no zOrder");
    }

    /// restoreZones must close the hole a dropped duplicate leaves in the run.
    void testRestoreZones_isDenseEvenWhenADuplicateIsDropped()
    {
        ZoneManager manager;
        addThreeZones(manager);
        QVariantList snapshot = manager.zones();

        // Collapse every carried zOrder to a single stale value, so only a stamp
        // driven by list order can pull the run apart again.
        for (QVariant& zoneVar : snapshot) {
            QVariantMap zone = zoneVar.toMap();
            zone[::PhosphorZones::ZoneJsonKeys::ZOrder] = 0;
            zoneVar = zone;
        }
        // Re-append the middle zone: restoreZones keeps the first occurrence and
        // skips the duplicate, so the surviving run must still be recompacted.
        snapshot.append(snapshot[1]);
        manager.restoreZones(snapshot);

        QCOMPARE(manager.zoneCount(), 3);
        verifyDense(manager, "after restoreZones dropped a duplicate");
        QCOMPARE(stackingOrder(manager), QStringList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
    }

    /// Paste goes through addZoneFromMap with a fresh id, and lands on top.
    void testPaste_isDenseAndLandsOnTop()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager);

        // Paste a copy of the BOTTOM zone. On top is the correct answer here.
        QVariantMap source = manager.getZoneById(ids[0]);
        source[::PhosphorZones::ZoneJsonKeys::X] = 0.0;
        source[::PhosphorZones::ZoneJsonKeys::Y] = 0.5;
        const QString pastedId = manager.addZoneFromMap(source);
        QVERIFY(!pastedId.isEmpty());

        QCOMPARE(manager.zoneCount(), 4);
        verifyDense(manager, "after paste");
        // A paste must not reuse the source id, or it would overwrite it.
        QVERIFY(pastedId != ids[0]);
        QCOMPARE(manager.getZoneById(pastedId).value(::PhosphorZones::ZoneJsonKeys::ZOrder).toInt(), 3);
    }

    void testDuplicate_isDenseAndLandsOnTop()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager);

        // Duplicate the BOTTOM zone; the copy belongs on top.
        const QString copyId = manager.duplicateZone(ids[0]);
        QVERIFY(!copyId.isEmpty());

        QCOMPARE(manager.zoneCount(), 4);
        verifyDense(manager, "after duplicate");
        QCOMPARE(manager.getZoneById(copyId).value(::PhosphorZones::ZoneJsonKeys::ZOrder).toInt(), 3);
    }

    void testSplit_isDense()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager);

        // Split the BOTTOM zone; the new half goes on top.
        const QString newId = manager.splitZone(ids[0], true);
        QVERIFY(!newId.isEmpty());

        QCOMPARE(manager.zoneCount(), 4);
        verifyDense(manager, "after split");
        QCOMPARE(manager.getZoneById(newId).value(::PhosphorZones::ZoneJsonKeys::ZOrder).toInt(), 3);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Z-order operations
    // ═══════════════════════════════════════════════════════════════════════

    void testBringToFront_isDenseAndReordersStack()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager);

        manager.bringToFront(ids[0]);

        verifyDense(manager, "after bringToFront");
        QCOMPARE(stackingOrder(manager), QStringList({QStringLiteral("B"), QStringLiteral("C"), QStringLiteral("A")}));
        QCOMPARE(manager.getZoneById(ids[0]).value(::PhosphorZones::ZoneJsonKeys::ZOrder).toInt(), 2);
    }

    void testSendToBack_isDenseAndReordersStack()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager);

        manager.sendToBack(ids[2]);

        verifyDense(manager, "after sendToBack");
        QCOMPARE(stackingOrder(manager), QStringList({QStringLiteral("C"), QStringLiteral("A"), QStringLiteral("B")}));
        QCOMPARE(manager.getZoneById(ids[2]).value(::PhosphorZones::ZoneJsonKeys::ZOrder).toInt(), 0);
    }

    void testBringForward_isDenseAndSwapsWithTheZoneAbove()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager);

        manager.bringForward(ids[0]);

        verifyDense(manager, "after bringForward");
        QCOMPARE(stackingOrder(manager), QStringList({QStringLiteral("B"), QStringLiteral("A"), QStringLiteral("C")}));
    }

    void testSendBackward_isDenseAndSwapsWithTheZoneBelow()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager);

        manager.sendBackward(ids[2]);

        verifyDense(manager, "after sendBackward");
        QCOMPARE(stackingOrder(manager), QStringList({QStringLiteral("A"), QStringLiteral("C"), QStringLiteral("B")}));
    }

    /// A z-order op at the boundary is a no-op and must not disturb the run.
    void testZOrderOpsAtBoundaries_areNoOpsAndStayDense()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager);
        const QStringList before = stackingOrder(manager);

        manager.bringToFront(ids[2]); // already on top
        manager.sendToBack(ids[0]); // already at the bottom
        manager.bringForward(ids[2]); // already on top
        manager.sendBackward(ids[0]); // already at the bottom

        verifyDense(manager, "after boundary z-order no-ops");
        QCOMPARE(stackingOrder(manager), before);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // The undo stacking contract
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Undoing a delete returns the zone to its ORIGINAL height, not the top.
     *
     * Zones [A(0), B(1), C(2)]: deleting A recompacts to [B(0), C(1)], and undo
     * must restore [A(0), B(1), C(2)] rather than appending A at zOrder 2. The
     * user put A at the bottom; an undo that hands it back on top has not undone
     * anything, it has restacked the layout.
     */
    void testUndoDelete_restoresTheZoneToItsOriginalPositionInTheStack()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager);

        const QVariantMap zoneData = manager.getZoneById(ids[0]);
        QCOMPARE(zoneData.value(::PhosphorZones::ZoneJsonKeys::ZOrder).toInt(), 0);

        QUndoStack stack;
        stack.push(new DeleteZoneCommand(QPointer<ZoneManager>(&manager), ids[0], zoneData, QString()));

        QCOMPARE(manager.zoneCount(), 2);
        verifyDense(manager, "after the delete command");
        QCOMPARE(stackingOrder(manager), QStringList({QStringLiteral("B"), QStringLiteral("C")}));

        stack.undo();

        QCOMPARE(manager.zoneCount(), 3);
        verifyDense(manager, "after undoing the delete");
        // A is back at the BOTTOM, where the user had it.
        QCOMPARE(stackingOrder(manager), QStringList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
        QCOMPARE(manager.getZoneById(ids[0]).value(::PhosphorZones::ZoneJsonKeys::ZOrder).toInt(), 0);
    }

    /**
     * @brief Undoing a delete restores the zone's ORIGINAL number, not a repositioned one.
     *
     * Numbers are user-owned. Zones numbered 1, 4, 7: deleting the "4" leaves the
     * survivors on 1 and 7 (a gap on 4 is valid, nothing collapses to 1, 2). The
     * delete freed the number 4, so undo replays the pre-delete snapshot and the
     * restored zone carries 4 again, at its original height in the stack.
     */
    void testUndoDelete_restoresTheOriginalNumber()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager); // A(1), B(2), C(3)
        // Renumber to a gapped 1, 4, 7 to prove numbers are user-owned.
        manager.updateZoneNumber(ids[1], 4);
        manager.updateZoneNumber(ids[2], 7);
        verifyNumbersUnique(manager, "after numbering 1, 4, 7");

        const QVariantMap zoneData = manager.getZoneById(ids[1]); // B, number 4, zOrder 1
        QCOMPARE(zoneData.value(::PhosphorZones::ZoneJsonKeys::ZoneNumber).toInt(), 4);
        QCOMPARE(zoneData.value(::PhosphorZones::ZoneJsonKeys::ZOrder).toInt(), 1);

        QUndoStack stack;
        stack.push(new DeleteZoneCommand(QPointer<ZoneManager>(&manager), ids[1], zoneData, QString()));

        // The survivors keep 1 and 7 — deleting the "4" did NOT renumber them.
        QCOMPARE(manager.zoneCount(), 2);
        QCOMPARE(manager.getZoneById(ids[0]).value(::PhosphorZones::ZoneJsonKeys::ZoneNumber).toInt(), 1);
        QCOMPARE(manager.getZoneById(ids[2]).value(::PhosphorZones::ZoneJsonKeys::ZoneNumber).toInt(), 7);
        verifyNumbersUnique(manager, "after deleting the 4");

        stack.undo();

        QCOMPARE(manager.zoneCount(), 3);
        verifyNumbersUnique(manager, "after undoing the delete");
        verifyDense(manager, "after undoing the delete");
        // The restored zone is 4 again, back in the middle of the stack.
        QCOMPARE(manager.getZoneById(ids[1]).value(::PhosphorZones::ZoneJsonKeys::ZoneNumber).toInt(), 4);
        QCOMPARE(manager.getZoneById(ids[1]).value(::PhosphorZones::ZoneJsonKeys::ZOrder).toInt(), 1);
        QCOMPARE(stackingOrder(manager), QStringList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));

        // Redo re-deletes and a second undo must land on the same numbers, so the
        // restore is not a one-shot that a replay can slip past.
        stack.redo();
        QCOMPARE(manager.getZoneById(ids[2]).value(::PhosphorZones::ZoneJsonKeys::ZoneNumber).toInt(), 7);
        stack.undo();
        QCOMPARE(manager.zoneCount(), 3);
        QCOMPARE(manager.getZoneById(ids[1]).value(::PhosphorZones::ZoneJsonKeys::ZoneNumber).toInt(), 4);
        verifyNumbersUnique(manager, "after the second undo");
        verifyDense(manager, "after the second undo");
    }

    /// A colliding paste takes the next-highest number; a non-colliding one keeps its own.
    void testPaste_collidingNumberGetsNextHighest()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager); // numbers 1, 2, 3

        // Paste a copy of the bottom zone, whose number 1 collides with a live
        // zone. It must be reassigned to max(1, 2, 3) + 1 = 4.
        QVariantMap colliding = manager.getZoneById(ids[0]); // zoneNumber 1
        colliding[::PhosphorZones::ZoneJsonKeys::Y] = 0.5;
        const QString collidingId = manager.addZoneFromMap(colliding);
        QVERIFY(!collidingId.isEmpty());
        QCOMPARE(manager.getZoneById(collidingId).value(::PhosphorZones::ZoneJsonKeys::ZoneNumber).toInt(), 4);
        verifyNumbersUnique(manager, "after a colliding paste");

        // Now paste with a number that is free (60): it must be kept verbatim, not
        // bumped to the top, because a user-owned number that does not collide is
        // never touched.
        QVariantMap free = manager.getZoneById(ids[0]);
        free[::PhosphorZones::ZoneJsonKeys::Y] = 0.7;
        free[::PhosphorZones::ZoneJsonKeys::ZoneNumber] = 60;
        const QString freeId = manager.addZoneFromMap(free);
        QVERIFY(!freeId.isEmpty());
        QCOMPARE(manager.getZoneById(freeId).value(::PhosphorZones::ZoneJsonKeys::ZoneNumber).toInt(), 60);
        verifyNumbersUnique(manager, "after a non-colliding paste");
    }

    /// Deleting a zone leaves a gap in the numbering rather than renumbering survivors.
    void testDelete_leavesAGapNotARenumber()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager); // numbers 1, 2, 3

        manager.deleteZone(ids[1]); // remove the "2"

        QCOMPARE(manager.zoneCount(), 2);
        // The survivors keep 1 and 3 — they are NOT collapsed to 1, 2.
        QCOMPARE(manager.getZoneById(ids[0]).value(::PhosphorZones::ZoneJsonKeys::ZoneNumber).toInt(), 1);
        QCOMPARE(manager.getZoneById(ids[2]).value(::PhosphorZones::ZoneJsonKeys::ZoneNumber).toInt(), 3);
        verifyNumbersUnique(manager, "after deleting the middle zone");
        // zOrder still recompacts to a dense run.
        verifyDense(manager, "after deleting the middle zone");
    }

    /// A new zone drawn into a gapped layout takes the next highest, not a gap or a position.
    void testNewZone_withAGapGetsNextHighest()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager);
        manager.updateZoneNumber(ids[1], 4);
        manager.updateZoneNumber(ids[2], 7); // numbers are now 1, 4, 7

        const QString newId = manager.addZone(0.0, 0.5, 0.2, 0.2);
        QVERIFY(!newId.isEmpty());

        // 8 = max(1, 4, 7) + 1. NOT 5 (the count), NOT 2 (the lowest gap).
        QCOMPARE(manager.getZoneById(newId).value(::PhosphorZones::ZoneJsonKeys::ZoneNumber).toInt(), 8);
        verifyNumbersUnique(manager, "after drawing a new zone into a gapped layout");
    }

    /// Duplicate hands the copy the next-highest number, reading live state.
    void testDuplicate_getsNextHighest()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager);
        manager.updateZoneNumber(ids[1], 4);
        manager.updateZoneNumber(ids[2], 7); // numbers are now 1, 4, 7

        const QString copyId = manager.duplicateZone(ids[0]);
        QVERIFY(!copyId.isEmpty());

        QCOMPARE(manager.getZoneById(copyId).value(::PhosphorZones::ZoneJsonKeys::ZoneNumber).toInt(), 8);
        verifyNumbersUnique(manager, "after duplicating into a gapped layout");
    }

    /// Same contract for a zone deleted from the middle of the stack.
    void testUndoDelete_restoresAMiddleZoneToItsOriginalPosition()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager);

        const QVariantMap zoneData = manager.getZoneById(ids[1]);
        QUndoStack stack;
        stack.push(new DeleteZoneCommand(QPointer<ZoneManager>(&manager), ids[1], zoneData, QString()));
        QCOMPARE(stackingOrder(manager), QStringList({QStringLiteral("A"), QStringLiteral("C")}));

        stack.undo();

        verifyDense(manager, "after undoing a middle delete");
        QCOMPARE(stackingOrder(manager), QStringList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
        QCOMPARE(manager.getZoneById(ids[1]).value(::PhosphorZones::ZoneJsonKeys::ZOrder).toInt(), 1);
    }

    /// Deleting the top zone: undo puts it back on top, which is also its original height.
    void testUndoDelete_restoresATopZoneToTheTop()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager);

        const QVariantMap zoneData = manager.getZoneById(ids[2]);
        QUndoStack stack;
        stack.push(new DeleteZoneCommand(QPointer<ZoneManager>(&manager), ids[2], zoneData, QString()));
        stack.undo();

        verifyDense(manager, "after undoing a top delete");
        QCOMPARE(stackingOrder(manager), QStringList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
        QCOMPARE(manager.getZoneById(ids[2]).value(::PhosphorZones::ZoneJsonKeys::ZOrder).toInt(), 2);
    }

    /// Redo re-deletes, and a second undo must land in the same place as the first.
    void testUndoDelete_survivesRedoAndUndoAgain()
    {
        ZoneManager manager;
        const QStringList ids = addThreeZones(manager);

        const QVariantMap zoneData = manager.getZoneById(ids[0]);
        QUndoStack stack;
        stack.push(new DeleteZoneCommand(QPointer<ZoneManager>(&manager), ids[0], zoneData, QString()));

        stack.undo();
        stack.redo();
        QCOMPARE(manager.zoneCount(), 2);
        verifyDense(manager, "after redoing the delete");

        stack.undo();
        QCOMPARE(manager.zoneCount(), 3);
        verifyDense(manager, "after the second undo");
        QCOMPARE(stackingOrder(manager), QStringList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}));
    }
};

QTEST_MAIN(TestZoneZOrder)
#include "test_zone_zorder.moc"
