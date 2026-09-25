// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Arbitration tests for PopoutController. Cooperative-per-scope,
// modal suppression, detached independence, same-id collisions,
// toggle, close, closeAll. No dismissed-callback or signal-ordering
// coverage; those live in test_popoutcontroller_dismiss.cpp and
// test_popoutcontroller_signals.cpp respectively.

#include "test_popoutcontroller_helpers.h"

#include <PhosphorPopout/PopoutController.h>

#include <QSignalSpy>
#include <QTest>

using namespace PhosphorPopout;
using PhosphorPopoutTest::makeRequest;

class TestPopoutControllerArbitration : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void open_cooperativeReturnsNonEmptyHandle();
    void open_secondCooperativeSameScopeReplacesFirst();
    void open_cooperativeDifferentScopesCoexist();
    void open_modalClosesAllCooperatives();
    void open_cooperativeRejectedWhileModalActive();
    void open_modalStacksOnModal();
    void open_modalSameIdRejected();
    void open_detachedSurvivesArbitration();
    void open_detachedAcceptedWhileModalActive();
    void open_emptyIdAllowsMultiple();
    void open_emptyIdCloseClearsEntry();
    void open_sameIdReturnsEmpty();
    void open_transportRefusalReturnsEmpty();
    void close_unknownHandleNoOp();
    void close_emptyHandleNoOp();
    void close_modalClearsModalActiveAfterStackDrain();
    void close_detachedFiresClosedAndUpdatesIsOpen();
    void toggle_openWhenClosed();
    void toggle_closeWhenOpen();
    void toggle_emptyIdRoutesToOpen();
    void toggle_emptyIdAlwaysOpens();
    void toggle_closesDetachedEvenWhileModalActive();
    void isOpen_tracksByPopoutId();
    void handleFor_unknownIdReturnsEmpty();
    void closeAll_drainsEntriesAndClearsModalCount();
    void closeAll_onEmptyControllerIsNoOp();
    void open_modalTransportRefusalLeavesCooperativesDrainedAndModalInactive();
    void open_modalSameIdRefusalWhileModalUpDoesNotTouchCount();
};

void TestPopoutControllerArbitration::open_cooperativeReturnsNonEmptyHandle()
{
    FakeTransport t;
    PopoutController c(&t);
    QSignalSpy openedSpy(&c, &PopoutController::popoutOpened);

    const QString h = c.open(makeRequest(QStringLiteral("a")));
    QVERIFY(!h.isEmpty());
    QCOMPARE(openedSpy.count(), 1);
    QCOMPARE(openedSpy.first().at(0).toString(), QStringLiteral("a"));
    QCOMPARE(openedSpy.first().at(1).toString(), h);
    QVERIFY(c.isOpen(QStringLiteral("a")));
    QCOMPARE(t.alive.size(), 1);
}

void TestPopoutControllerArbitration::open_secondCooperativeSameScopeReplacesFirst()
{
    FakeTransport t;
    PopoutController c(&t);
    QSignalSpy openedSpy(&c, &PopoutController::popoutOpened);
    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);

    const QString h1 = c.open(makeRequest(QStringLiteral("a")));
    const QString h2 = c.open(makeRequest(QStringLiteral("b")));

    QVERIFY(!h1.isEmpty());
    QVERIFY(!h2.isEmpty());
    QVERIFY(h1 != h2);
    QCOMPARE(openedSpy.count(), 2);
    QCOMPARE(closedSpy.count(), 1);
    QCOMPARE(closedSpy.first().at(0).toString(), QStringLiteral("a"));
    QCOMPARE(closedSpy.first().at(1).toString(), h1);
    QVERIFY(!c.isOpen(QStringLiteral("a")));
    QVERIFY(c.isOpen(QStringLiteral("b")));
    QCOMPARE(t.alive.size(), 1);
}

