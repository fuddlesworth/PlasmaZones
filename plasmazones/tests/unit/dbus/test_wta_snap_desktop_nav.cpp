// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_wta_snap_desktop_nav.cpp
 * @brief Snap focus at the edge of a layout with no neighbour output: it
 *        steps onto the next desktop and enters at that desktop's first zone
 *        (its last stepping back), taking only windows that desktop's own
 *        store holds in the current activity (F387, F146, F225, F340).
 */

#include "wta_snap_nav_fixture.h"

#include <QSignalSpy>

using PhosphorEngine::NavigationContext;
using PhosphorSnapEngine::SnapEngine;
using PhosphorSnapEngine::SnapState;

class TestWtaSnapDesktopNav : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // The window in zone 1 of the next desktop is entered stepping right,
    // though another window there sorts first by id (F387).
    void entersAtTheFirstZoneOfTheNextDesktop()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        twoDesktops(f);
        const QString w = f.live(QStringLiteral("edge"));
        const QString zz = f.live(QStringLiteral("zz"));
        const QString aa = f.live(QStringLiteral("aa"));
        f.snapOn(zz, {f.zone(0)}, kLeft, 2);
        f.snapOn(aa, {f.zone(2)}, kLeft, 2);
        f.snapOn(w, {f.zone(2)}, kLeft, 1);
        QSignalSpy activate(f.snap.get(), &PhosphorEngine::PlacementEngineBase::activateWindowRequested);
        f.snap->focusInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(activate.count(), 1);
        QCOMPARE(activate.first().at(0).toString(), zz);
    }

    // Stepping back enters at the last zone, and a float waits while a
    // snapped window is there (F387).
    void stepsBackIntoTheLastZoneBeforeAnyFloat()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        twoDesktops(f);
        const QString w = f.live(QStringLiteral("edge"));
        const QString first = f.live(QStringLiteral("zz"));
        const QString last = f.live(QStringLiteral("mm"));
        const QString floater = f.live(QStringLiteral("zzz"));
        f.snapOn(first, {f.zone(0)}, kLeft, 1);
        f.snapOn(last, {f.zone(2)}, kLeft, 1);
        f.snapOn(floater, {f.zone(1)}, kLeft, 1);
        f.snap->setWindowFloat(floater, true, kLeft);
        QVERIFY(f.snap->isFloating(floater));
        f.showDesktop(kLeft, 2);
        f.snapOn(w, {f.zone(0)}, kLeft, 2);
        QSignalSpy activate(f.snap.get(), &PhosphorEngine::PlacementEngineBase::activateWindowRequested);
        f.snap->focusInDirection(QStringLiteral("left"), NavigationContext{w, kLeft});
        QCOMPARE(activate.count(), 1);
        QCOMPARE(activate.first().at(0).toString(), last);
    }

    // A window snapped on the next desktop under another activity is not
    // that desktop's in this one (F225).
    void skipsAnotherActivitysWindows()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        twoDesktops(f);
        const QString other = f.live(QStringLiteral("aa"));
        const QString w = f.live(QStringLiteral("edge"));
        f.layouts->setCurrentActivity(QStringLiteral("other"));
        f.snapOn(other, {f.zone(0)}, kLeft, 2);
        f.layouts->setCurrentActivity(QString());
        f.snapOn(w, {f.zone(2)}, kLeft, 1);
        QSignalSpy activate(f.snap.get(), &PhosphorEngine::PlacementEngineBase::activateWindowRequested);
        QSignalSpy feedback(f.snap.get(), &PhosphorEngine::PlacementEngineBase::navigationFeedback);
        f.snap->focusInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(activate.count(), 0);
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_adjacent_zone"));
    }

    // Minimized windows, and windows already on the desktop in view, are not
    // where focus steps to (F146).
    void skipsMinimizedAndAlreadyVisibleWindows()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        twoDesktops(f);
        const QString minimized = f.registerWindow(QStringLiteral("min"), {}, {}, true);
        PhosphorEngine::WindowMetadata both;
        both.appId = QStringLiteral("app");
        both.isMinimized = false;
        both.virtualDesktop = 1;
        both.virtualDesktops = {1, 2};
        f.registry.upsert(QStringLiteral("both"), both);
        const QString onBoth = f.registry.canonicalizeWindowId(QStringLiteral("app|both"));
        PhosphorEngine::WindowMetadata pinned;
        pinned.appId = QStringLiteral("app");
        pinned.isMinimized = false;
        pinned.isSticky = true;
        f.registry.upsert(QStringLiteral("pinned"), pinned);
        const QString sticky = f.registry.canonicalizeWindowId(QStringLiteral("app|pinned"));
        const QString w = f.live(QStringLiteral("edge"));
        f.snapOn(minimized, {f.zone(0)}, kLeft, 2);
        f.snapOn(onBoth, {f.zone(1)}, kLeft, 2);
        f.snapOn(sticky, {f.zone(2)}, kLeft, 2);
        f.snapOn(w, {f.zone(2)}, kLeft, 1);
        QSignalSpy activate(f.snap.get(), &PhosphorEngine::PlacementEngineBase::activateWindowRequested);
        f.snap->focusInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(activate.count(), 0);
    }

    // The focused window, named by an id its class has since changed, is not
    // its own target on the next desktop (F340).
    void skipsAClassMutatedSelf()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        twoDesktops(f);
        const QString w = f.live(QStringLiteral("1"));
        const QString renamed = QStringLiteral("renamed|1");
        QCOMPARE(f.registry.canonicalizeForLookup(renamed), w);
        f.snapOn(w, {f.zone(2)}, kLeft, 1);
        f.snap->stateForWindowOnScreen(w, kLeft, 2)->assignWindowToZone(w, f.zone(0), kLeft, 2);
        QSignalSpy activate(f.snap.get(), &PhosphorEngine::PlacementEngineBase::activateWindowRequested);
        f.snap->focusInDirection(QStringLiteral("right"), NavigationContext{renamed, kLeft});
        QCOMPARE(activate.count(), 0);
    }

    // A desktop that runs autotile is not entered through a zone a window
    // still remembers there from before (F225).
    void tilingDesktopIsNotEntered()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        twoDesktops(f);
        const QString frozen = f.live(QStringLiteral("aa"));
        const QString w = f.live(QStringLiteral("edge"));
        f.snapOn(frozen, {f.zone(0)}, kLeft, 2);
        f.snapOn(w, {f.zone(2)}, kLeft, 1);
        f.setMode(kLeft, 2, PhosphorZones::AssignmentEntry::Autotile);
        QSignalSpy activate(f.snap.get(), &PhosphorEngine::PlacementEngineBase::activateWindowRequested);
        QSignalSpy feedback(f.snap.get(), &PhosphorEngine::PlacementEngineBase::navigationFeedback);
        f.snap->focusInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(activate.count(), 0);
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_adjacent_zone"));
    }

    // ── L14.10: the keyboard move to the next desktop ──

    // The moved window is stated snapped in the zone it lands in, holds no
    // zone on the desktop it left, and the record's snap slot names the new
    // zone on the new desktop (F534, F106, F458).
    void movedWindowIsStatedSnappedInItsNewZone()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        twoDesktops(f);
        PhosphorZones::Layout* other = secondLayout(f);
        const QString mapped = other->zones().at(2)->id().toString();
        const QString w = f.live(QStringLiteral("mv-1"), QRect(1300, 100, 400, 300));
        f.snapOn(w, {f.zone(2)}, kLeft, 1);
        QStringList snapped;
        QObject::connect(f.snap.get(), &SnapEngine::windowSnapStateChanged, f.snap.get(),
                         [&](const QString&, const PhosphorProtocol::WindowStateEntry& entry) {
                             snapped.append(entry.changeType + QLatin1Char(':') + entry.zoneId);
                         });
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(snapped, (QStringList{QStringLiteral("snapped:") + mapped}));
        QCOMPARE(f.snap->stateForWindowOnScreen(w, kLeft, 2)->zonesForWindow(w), (QStringList{mapped}));
        QVERIFY(!f.snap->heldKeyForWindow(w) || f.snap->heldKeyForWindow(w)->desktop == 2);
        QVERIFY(f.wta->service()->windowsInZone(f.zone(2)).isEmpty());
        const auto record = f.wta->service()->placementStore().peekExact(w);
        QVERIFY(record.has_value());
        QCOMPARE(record->virtualDesktop, 2);
        QCOMPARE(record->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).zoneIds, (QStringList{mapped}));
        QVERIFY(f.wta->service()->userSnappedClasses().contains(QStringLiteral("app")));
    }

    // A window on both desktops keeps the zone it already holds on the one it
    // is moved to (F611).
    void keepsTheZoneItAlreadyHasThere()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        twoDesktops(f);
        const QString w = onBothDesktops(f, QStringLiteral("keep-1"));
        f.snapOn(w, {f.zone(2)}, kLeft, 1);
        f.snap->stateForWindowOnScreen(w, kLeft, 2)->assignWindowToZone(w, f.zone(0), kLeft, 2);
        f.showDesktop(kLeft, 1);
        QRect applied;
        QObject::connect(f.snap.get(), &SnapEngine::applyGeometryRequested, f.snap.get(),
                         [&](const QString&, int x, int y, int width, int height) {
                             applied = QRect(x, y, width, height);
                         });
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(f.snap->stateForWindowOnScreen(w, kLeft, 2)->zonesForWindow(w), (QStringList{f.zone(0)}));
        QCOMPARE(applied, f.zoneRect(0, kLeft));
        const SnapState* left = f.snap->stateForWindowOnScreen(w, kLeft, 1);
        QVERIFY(left->zonesForWindow(w).isEmpty());
    }

    // A window floating on the desktop it is moved to stays floating there,
    // back at its float-back, applied before the desktop move (F619).
    void keepsTheFloatItHasThere()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        twoDesktops(f);
        const QString w = onBothDesktops(f, QStringLiteral("float-1"));
        const QRect free(300, 200, 640, 480);
        f.wta->service()->recordFreeGeometry(w, kLeft, free, true);
        f.snapOn(w, {f.zone(2)}, kLeft, 1);
        SnapState* there = f.snap->stateForWindowOnScreen(w, kLeft, 2);
        there->setFloatingOnScreen(w, kLeft, 2);
        there->setFloating(w, true);
        f.showDesktop(kLeft, 1);
        QStringList order;
        QRect applied;
        QObject::connect(f.snap.get(), &SnapEngine::applyGeometryRequested, f.snap.get(),
                         [&](const QString&, int x, int y, int width, int height, const QString& zoneId) {
                             applied = QRect(x, y, width, height);
                             order.append(zoneId.isEmpty() ? QStringLiteral("free") : QStringLiteral("zone"));
                         });
        QObject::connect(f.snap.get(), &PhosphorEngine::PlacementEngineBase::windowDesktopMoveRequested, f.snap.get(),
                         [&](const QString&, int desktop) {
                             order.append(QStringLiteral("desktop:%1").arg(desktop));
                         });
        QStringList stated;
        QObject::connect(f.snap.get(), &SnapEngine::windowSnapStateChanged, f.snap.get(),
                         [&](const QString&, const PhosphorProtocol::WindowStateEntry& entry) {
                             stated.append(entry.changeType);
                         });
        QSignalSpy floated(f.snap.get(), &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(order, (QStringList{QStringLiteral("free"), QStringLiteral("desktop:2")}));
        QCOMPARE(applied, free);
        QCOMPARE(stated, (QStringList{QStringLiteral("unsnapped")}));
        QCOMPARE(floated.count(), 1);
        QCOMPARE(floated.first().at(1).toBool(), true);
        QVERIFY(there->isFloating(w));
        QVERIFY(there->zonesForWindow(w).isEmpty());
    }

    // A window on every desktop is not moved (F289).
    void stickyWindowIsNotMoved()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        twoDesktops(f);
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.isMinimized = false;
        meta.isSticky = true;
        f.registry.upsert(QStringLiteral("pin-1"), meta);
        const QString w = f.registry.canonicalizeWindowId(QStringLiteral("app|pin-1"));
        f.setFrame(w, QRect(1300, 100, 400, 300));
        f.snapOn(w, {f.zone(2)}, kLeft, 1);
        QSignalSpy moved(f.snap.get(), &PhosphorEngine::PlacementEngineBase::windowDesktopMoveRequested);
        QSignalSpy feedback(f.snap.get(), &PhosphorEngine::PlacementEngineBase::navigationFeedback);
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(moved.count(), 0);
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_adjacent_zone"));
        QCOMPARE(f.snap->stateForWindowOnScreen(w, kLeft, 1)->zonesForWindow(w), (QStringList{f.zone(2)}));
    }

    // With no slot on the next desktop only the compositor moves the window,
    // and the zone it holds here is left for the membership pass (F289).
    void noSlotLeavesTheSourceStoreAlone()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        twoDesktops(f);
        auto* single = PlasmaZones::createTestLayout(1, f.layouts);
        f.layouts->addLayout(single);
        f.layouts->assignLayout(kLeft, 2, QString(), single);
        const QString w = f.live(QStringLiteral("slot-1"), QRect(1300, 100, 400, 300));
        f.snapOn(w, {f.zone(2)}, kLeft, 1);
        QSignalSpy moved(f.snap.get(), &PhosphorEngine::PlacementEngineBase::windowDesktopMoveRequested);
        QSignalSpy feedback(f.snap.get(), &PhosphorEngine::PlacementEngineBase::navigationFeedback);
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(moved.count(), 1);
        QCOMPARE(feedback.last().at(0).toBool(), true);
        const SnapState* source = f.snap->stateForWindowOnScreen(w, kLeft, 1);
        QCOMPARE(source->desktopForWindow(w), 1);
        QCOMPARE(source->zonesForWindow(w), (QStringList{f.zone(2)}));
    }

    // A span keeps its zones on a desktop with the same layout (Q3).
    void spanKeepsItsZonesOnASharedLayout()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        twoDesktops(f);
        const QString w = f.live(QStringLiteral("span-1"), QRect(700, 100, 1200, 300));
        f.snapOn(w, {f.zone(1), f.zone(2)}, kLeft, 1);
        QRect applied;
        QObject::connect(f.snap.get(), &SnapEngine::applyGeometryRequested, f.snap.get(),
                         [&](const QString&, int x, int y, int width, int height) {
                             applied = QRect(x, y, width, height);
                         });
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(f.snap->stateForWindowOnScreen(w, kLeft, 2)->zonesForWindow(w), (QStringList{f.zone(1), f.zone(2)}));
        QCOMPARE(applied, f.wta->service()->resolveZoneGeometry({f.zone(1), f.zone(2)}, kLeft));
    }

    // On another layout a span takes its first zone's position alone (Q3).
    void spanTakesItsFirstZonesSlotOnAnotherLayout()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        twoDesktops(f);
        PhosphorZones::Layout* other = secondLayout(f);
        const QString w = f.live(QStringLiteral("span-2"), QRect(700, 100, 1200, 300));
        f.snapOn(w, {f.zone(1), f.zone(2)}, kLeft, 1);
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(f.snap->stateForWindowOnScreen(w, kLeft, 2)->zonesForWindow(w),
                 (QStringList{other->zones().at(1)->id().toString()}));
    }

    // ── L14.11: moves to a desktop running another mode ──

    // A move onto a scrolling desktop is left to the scroll engine's arrival
    // once that desktop is shown: no receive now, the desktop move, and snap
    // lets the window go (F305).
    void moveToAScrollingDesktopIsAReactiveArrival()
    {
        StubPlacementEngine scroll;
        scroll.id = QStringLiteral("scrolling");
        SnapNavFixture f;
        QVERIFY(f.ready());
        twoDesktops(f);
        f.wta->setEngines(f.snap.get(), &f.tiling, &scroll);
        f.setMode(kLeft, 2, PhosphorZones::AssignmentEntry::Scrolling);
        const QString w = f.live(QStringLiteral("scr-1"), QRect(700, 100, 400, 300));
        f.snapOn(w, {f.zone(1)}, kLeft, 1);
        QSignalSpy moved(f.wta, &WindowTrackingAdaptor::windowDesktopMoveRequested);
        Q_EMIT f.snap->crossModeMoveRequested(w, kLeft, 2, QStringLiteral("right"));
        QVERIFY(scroll.received.isEmpty());
        QCOMPARE(moved.count(), 1);
        QCOMPARE(moved.first().at(1).toInt(), 2);
        QVERIFY(!f.snap->isWindowTracked(w));
        const auto record = f.wta->service()->placementStore().peekExact(w);
        QVERIFY(!record || record->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).zoneIds.isEmpty());
        f.wta->setEngines(f.snap.get(), &f.tiling, nullptr);
    }

    // A window moved from a tiling desktop onto a snapping one enters that
    // desktop's own layout (F726).
    void autotileToSnapDesktopEntersThatDesktopsLayout()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        twoDesktops(f);
        PhosphorZones::Layout* other = secondLayout(f);
        f.setMode(kLeft, 1, PhosphorZones::AssignmentEntry::Autotile);
        const QString w = f.live(QStringLiteral("at-1"));
        f.tiling.heldScreen.insert(w, kLeft);
        Q_EMIT f.tiling.crossModeMoveRequested(w, kLeft, 2, QStringLiteral("right"));
        QCOMPARE(f.snap->stateForWindowOnScreen(w, kLeft, 2)->zonesForWindow(w),
                 (QStringList{other->zones().at(0)->id().toString()}));
    }

    // Arriving on a desktop where it already holds a zone, it keeps that zone
    // and is stated snapped there (F611).
    void snapArrivalKeepsTheDestinationsOwnZone()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        twoDesktops(f);
        PhosphorZones::Layout* other = secondLayout(f);
        const QString kept = other->zones().at(2)->id().toString();
        f.setMode(kLeft, 1, PhosphorZones::AssignmentEntry::Autotile);
        const QString w = f.live(QStringLiteral("at-2"));
        f.snap->stateForWindowOnScreen(w, kLeft, 2)->assignWindowToZone(w, kept, kLeft, 2);
        f.tiling.heldScreen.insert(w, kLeft);
        QStringList stated;
        QObject::connect(f.snap.get(), &SnapEngine::windowSnapStateChanged, f.snap.get(),
                         [&](const QString&, const PhosphorProtocol::WindowStateEntry& entry) {
                             stated.append(entry.changeType + QLatin1Char(':') + entry.zoneId);
                         });
        Q_EMIT f.tiling.crossModeMoveRequested(w, kLeft, 2, QStringLiteral("right"));
        QCOMPARE(f.snap->stateForWindowOnScreen(w, kLeft, 2)->zonesForWindow(w), (QStringList{kept}));
        QCOMPARE(stated, (QStringList{QStringLiteral("snapped:") + kept}));
    }

    // A tiling monitor that refuses the window leaves it in its zone, and the
    // OSD says the move failed (F305).
    void refusedMonitorHandoffSaysTheMoveFailed()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        f.setMode(kRight, 1, PhosphorZones::AssignmentEntry::Autotile);
        f.tiling.refuseReceive = true;
        f.cross.outputs.insert(kLeft + QStringLiteral("|right"), kRight);
        const QString w = f.live(QStringLiteral("ref-1"), QRect(1300, 100, 400, 300));
        f.snapOn(w, {f.zone(2)}, kLeft, 1);
        QSignalSpy feedback(f.snap.get(), &PhosphorEngine::PlacementEngineBase::navigationFeedback);
        f.snap->moveFocusedInDirection(QStringLiteral("right"), NavigationContext{w, kLeft});
        QCOMPARE(f.tiling.received.size(), 1);
        QVERIFY(!feedback.isEmpty());
        QCOMPARE(feedback.last().at(0).toBool(), false);
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("swap_failed"));
        QCOMPARE(f.snap->stateForWindowOnScreen(w, kLeft, 1)->zonesForWindow(w), (QStringList{f.zone(2)}));
    }

    // The adjacency resolver answers the edge zone of a named desktop's layout.
    void firstZoneOnDesktopReadsThatDesktopsLayout()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        twoDesktops(f);
        PhosphorZones::Layout* other = secondLayout(f);
        QCOMPARE(f.zda->getFirstZoneInDirectionOnDesktop(QStringLiteral("left"), kLeft, 2),
                 other->zones().at(0)->id().toString());
        QCOMPARE(f.zda->getFirstZoneInDirectionOnDesktop(QStringLiteral("right"), kLeft, 2),
                 other->zones().at(2)->id().toString());
        QCOMPARE(f.zda->getFirstZoneInDirectionOnDesktop(QStringLiteral("left"), kLeft, 0), f.zone(0));
    }

    // ── L14.12: Restore acts on snapped windows only ──

    // A floating window has no zone to restore out of: refused, not moved,
    // and its float-back kept (F190).
    void restoreRefusesAFloatingWindow()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("rf-1"));
        f.snapThenFloat(w, kLeft);
        QVERIFY(f.snap->isFloating(w));
        const QRect free(300, 200, 640, 480);
        f.wta->service()->recordFreeGeometry(w, kLeft, free, true);
        QSignalSpy applies(f.snap.get(), &SnapEngine::applyGeometryRequested);
        QSignalSpy feedback(f.snap.get(), &PhosphorEngine::PlacementEngineBase::navigationFeedback);
        f.snap->restoreFocusedWindow(NavigationContext{w, kLeft});
        QCOMPARE(applies.count(), 0);
        QVERIFY(!feedback.isEmpty());
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("not_snapped"));
        QCOMPARE(f.floatBack(w, kLeft), free);
    }

    // A snapped window goes to its float-back on its own screen, floats, and
    // the record says floating at once; another screen's float-back stays
    // (F843, F353).
    void restoreOfASnappedWindow()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("rs-1"));
        const QRect left(300, 200, 640, 480);
        const QRect right(2200, 200, 640, 480);
        f.wta->service()->recordFreeGeometry(w, kLeft, left, true);
        f.wta->service()->recordFreeGeometry(w, kRight, right, true);
        f.snapOn(w, {f.zone(0)}, kLeft, 1);
        QStringList applied;
        QObject::connect(f.snap.get(), &SnapEngine::applyGeometryRequested, f.snap.get(),
                         [&](const QString&, int x, int y, int width, int height, const QString& zoneId) {
                             applied.append(
                                 QStringLiteral("%1,%2 %3x%4 [%5]").arg(x).arg(y).arg(width).arg(height).arg(zoneId));
                         });
        QSignalSpy floated(f.snap.get(), &PhosphorEngine::PlacementEngineBase::windowFloatingChanged);
        f.snap->restoreFocusedWindow(NavigationContext{w, kLeft});
        QCOMPARE(applied, (QStringList{QStringLiteral("300,200 640x480 []")}));
        QCOMPARE(floated.count(), 1);
        QCOMPARE(floated.first().at(1).toBool(), true);
        QCOMPARE(floated.first().at(2).toString(), kLeft);
        QVERIFY(f.snap->isWindowTracked(w));
        QCOMPARE(f.floatBack(w, kRight), right);
        const auto record = f.wta->service()->placementStore().peekExact(w);
        QVERIFY(record.has_value());
        QCOMPARE(record->slotFor(PhosphorEngine::WindowPlacement::snapEngineId()).state,
                 QString(PhosphorEngine::WindowPlacement::stateFloating()));
    }

    // With no float-back there is nothing to restore to (F843).
    void restoreWithoutAFloatBackDoesNothing()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString w = f.live(QStringLiteral("rn-1"));
        f.snapOn(w, {f.zone(0)}, kLeft, 1);
        QSignalSpy applies(f.snap.get(), &SnapEngine::applyGeometryRequested);
        QSignalSpy feedback(f.snap.get(), &PhosphorEngine::PlacementEngineBase::navigationFeedback);
        f.snap->restoreFocusedWindow(NavigationContext{w, kLeft});
        QCOMPARE(applies.count(), 0);
        QVERIFY(!feedback.isEmpty());
        QCOMPARE(feedback.last().at(0).toBool(), false);
        QCOMPARE(f.snap->stateForWindowOnScreen(w, kLeft, 1)->zonesForWindow(w), (QStringList{f.zone(0)}));
    }

    // ── L14.13: the pre-float zone is per window and per desktop ──

    // A second window of an app that never was snapped does not unfloat into
    // the zone the first one floated from (F306).
    void unfloatNeverTakesASiblingsZone()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        const QString k1 = f.live(QStringLiteral("k-1"));
        const QString k2 = f.live(QStringLiteral("k-2"));
        f.snapThenFloat(k1, kLeft);
        QCOMPARE(f.wta->service()->preFloatZones(k1), (QStringList{f.zone(0)}));
        f.snap->setWindowFloat(k2, true, kLeft);
        QVERIFY(f.snap->isFloating(k2));
        QVERIFY(f.wta->service()->preFloatZones(k2).isEmpty());
        QSignalSpy feedback(f.snap.get(), &PhosphorEngine::PlacementEngineBase::navigationFeedback);
        f.snap->toggleWindowFloat(k2, kLeft);
        QVERIFY(f.snap->isFloating(k2));
        QVERIFY(!feedback.isEmpty());
        QCOMPARE(feedback.last().at(2).toString(), QStringLiteral("no_pre_float_zone"));
        QVERIFY(f.wta->service()->windowsInZone(f.zone(0)).isEmpty());
    }

    // A window on two desktops unfloats into the zone it floated from on the
    // desktop in view: a snap on one does not wipe the other's (F312).
    void eachDesktopKeepsItsOwnPreFloatZone()
    {
        SnapNavFixture f;
        QVERIFY(f.ready());
        twoDesktops(f);
        const QString w = onBothDesktops(f, QStringLiteral("pf-1"));
        f.snapOn(w, {f.zone(0)}, kLeft, 1);
        f.snap->stateForWindowOnScreen(w, kLeft, 2)->assignWindowToZone(w, f.zone(1), kLeft, 2);
        f.showDesktop(kLeft, 1);
        f.snap->toggleWindowFloat(w, kLeft);
        QVERIFY(f.snap->isFloating(w));
        f.showDesktop(kLeft, 2);
        f.snap->toggleWindowFloat(w, kLeft);
        QVERIFY(f.snap->isFloating(w));
        f.snap->toggleWindowFloat(w, kLeft);
        QCOMPARE(f.snap->stateForWindowOnScreen(w, kLeft, 2)->zonesForWindow(w), (QStringList{f.zone(1)}));
        f.showDesktop(kLeft, 1);
        f.snap->toggleWindowFloat(w, kLeft);
        QCOMPARE(f.snap->stateForWindowOnScreen(w, kLeft, 1)->zonesForWindow(w), (QStringList{f.zone(0)}));
    }

