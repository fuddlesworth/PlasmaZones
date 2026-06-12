// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Unit test for the reference-counted idle-inhibition aggregator. Pure logic, no
// compositor: it pins the cookie semantics (edge-only inhibitedChanged, nested
// inhibit/release, unique non-reused cookies, release-unknown no-op).

#include "idleinhibitionmanager.h"

#include <QSignalSpy>
#include <QTest>

using namespace PhosphorServiceIdle;

class IdleInhibitionManagerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void startsUninhibited();
    void singleInhibitToggles();
    void nestedInhibitChangesOnlyOnEdges();
    void releaseUnknownCookieIsNoop();
    void cookiesAreUniqueAndNotReused();
};

void IdleInhibitionManagerTest::startsUninhibited()
{
    IdleInhibitionManager mgr;
    QVERIFY(!mgr.isInhibited());
    QCOMPARE(mgr.activeCount(), 0);
}

void IdleInhibitionManagerTest::singleInhibitToggles()
{
    IdleInhibitionManager mgr;
    QSignalSpy spy(&mgr, &IdleInhibitionManager::inhibitedChanged);

    const int cookie = mgr.inhibit();
    QVERIFY(mgr.isInhibited());
    QCOMPARE(mgr.activeCount(), 1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toBool(), true);

    QVERIFY(mgr.release(cookie));
    QVERIFY(!mgr.isInhibited());
    QCOMPARE(mgr.activeCount(), 0);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toBool(), false);
}

void IdleInhibitionManagerTest::nestedInhibitChangesOnlyOnEdges()
{
    IdleInhibitionManager mgr;
    QSignalSpy spy(&mgr, &IdleInhibitionManager::inhibitedChanged);

    const int a = mgr.inhibit(); // false -> true (edge)
    const int b = mgr.inhibit(); // already inhibited, no edge
    const int c = mgr.inhibit(); // already inhibited, no edge
    QCOMPARE(mgr.activeCount(), 3);
    QCOMPARE(spy.count(), 1); // only the rising edge

    QVERIFY(mgr.release(b)); // still inhibited (a, c held)
    QVERIFY(mgr.release(a)); // still inhibited (c held)
    QCOMPARE(spy.count(), 1); // no edge yet
    QVERIFY(mgr.isInhibited());

    QVERIFY(mgr.release(c)); // last one -> falling edge
    QVERIFY(!mgr.isInhibited());
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.takeLast().at(0).toBool(), false);
}

void IdleInhibitionManagerTest::releaseUnknownCookieIsNoop()
{
    IdleInhibitionManager mgr;
    QSignalSpy spy(&mgr, &IdleInhibitionManager::inhibitedChanged);

    QVERIFY(!mgr.release(999)); // never issued
    QCOMPARE(spy.count(), 0);

    const int cookie = mgr.inhibit();
    QVERIFY(mgr.release(cookie));
    QVERIFY(!mgr.release(cookie)); // double release is a no-op
    QCOMPARE(mgr.activeCount(), 0);
    QCOMPARE(spy.count(), 2); // one rising, one falling; the double release adds none
}

void IdleInhibitionManagerTest::cookiesAreUniqueAndNotReused()
{
    IdleInhibitionManager mgr;
    const int a = mgr.inhibit();
    const int b = mgr.inhibit();
    QVERIFY(a != b);

    mgr.release(a);
    const int c = mgr.inhibit();
    // The released cookie must not be handed out again, so releasing a stale
    // copy of `a` can never disturb a live inhibition.
    QVERIFY(c != a);
    QVERIFY(c != b);
}

QTEST_GUILESS_MAIN(IdleInhibitionManagerTest)
#include "test_inhibition.moc"