void TestPopoutControllerArbitration::open_cooperativeDifferentScopesCoexist()
{
    FakeTransport t;
    PopoutController c(&t);

    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    const QString h1 = c.open(makeRequest(QStringLiteral("a"), ExclusiveMode::Cooperative, QStringLiteral("scope-1")));
    const QString h2 = c.open(makeRequest(QStringLiteral("b"), ExclusiveMode::Cooperative, QStringLiteral("scope-2")));

    QVERIFY(!h1.isEmpty());
    QVERIFY(!h2.isEmpty());
    QVERIFY(c.isOpen(QStringLiteral("a")));
    QVERIFY(c.isOpen(QStringLiteral("b")));
    QCOMPARE(t.alive.size(), 2);
    // Different scopes are independent. The second open must not
    // close the first one.
    QCOMPARE(closedSpy.count(), 0);
}

void TestPopoutControllerArbitration::open_modalClosesAllCooperatives()
{
    FakeTransport t;
    PopoutController c(&t);

    QVERIFY(!c.open(makeRequest(QStringLiteral("coop-1"), ExclusiveMode::Cooperative, QStringLiteral("s1"))).isEmpty());
    QVERIFY(!c.open(makeRequest(QStringLiteral("coop-2"), ExclusiveMode::Cooperative, QStringLiteral("s2"))).isEmpty());
    QCOMPARE(t.alive.size(), 2);

    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    QSignalSpy modalSpy(&c, &PopoutController::modalActiveChanged);
    const QString modalHandle = c.open(makeRequest(QStringLiteral("alert"), ExclusiveMode::Modal));

    QVERIFY(!modalHandle.isEmpty());
    QCOMPARE(closedSpy.count(), 2);
    QVERIFY(!c.isOpen(QStringLiteral("coop-1")));
    QVERIFY(!c.isOpen(QStringLiteral("coop-2")));
    QVERIFY(c.isOpen(QStringLiteral("alert")));
    QVERIFY(c.isModalActive());
    QCOMPARE(modalSpy.count(), 1);
    QCOMPARE(t.alive.size(), 1);
}

void TestPopoutControllerArbitration::open_cooperativeRejectedWhileModalActive()
{
    FakeTransport t;
    PopoutController c(&t);
    QVERIFY(!c.open(makeRequest(QStringLiteral("alert"), ExclusiveMode::Modal)).isEmpty());
    QVERIFY(c.isModalActive());

    QSignalSpy openedSpy(&c, &PopoutController::popoutOpened);
    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    const int closeCallsBefore = t.closeSurfaceCalls;
    const QString h = c.open(makeRequest(QStringLiteral("calendar")));

    QVERIFY(h.isEmpty());
    QCOMPARE(openedSpy.count(), 0);
    QCOMPARE(closedSpy.count(), 0);
    QVERIFY(!c.isOpen(QStringLiteral("calendar")));
    // Transport must NOT have been invoked for the rejected request.
    // The only entry in openLog is the alert that opened up first.
    QCOMPARE(t.openLog.size(), 1);
    // Pin the full no-side-effects contract for the reject path.
    QCOMPARE(t.closeLog.size(), 0);
    QCOMPARE(t.closeSurfaceCalls, closeCallsBefore);
}

void TestPopoutControllerArbitration::open_modalStacksOnModal()
{
    FakeTransport t;
    PopoutController c(&t);

    const QString m1 = c.open(makeRequest(QStringLiteral("alert-1"), ExclusiveMode::Modal));
    // The spy is installed after the first modal so it observes only
    // the no-op second-modal case. A second modal must not fire
    // modalActiveChanged (state was already "active") and must not
    // close the first modal.
    QSignalSpy modalSpy(&c, &PopoutController::modalActiveChanged);
    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    const QString m2 = c.open(makeRequest(QStringLiteral("alert-2"), ExclusiveMode::Modal));

    QVERIFY(!m1.isEmpty());
    QVERIFY(!m2.isEmpty());
    QVERIFY(c.isOpen(QStringLiteral("alert-1")));
    QVERIFY(c.isOpen(QStringLiteral("alert-2")));
    QVERIFY(c.isModalActive());
    QCOMPARE(modalSpy.count(), 0);
    QCOMPARE(closedSpy.count(), 0);
}

