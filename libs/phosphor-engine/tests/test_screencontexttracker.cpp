// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorEngine/ScreenContextTracker.h>

#include <QTest>

using PhosphorEngine::ContextChange;
using PhosphorEngine::PlacementStateKey;
using PhosphorEngine::ScreenContextTracker;

class TestScreenContextTracker : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void currentKeyForScreen_precedence();
    void setCurrentDesktop_arming();
    void setCurrentDesktopForScreen_perOutput();
    void setCurrentActivity_arming();
    void stickyPin_takeAndHas();
    void removeScreen_dropsBothMaps();
    void releaseScreenOwnership_keepsPerOutputDesktop();
    void removeScreensIf_byPredicate();
    void pruneDesktop_byValue();
    void renumberDesktops_identityShift();
    void guards_rejectDesktopsBelowOne();
};

void TestScreenContextTracker::currentKeyForScreen_precedence()
{
    ScreenContextTracker t;
    // Default: global desktop 1, empty activity.
    QCOMPARE(t.currentKeyForScreen(QStringLiteral("S1")), (PlacementStateKey{QStringLiteral("S1"), 1, QString()}));

    t.setCurrentDesktop(3);
    t.setCurrentActivity(QStringLiteral("work"));
    QCOMPARE(t.currentKeyForScreen(QStringLiteral("S1")),
             (PlacementStateKey{QStringLiteral("S1"), 3, QStringLiteral("work")}));

    // Per-output desktop overrides the global desktop for that screen only.
    t.setCurrentDesktopForScreen(QStringLiteral("S1"), 5);
    QCOMPARE(t.currentKeyForScreen(QStringLiteral("S1")).desktop, 5);
    QCOMPARE(t.currentKeyForScreen(QStringLiteral("S2")).desktop, 3);

    // Sticky pin wins over per-output.
    t.setStickyPin(QStringLiteral("S1"), 9);
    QCOMPARE(t.currentKeyForScreen(QStringLiteral("S1")).desktop, 9);
    QVERIFY(t.hasStickyPin(QStringLiteral("S1")));
}

void TestScreenContextTracker::setCurrentDesktop_arming()
{
    ScreenContextTracker t;
    // First push is a same-value (1 == 1) establish: no change, no arm.
    ContextChange r = t.setCurrentDesktop(1);
    QVERIFY(!r.changed);
    QVERIFY(!r.armSwitch);
    QVERIFY(t.desktopContextEverSet());

    // Genuine switch AFTER establish: changed and armed.
    r = t.setCurrentDesktop(2);
    QVERIFY(r.changed);
    QVERIFY(r.armSwitch);
    QCOMPARE(t.currentDesktop(), 2);

    // A fresh tracker whose first call is already a change must NOT arm (startup
    // push on a non-default desktop): changed but not armed.
    ScreenContextTracker t2;
    r = t2.setCurrentDesktop(4);
    QVERIFY(r.changed);
    QVERIFY(!r.armSwitch);
    QVERIFY(t2.desktopContextEverSet());
}

void TestScreenContextTracker::setCurrentDesktopForScreen_perOutput()
{
    ScreenContextTracker t;
    QCOMPARE(t.screenDesktop(QStringLiteral("S1")), 1); // falls back to global

    ContextChange r = t.setCurrentDesktopForScreen(QStringLiteral("S1"), 7);
    QVERIFY(r.changed);
    QVERIFY(!r.armSwitch); // no prior establish
    QCOMPARE(t.screenDesktop(QStringLiteral("S1")), 7);

    // Same per-screen desktop re-push: establish only, no change.
    r = t.setCurrentDesktopForScreen(QStringLiteral("S1"), 7);
    QVERIFY(!r.changed);

    // Guards: empty screen / desktop < 1 are ignored.
    r = t.setCurrentDesktopForScreen(QString(), 3);
    QVERIFY(!r.changed);
    r = t.setCurrentDesktopForScreen(QStringLiteral("S1"), 0);
    QVERIFY(!r.changed);

    t.clearCurrentDesktopForScreen(QStringLiteral("S1"));
    QCOMPARE(t.screenDesktop(QStringLiteral("S1")), 1);

    // A FIRST per-output push that EQUALS the global desktop must still pin
    // the screen (presence, not value): without the pin, a later global
    // switch dragged this screen's key onto a desktop it was not showing.
    ScreenContextTracker t2;
    t2.setCurrentDesktop(2);
    ContextChange first = t2.setCurrentDesktopForScreen(QStringLiteral("S1"), 2);
    QVERIFY(!first.changed); // effective desktop did not move
    QVERIFY(!first.armSwitch);
    t2.setCurrentDesktop(5);
    QCOMPARE(t2.screenDesktop(QStringLiteral("S1")), 2); // pinned, not dragged
    QCOMPARE(t2.currentKeyForScreen(QStringLiteral("S2")).desktop, 5); // unpinned screens follow
}