private:
    /// A second three-zone layout, run by DP-1 on desktop 2.
    static PhosphorZones::Layout* secondLayout(SnapNavFixture& f)
    {
        auto* other = PlasmaZones::createTestLayout(3, f.layouts);
        f.layouts->addLayout(other);
        f.layouts->assignLayout(kLeft, 2, QString(), other);
        return other;
    }

    /// A live window (app "app") on desktops 1 and 2, its frame in zone 3.
    static QString onBothDesktops(SnapNavFixture& f, const QString& instance)
    {
        PhosphorEngine::WindowMetadata meta;
        meta.appId = QStringLiteral("app");
        meta.isMinimized = false;
        meta.virtualDesktop = 1;
        meta.virtualDesktops = {1, 2};
        f.registry.upsert(instance, meta);
        const QString windowId = f.registry.canonicalizeWindowId(QStringLiteral("app|") + instance);
        f.setFrame(windowId, QRect(1300, 100, 400, 300));
        return windowId;
    }

    /// DP-1 runs the fixture's layout on desktops 1 and 2, desktop 1 shown,
    /// with no neighbour output so focus at its edge steps desktops.
    static void twoDesktops(SnapNavFixture& f)
    {
        f.layouts->assignLayout(kLeft, 2, QString(), f.layout);
        f.cross.desktopCount = 2;
        f.showDesktop(kLeft, 1);
    }
};

QTEST_MAIN(TestWtaSnapDesktopNav)
#include "test_wta_snap_desktop_nav.moc"