void TestPopoutControllerArbitration::open_detachedSurvivesArbitration()
{
    FakeTransport t;
    PopoutController c(&t);

    QVERIFY(!c.open(makeRequest(QStringLiteral("note"), ExclusiveMode::Detached)).isEmpty());
    QVERIFY(!c.open(makeRequest(QStringLiteral("coop"))).isEmpty());
    QVERIFY(c.isOpen(QStringLiteral("note")));
    QVERIFY(c.isOpen(QStringLiteral("coop")));

    // Modal should NOT close the detached popout.
    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    QVERIFY(!c.open(makeRequest(QStringLiteral("alert"), ExclusiveMode::Modal)).isEmpty());

    QVERIFY(c.isOpen(QStringLiteral("note")));
    QVERIFY(!c.isOpen(QStringLiteral("coop"))); // cooperative did close
    QCOMPARE(closedSpy.count(), 1);
    QCOMPARE(closedSpy.first().at(0).toString(), QStringLiteral("coop"));
}

void TestPopoutControllerArbitration::open_sameIdReturnsEmpty()
{
    FakeTransport t;
    PopoutController c(&t);

    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    const QString h1 = c.open(makeRequest(QStringLiteral("a")));
    const int closeCallsBefore = t.closeSurfaceCalls;
    const QString h2 = c.open(makeRequest(QStringLiteral("a")));

    QVERIFY(!h1.isEmpty());
    // Second open with the same popoutId is a no-op rather than a
    // silent duplicate. Callers must close first to reopen.
    QVERIFY(h2.isEmpty());
    QCOMPARE(t.alive.size(), 1);
    // openLog assertion catches a regression where the transport sees
    // the rejected request even though the controller dropped its
    // table entry.
    QCOMPARE(t.openLog.size(), 1);
    QCOMPARE(c.handleFor(QStringLiteral("a")), h1);
    // The rejected path must NOT fire popoutClosed for h1; a regression
    // that routes same-id rejection through closeCooperativeInScope
    // would emit here.
    QCOMPARE(closedSpy.count(), 0);
    QCOMPARE(t.closeSurfaceCalls, closeCallsBefore);
}

void TestPopoutControllerArbitration::open_transportRefusalReturnsEmpty()
{
    FakeTransport t;
    PopoutController c(&t);
    t.refuseNextOpen = true;

    QSignalSpy openedSpy(&c, &PopoutController::popoutOpened);
    const QString h = c.open(makeRequest(QStringLiteral("a")));

    QVERIFY(h.isEmpty());
    QCOMPARE(openedSpy.count(), 0);
    QVERIFY(!c.isOpen(QStringLiteral("a")));
}

void TestPopoutControllerArbitration::close_unknownHandleNoOp()
{
    FakeTransport t;
    PopoutController c(&t);
    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    c.close(QStringLiteral("never-existed"));
    QCOMPARE(closedSpy.count(), 0);
}

void TestPopoutControllerArbitration::close_modalClearsModalActiveAfterStackDrain()
{
    FakeTransport t;
    PopoutController c(&t);

    const QString m1 = c.open(makeRequest(QStringLiteral("alert-1"), ExclusiveMode::Modal));
    const QString m2 = c.open(makeRequest(QStringLiteral("alert-2"), ExclusiveMode::Modal));

    QSignalSpy modalSpy(&c, &PopoutController::modalActiveChanged);
    c.close(m1);
    QVERIFY(c.isModalActive()); // still one modal up
    QCOMPARE(modalSpy.count(), 0);

    c.close(m2);
    QVERIFY(!c.isModalActive());
    QCOMPARE(modalSpy.count(), 1);

    // After modal clears, cooperatives are no longer rejected.
    const QString coop = c.open(makeRequest(QStringLiteral("coop")));
    QVERIFY(!coop.isEmpty());
}

void TestPopoutControllerArbitration::toggle_openWhenClosed()
{
    FakeTransport t;
    PopoutController c(&t);
    const QString h = c.toggle(makeRequest(QStringLiteral("a")));
    QVERIFY(!h.isEmpty());
    QVERIFY(c.isOpen(QStringLiteral("a")));
}

void TestPopoutControllerArbitration::toggle_closeWhenOpen()
{
    FakeTransport t;
    PopoutController c(&t);
    QVERIFY(!c.open(makeRequest(QStringLiteral("a"))).isEmpty());
    QVERIFY(c.isOpen(QStringLiteral("a")));

    const QString h = c.toggle(makeRequest(QStringLiteral("a")));
    // toggle closing returns empty per the documented contract.
    QVERIFY(h.isEmpty());
    QVERIFY(!c.isOpen(QStringLiteral("a")));
}

