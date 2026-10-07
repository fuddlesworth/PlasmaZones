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

private:
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