void TestScreenContextTracker::setCurrentActivity_arming()
{
    ScreenContextTracker t;
    // Empty same-value push does NOT establish (activities unavailable).
    ContextChange r = t.setCurrentActivity(QString());
    QVERIFY(!r.changed);
    QVERIFY(!t.activityContextEverSet());

    // First non-empty is a change but not armed.
    r = t.setCurrentActivity(QStringLiteral("a"));
    QVERIFY(r.changed);
    QVERIFY(!r.armSwitch);
    QVERIFY(t.activityContextEverSet());

    // "a" -> "" -> "b": the "" leg is a change (armed, context established),
    // and the "" -> "b" leg stays armed.
    r = t.setCurrentActivity(QString());
    QVERIFY(r.changed);
    QVERIFY(r.armSwitch);
    r = t.setCurrentActivity(QStringLiteral("b"));
    QVERIFY(r.changed);
    QVERIFY(r.armSwitch);
    QCOMPARE(t.currentActivity(), QStringLiteral("b"));
}

void TestScreenContextTracker::stickyPin_takeAndHas()
{
    ScreenContextTracker t;
    QVERIFY(!t.hasStickyPin(QStringLiteral("S1")));
    t.setStickyPin(QStringLiteral("S1"), 4);
    QVERIFY(t.hasStickyPin(QStringLiteral("S1")));
    QCOMPARE(t.takeStickyPin(QStringLiteral("S1")), 4);
    QVERIFY(!t.hasStickyPin(QStringLiteral("S1")));
}

void TestScreenContextTracker::removeScreen_dropsBothMaps()
{
    ScreenContextTracker t;
    t.setStickyPin(QStringLiteral("S1"), 2);
    t.setCurrentDesktopForScreen(QStringLiteral("S1"), 6);
    t.removeScreen(QStringLiteral("S1"));
    QVERIFY(!t.hasStickyPin(QStringLiteral("S1")));
    QCOMPARE(t.screenDesktop(QStringLiteral("S1")), 1); // per-output entry gone
}

void TestScreenContextTracker::removeScreensIf_byPredicate()
{
    ScreenContextTracker t;
    t.setStickyPin(QStringLiteral("keep"), 2);
    t.setStickyPin(QStringLiteral("drop"), 3);
    t.setCurrentDesktopForScreen(QStringLiteral("drop"), 8);
    t.removeScreensIf([](const QString& id) {
        return id == QStringLiteral("drop");
    });
    QVERIFY(t.hasStickyPin(QStringLiteral("keep")));
    QVERIFY(!t.hasStickyPin(QStringLiteral("drop")));
    QCOMPARE(t.screenDesktop(QStringLiteral("drop")), 1);
}

void TestScreenContextTracker::releaseScreenOwnership_keepsPerOutputDesktop()
{
    // A screen LEAVING an engine's mode set keeps its per-output desktop: that
    // is compositor truth about the screen, not engine bookkeeping, and the
    // engine cannot re-derive it. Only the sticky pin — which is the engine's
    // own — is dropped.
    //
    // The regression this pins: the leave path used removeScreen, so
    // screenDesktop() fell back to the GLOBAL desktop. The daemon sets that
    // exactly once at startup (every later change arrives per-output), so a
    // screen that left an engine and came back keyed every context to the
    // startup desktop — merging all virtual desktops' state into one.
    ScreenContextTracker t;
    t.setCurrentDesktopForScreen(QStringLiteral("S1"), 4);
    t.setStickyPin(QStringLiteral("S1"), 9);
    QCOMPARE(t.currentKeyForScreen(QStringLiteral("S1")).desktop, 9); // pin wins

    t.releaseScreenOwnership(QStringLiteral("S1"));
    QVERIFY(!t.hasStickyPin(QStringLiteral("S1")));
    // The per-output desktop survives, so the key does NOT collapse onto the
    // global desktop (1).
    QCOMPARE(t.screenDesktop(QStringLiteral("S1")), 4);
    QCOMPARE(t.currentKeyForScreen(QStringLiteral("S1")).desktop, 4);

    // removeScreen stays the OUTPUT-removal verb and still drops both.
    t.removeScreen(QStringLiteral("S1"));
    QCOMPARE(t.screenDesktop(QStringLiteral("S1")), 1);
}