void TestPopoutControllerArbitration::toggle_emptyIdRoutesToOpen()
{
    FakeTransport t;
    PopoutController c(&t);
    PopoutRequest req; // empty popoutId
    const QString h = c.toggle(req);
    QVERIFY(!h.isEmpty());
}

void TestPopoutControllerArbitration::isOpen_tracksByPopoutId()
{
    FakeTransport t;
    PopoutController c(&t);
    QVERIFY(!c.isOpen(QStringLiteral("a")));
    QVERIFY(!c.open(makeRequest(QStringLiteral("a"))).isEmpty());
    QVERIFY(c.isOpen(QStringLiteral("a")));
    QVERIFY(!c.isOpen(QStringLiteral("b")));
}

void TestPopoutControllerArbitration::closeAll_drainsEntriesAndClearsModalCount()
{
    FakeTransport t;
    PopoutController c(&t);
    QVERIFY(!c.open(makeRequest(QStringLiteral("note"), ExclusiveMode::Detached)).isEmpty());
    QVERIFY(!c.open(makeRequest(QStringLiteral("alert-1"), ExclusiveMode::Modal)).isEmpty());
    QVERIFY(!c.open(makeRequest(QStringLiteral("alert-2"), ExclusiveMode::Modal)).isEmpty());
    QVERIFY(c.isModalActive());
    QCOMPARE(t.alive.size(), 3);

    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    QSignalSpy modalSpy(&c, &PopoutController::modalActiveChanged);
    c.closeAll();

    QCOMPARE(closedSpy.count(), 3);
    QVERIFY(!c.isModalActive());
    QCOMPARE(modalSpy.count(), 1);
    QCOMPARE(t.alive.size(), 0);
    QVERIFY(!c.isOpen(QStringLiteral("note")));
}

void TestPopoutControllerArbitration::open_modalSameIdRejected()
{
    FakeTransport t;
    PopoutController c(&t);

    const QString h1 = c.open(makeRequest(QStringLiteral("alert"), ExclusiveMode::Modal));
    QVERIFY(!h1.isEmpty());

    // Second open with the same popoutId is rejected regardless of
    // ExclusiveMode. Modal-on-Modal stacks only across distinct ids.
    const QString h2 = c.open(makeRequest(QStringLiteral("alert"), ExclusiveMode::Modal));
    QVERIFY(h2.isEmpty());
    QCOMPARE(t.openLog.size(), 1);

    // Rejected open must NOT bump modalCount. Closing h1 should
    // drain the active modal state. A regression that incremented
    // the count on the rejected path would leave isModalActive()
    // stuck at true after this close.
    c.close(h1);
    QVERIFY(!c.isModalActive());
}

void TestPopoutControllerArbitration::open_detachedAcceptedWhileModalActive()
{
    FakeTransport t;
    PopoutController c(&t);
    QVERIFY(!c.open(makeRequest(QStringLiteral("alert"), ExclusiveMode::Modal)).isEmpty());
    QVERIFY(c.isModalActive());

    const int closeCallsBefore = t.closeSurfaceCalls;
    QSignalSpy modalSpy(&c, &PopoutController::modalActiveChanged);
    // Detached bypasses the modal-suppression check entirely.
    const QString h = c.open(makeRequest(QStringLiteral("note"), ExclusiveMode::Detached));
    QVERIFY(!h.isEmpty());
    QVERIFY(c.isOpen(QStringLiteral("note")));
    // Detached must NOT touch modal bookkeeping or trigger extra
    // closeSurface calls.
    QCOMPARE(modalSpy.count(), 0);
    QCOMPARE(t.closeSurfaceCalls, closeCallsBefore);
    QCOMPARE(t.closeLog.size(), 0);
}

void TestPopoutControllerArbitration::open_emptyIdAllowsMultiple()
{
    FakeTransport t;
    PopoutController c(&t);

    // The same-id collision check skips empty popoutIds, so two
    // anonymous opens both succeed.
    PopoutRequest req;
    req.exclusive = ExclusiveMode::Cooperative;
    req.scope = QStringLiteral("scope-a");
    const QString h1 = c.open(req);
    req.scope = QStringLiteral("scope-b");
    const QString h2 = c.open(req);
    QVERIFY(!h1.isEmpty());
    QVERIFY(!h2.isEmpty());
    QVERIFY(h1 != h2);
    QCOMPARE(t.alive.size(), 2);
}

void TestPopoutControllerArbitration::open_emptyIdCloseClearsEntry()
{
    // Empty-id popouts must clean up after close just like named ones.
    // A regression that conflates "no popoutId" with "no entry to
    // remove" would leak anonymous entries.
    FakeTransport t;
    PopoutController c(&t);

    PopoutRequest req;
    req.scope = QStringLiteral("scope-a");
    const QString h = c.open(req);
    QVERIFY(!h.isEmpty());
    QVERIFY(c.isOpen(QString()));

    c.close(h);
    QVERIFY(!c.isOpen(QString()));
    QCOMPARE(t.alive.size(), 0);
    QCOMPARE(t.closeLog.size(), 1);

    // Re-open the same empty-id slot to confirm the close fully
    // released the table row.
    const QString h2 = c.open(req);
    QVERIFY(!h2.isEmpty());
    QVERIFY(h2 != h);
}

void TestPopoutControllerArbitration::close_emptyHandleNoOp()
{
    FakeTransport t;
    PopoutController c(&t);
    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    c.close(QString());
    QCOMPARE(closedSpy.count(), 0);
    QCOMPARE(t.closeLog.size(), 0);
}

void TestPopoutControllerArbitration::toggle_emptyIdAlwaysOpens()
{
    // Empty-id toggle has no fixed referent. Each call opens a fresh
    // popout rather than toggling an existing one. This test pins the
    // distinct-scope case where two coexist. Same-scope cooperative
    // arbitration is exercised by open_secondCooperativeSameScopeReplacesFirst.
    PopoutRequest a;
    a.scope = QStringLiteral("scope-a");
    PopoutRequest b;
    b.scope = QStringLiteral("scope-b");

    FakeTransport t;
    PopoutController c(&t);
    QVERIFY(!c.toggle(a).isEmpty());
    QVERIFY(!c.toggle(b).isEmpty());
    QCOMPARE(t.openLog.size(), 2);
    QCOMPARE(t.alive.size(), 2);
}

void TestPopoutControllerArbitration::toggle_closesDetachedEvenWhileModalActive()
{
    // toggle short-circuits to close() when the popoutId is already
    // open, BEFORE running arbitration. Detached popouts survive
    // modal-open, so a toggle of an already-open Detached while a
    // modal is up must close the Detached rather than route through
    // arbitration. Pins this non-obvious short-circuit so a refactor
    // that pushes arbitration earlier in toggle doesn't accidentally
    // suppress the close path.
    FakeTransport t;
    PopoutController c(&t);
    QVERIFY(!c.open(makeRequest(QStringLiteral("note"), ExclusiveMode::Detached)).isEmpty());
    QVERIFY(!c.open(makeRequest(QStringLiteral("alert"), ExclusiveMode::Modal)).isEmpty());
    QVERIFY(c.isOpen(QStringLiteral("note")));
    QVERIFY(c.isModalActive());

    const QString toggleResult = c.toggle(makeRequest(QStringLiteral("note"), ExclusiveMode::Detached));
    QVERIFY(toggleResult.isEmpty());
    QVERIFY(!c.isOpen(QStringLiteral("note")));
    // Modal stays up; toggling the Detached closed must not perturb
    // unrelated arbitration state.
    QVERIFY(c.isModalActive());
}

void TestPopoutControllerArbitration::handleFor_unknownIdReturnsEmpty()
{
    FakeTransport t;
    PopoutController c(&t);
    QVERIFY(c.handleFor(QStringLiteral("ghost")).isEmpty());
    QVERIFY(!c.open(makeRequest(QStringLiteral("a"))).isEmpty());
    QVERIFY(c.handleFor(QStringLiteral("ghost")).isEmpty());
}