void TestScreenContextTracker::pruneDesktop_byValue()
{
    ScreenContextTracker t;
    t.setStickyPin(QStringLiteral("S1"), 5);
    t.setStickyPin(QStringLiteral("S2"), 6);
    t.setCurrentDesktopForScreen(QStringLiteral("S3"), 5);
    t.pruneDesktop(5);
    QVERIFY(!t.hasStickyPin(QStringLiteral("S1"))); // pinned to removed desktop 5
    QVERIFY(t.hasStickyPin(QStringLiteral("S2"))); // desktop 6 survives
    QCOMPARE(t.screenDesktop(QStringLiteral("S3")), 1); // per-output 5 gone
}

void TestScreenContextTracker::renumberDesktops_identityShift()
{
    ScreenContextTracker t;
    t.setCurrentDesktop(4);
    t.setCurrentDesktopForScreen(QStringLiteral("S1"), 3);
    t.setCurrentDesktopForScreen(QStringLiteral("S2"), 1);
    t.setStickyPin(QStringLiteral("S1"), 4);

    // Desktop 2 was destroyed: 3→2, 4→3; 1 untouched (absent = unchanged).
    QHash<int, int> mapping;
    mapping.insert(3, 2);
    mapping.insert(4, 3);
    t.renumberDesktops(mapping);

    QCOMPARE(t.currentDesktop(), 3);
    QCOMPARE(t.screenDesktop(QStringLiteral("S1")), 2);
    QCOMPARE(t.screenDesktop(QStringLiteral("S2")), 1);
    QCOMPARE(t.stickyPinnedDesktop(QStringLiteral("S1")), 3);

    // Empty mapping is a no-op.
    t.renumberDesktops({});
    QCOMPARE(t.currentDesktop(), 3);
}

void TestScreenContextTracker::guards_rejectDesktopsBelowOne()
{
    // KWin desktops are 1-based with no reserved "unset" value, so a 0 or
    // negative push must leave every tracked value exactly as it was rather
    // than poisoning the keys derived from it.
    ScreenContextTracker t;
    t.setCurrentDesktop(3);
    t.setCurrentDesktopForScreen(QStringLiteral("S1"), 4);
    t.setStickyPin(QStringLiteral("S2"), 5);

    // Global setter: value unchanged, no change reported.
    ContextChange r = t.setCurrentDesktop(0);
    QVERIFY(!r.changed);
    QVERIFY(!r.armSwitch);
    QCOMPARE(t.currentDesktop(), 3);
    r = t.setCurrentDesktop(-1);
    QVERIFY(!r.changed);
    QCOMPARE(t.currentDesktop(), 3);

    // Per-output setter on an ALREADY ESTABLISHED screen: the existing entry
    // survives (the empty-map arm is covered in setCurrentDesktopForScreen_perOutput).
    r = t.setCurrentDesktopForScreen(QStringLiteral("S1"), 0);
    QVERIFY(!r.changed);
    QCOMPARE(t.screenDesktop(QStringLiteral("S1")), 4);

    // Sticky pin: rejected outright, so 0 stays unambiguous as "not pinned".
    t.setStickyPin(QStringLiteral("S3"), 0);
    QVERIFY(!t.hasStickyPin(QStringLiteral("S3")));
    t.setStickyPin(QStringLiteral("S2"), -2);
    QCOMPARE(t.stickyPinnedDesktop(QStringLiteral("S2")), 5); // existing pin intact

    // A renumber must not write what those setters refuse: a poisoned target
    // leaves the value on its current desktop.
    QHash<int, int> poisoned;
    poisoned.insert(3, 0);
    poisoned.insert(4, -1);
    poisoned.insert(5, 2);
    t.renumberDesktops(poisoned);
    QCOMPARE(t.currentDesktop(), 3);
    QCOMPARE(t.screenDesktop(QStringLiteral("S1")), 4);
    QCOMPARE(t.stickyPinnedDesktop(QStringLiteral("S2")), 2); // valid target still applies
}

QTEST_MAIN(TestScreenContextTracker)
#include "test_screencontexttracker.moc"