void TestPopoutControllerArbitration::closeAll_onEmptyControllerIsNoOp()
{
    FakeTransport t;
    PopoutController c(&t);
    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    QSignalSpy modalSpy(&c, &PopoutController::modalActiveChanged);
    c.closeAll();
    QCOMPARE(closedSpy.count(), 0);
    QCOMPARE(modalSpy.count(), 0);
    QCOMPARE(t.closeSurfaceCalls, 0);
}

void TestPopoutControllerArbitration::close_detachedFiresClosedAndUpdatesIsOpen()
{
    // Detached has no special close path, but pinning the close-of-
    // Detached behaviour catches regressions that accidentally route
    // Detached through a different teardown helper.
    FakeTransport t;
    PopoutController c(&t);
    const QString h = c.open(makeRequest(QStringLiteral("pinned"), ExclusiveMode::Detached));
    QVERIFY(!h.isEmpty());
    QVERIFY(c.isOpen(QStringLiteral("pinned")));

    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    QSignalSpy modalSpy(&c, &PopoutController::modalActiveChanged);
    c.close(h);

    QCOMPARE(closedSpy.count(), 1);
    QCOMPARE(closedSpy.first().at(0).toString(), QStringLiteral("pinned"));
    QCOMPARE(closedSpy.first().at(1).toString(), h);
    QVERIFY(!c.isOpen(QStringLiteral("pinned")));
    QCOMPARE(modalSpy.count(), 0);
    QCOMPARE(t.closeLog.size(), 1);
}

void TestPopoutControllerArbitration::open_modalTransportRefusalLeavesCooperativesDrainedAndModalInactive()
{
    // The Modal branch closes cooperatives BEFORE asking the transport
    // to open the modal surface. If the transport then refuses, the
    // cooperatives stay closed (documented as intentional in
    // popoutcontroller.cpp) and modalCount stays at 0. A regression
    // that bumps modalCount on the rejected open path would leave
    // isModalActive() stuck at true.
    FakeTransport t;
    PopoutController c(&t);
    QVERIFY(!c.open(makeRequest(QStringLiteral("coop-1"), ExclusiveMode::Cooperative, QStringLiteral("s1"))).isEmpty());
    QVERIFY(!c.open(makeRequest(QStringLiteral("coop-2"), ExclusiveMode::Cooperative, QStringLiteral("s2"))).isEmpty());
    QCOMPARE(t.alive.size(), 2);

    t.refuseNextOpen = true;
    QSignalSpy modalSpy(&c, &PopoutController::modalActiveChanged);
    QSignalSpy openedSpy(&c, &PopoutController::popoutOpened);
    const QString modalHandle = c.open(makeRequest(QStringLiteral("alert"), ExclusiveMode::Modal));

    QVERIFY(modalHandle.isEmpty());
    QCOMPARE(openedSpy.count(), 0);
    QVERIFY(!c.isModalActive());
    QCOMPARE(modalSpy.count(), 0);
    // Cooperatives are gone, by design. The arbitration ran before
    // the transport refused; rolling them back would put the
    // controller in an inconsistent state.
    QVERIFY(!c.isOpen(QStringLiteral("coop-1")));
    QVERIFY(!c.isOpen(QStringLiteral("coop-2")));
}

void TestPopoutControllerArbitration::open_modalSameIdRefusalWhileModalUpDoesNotTouchCount()
{
    // Open Modal A, transport refuses Modal B with a distinct id.
    // Same-id collision is tested elsewhere; this exercises the
    // separate refusal path. modalCount must remain at 1 so closing
    // A drains modalActive cleanly.
    FakeTransport t;
    PopoutController c(&t);
    const QString a = c.open(makeRequest(QStringLiteral("alert-a"), ExclusiveMode::Modal));
    QVERIFY(!a.isEmpty());

    t.refuseNextOpen = true;
    QSignalSpy modalSpy(&c, &PopoutController::modalActiveChanged);
    const QString b = c.open(makeRequest(QStringLiteral("alert-b"), ExclusiveMode::Modal));
    QVERIFY(b.isEmpty());
    QCOMPARE(modalSpy.count(), 0);
    QVERIFY(c.isModalActive());

    c.close(a);
    QVERIFY(!c.isModalActive());
    QCOMPARE(modalSpy.count(), 1);
}

QTEST_MAIN(TestPopoutControllerArbitration)
#include "test_popoutcontroller_arbitration.moc"
