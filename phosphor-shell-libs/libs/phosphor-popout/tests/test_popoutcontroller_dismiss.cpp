// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Dismissed-callback, re-entrancy guard, and destructor tests for
// PopoutController. Arbitration coverage lives in
// test_popoutcontroller_arbitration.cpp; signal-ordering / slot-state
// coverage lives in test_popoutcontroller_signals.cpp.

#include "test_popoutcontroller_helpers.h"

#include <PhosphorPopout/PopoutController.h>

#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace PhosphorPopout;
using PhosphorPopoutTest::makeRequest;

class TestPopoutControllerDismiss : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void dismissedCallback_installedDuringConstruction();
    void dismissedCallback_removesEntryAndUpdatesModalState();
    void dismissedCallback_cooperativeDoesNotEmitModalChange();
    void dismissedCallback_unknownHandleNoOp();
    void dismissedCallback_reentrantTransportSuppressedDuringSelfTeardown();
    void dismissedCallback_reentrantTransportSuppressedDuringCloseAll();
    void close_thenLateDismissDoesNotDoubleFire();
    void destructor_detachesDismissedCallback();
    void destructor_withOpenPopoutsDoesNotDrainTransport();
};

void TestPopoutControllerDismiss::dismissedCallback_installedDuringConstruction()
{
    // The controller's ctor installs exactly one callback. Sibling
    // tests assume this. Check it explicitly so a regression that
    // removes the install is caught here rather than as a confusing
    // failure elsewhere.
    FakeTransport t;
    QVERIFY(!t.dismissedCb);
    PopoutController c(&t);
    QVERIFY(t.dismissedCb);
}

void TestPopoutControllerDismiss::dismissedCallback_removesEntryAndUpdatesModalState()
{
    FakeTransport t;
    PopoutController c(&t);
    const QString h = c.open(makeRequest(QStringLiteral("alert"), ExclusiveMode::Modal));
    QVERIFY(c.isModalActive());

    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    QSignalSpy modalSpy(&c, &PopoutController::modalActiveChanged);
    const int callsBefore = t.closeSurfaceCalls;
    t.dismiss(h);

    QCOMPARE(closedSpy.count(), 1);
    QCOMPARE(closedSpy.first().at(0).toString(), QStringLiteral("alert"));
    QCOMPARE(closedSpy.first().at(1).toString(), h);
    QVERIFY(!c.isOpen(QStringLiteral("alert")));
    QVERIFY(!c.isModalActive());
    QCOMPARE(modalSpy.count(), 1);
    // The dismissed callback must route through removeEntryQuiet,
    // NOT removeEntry. The transport already knows the surface is
    // gone; calling closeSurface again would loop. A regression that
    // swapped the helpers would bump closeSurfaceCalls here.
    QCOMPARE(t.closeSurfaceCalls, callsBefore);
}

void TestPopoutControllerDismiss::dismissedCallback_cooperativeDoesNotEmitModalChange()
{
    // Cooperative dismissal must not touch modal bookkeeping. A
    // regression that always emits modalActiveChanged from the
    // callback would fail this.
    FakeTransport t;
    PopoutController c(&t);
    const QString h = c.open(makeRequest(QStringLiteral("calendar")));
    QVERIFY(!h.isEmpty());

    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    QSignalSpy modalSpy(&c, &PopoutController::modalActiveChanged);
    const int callsBefore = t.closeSurfaceCalls;
    t.dismiss(h);

    QCOMPARE(closedSpy.count(), 1);
    QCOMPARE(modalSpy.count(), 0);
    QVERIFY(!c.isOpen(QStringLiteral("calendar")));
    // Same removeEntryQuiet contract as the modal case. The
    // transport already knows the surface is gone, so the callback
    // path must not call closeSurface again.
    QCOMPARE(t.closeSurfaceCalls, callsBefore);
}

void TestPopoutControllerDismiss::dismissedCallback_unknownHandleNoOp()
{
    FakeTransport t;
    PopoutController c(&t);
    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    // QVERIFY rather than `if`. A guard here would mask a regression
    // where the controller never installs its dismissed callback.
    QVERIFY(t.dismissedCb);
    const int callsBefore = t.closeSurfaceCalls;
    t.dismissedCb(QStringLiteral("never-existed"));
    QCOMPARE(closedSpy.count(), 0);
    // Pin the removeEntryQuiet contract on the unknown-handle path
    // too. A regression that swapped the quiet helper for removeEntry
    // would call closeSurface for the unknown handle.
    QCOMPARE(t.closeSurfaceCalls, callsBefore);
}

void TestPopoutControllerDismiss::dismissedCallback_reentrantTransportSuppressedDuringSelfTeardown()
{
    // Mirrors the closeAll test, but exercises the ScopedTrue guard
    // on open's Modal-suppression path (popoutcontroller.cpp's
    // ExclusiveMode::Modal branch wraps closeAllCooperatives in a
    // guard). Same-handle echo cannot prove the guard because
    // removeEntry's erase-before-closeSurface ordering makes the
    // re-entrant callback's lookup find nothing. Cross-handle echo
    // closes a sibling cooperative out from under closeAllCooperatives'
    // iteration if the guard is absent.
    //
    // With the guard, opening the Modal drains [h1, h2]:
    //   removeEntry(h1) -> erase h1 -> closeSurface(h1)
    //     -> transport fires dismissed(h2) cross-handle
    //     -> callback short-circuits because inSelfTeardown is true
    //   removeEntry(h2) -> erase h2 -> closeSurface(h2)
    //   Total closeSurfaceCalls: 2
    //
    // Without the guard, dismissed(h2) would removeEntryQuiet h2 mid-
    // iteration, removeEntry(h2) would find nothing, and only one
    // closeSurface call would fire.
    ReentrantFakeTransport t;
    PopoutController c(&t);
    const QString h1 = c.open(makeRequest(QStringLiteral("coop-1"), ExclusiveMode::Cooperative, QStringLiteral("s1")));
    const QString h2 = c.open(makeRequest(QStringLiteral("coop-2"), ExclusiveMode::Cooperative, QStringLiteral("s2")));
    QVERIFY(!h1.isEmpty());
    QVERIFY(!h2.isEmpty());

    t.crossHandleEcho.insert(h1, h2);

    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    QVERIFY(!c.open(makeRequest(QStringLiteral("alert"), ExclusiveMode::Modal)).isEmpty());

    QCOMPARE(closedSpy.count(), 2);
    QCOMPARE(t.closeSurfaceCalls, 2);
    QVERIFY(!c.isOpen(QStringLiteral("coop-1")));
    QVERIFY(!c.isOpen(QStringLiteral("coop-2")));
    QVERIFY(c.isOpen(QStringLiteral("alert")));
}

void TestPopoutControllerDismiss::dismissedCallback_reentrantTransportSuppressedDuringCloseAll()
{
    // closeAll's ScopedTrue guard is genuinely load-bearing only when
    // the contract-violating transport fires dismissed for a sibling
    // handle (cross-handle echo). For the same-handle echo case, the
    // erase-before-closeSurface ordering in removeEntry already
    // prevents corruption. This test sets up the cross-handle
    // scenario and pins the guard via the transport's closeSurface
    // call count.
    //
    // With the guard, closeAll's iteration of [h1, h2] reaches both:
    //   removeEntry(h1) -> erase h1 -> closeSurface(h1)
    //     -> transport fires dismissed(h2) cross-handle
    //     -> callback short-circuits because inSelfTeardown is true
    //     -> h2 still in entries
    //   removeEntry(h2) -> erase h2 -> closeSurface(h2)
    //   Total closeSurfaceCalls: 2
    //
    // Without the guard, the cross-handle callback would removeEntryQuiet(h2)
    // mid-iteration:
    //   removeEntry(h1) -> erase h1 -> closeSurface(h1)
    //     -> dismissed(h2) -> removeEntryQuiet(h2) erases h2
    //   removeEntry(h2) finds nothing, bails (no closeSurface call)
    //   Total closeSurfaceCalls: 1
    //
    // closeSurfaceCalls == 2 pins the guard's effect.
    ReentrantFakeTransport t;
    PopoutController c(&t);
    const QString h1 = c.open(makeRequest(QStringLiteral("coop-1"), ExclusiveMode::Cooperative, QStringLiteral("s1")));
    const QString h2 = c.open(makeRequest(QStringLiteral("coop-2"), ExclusiveMode::Cooperative, QStringLiteral("s2")));
    QVERIFY(!h1.isEmpty());
    QVERIFY(!h2.isEmpty());

    // Wire the cross-handle echo: closing h1 will fire dismissed for h2.
    t.crossHandleEcho.insert(h1, h2);

    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    c.closeAll();

    QCOMPARE(closedSpy.count(), 2);
    QCOMPARE(t.closeSurfaceCalls, 2);
    QVERIFY(!c.isOpen(QStringLiteral("coop-1")));
    QVERIFY(!c.isOpen(QStringLiteral("coop-2")));
}

void TestPopoutControllerDismiss::close_thenLateDismissDoesNotDoubleFire()
{
    // A real layer-shell can deliver a dismissed event for a handle
    // the controller already removed via close() (the surface was
    // self-dismissing at the same moment we asked it to close). The
    // controller's erase-before-closeSurface ordering plus
    // removeEntryQuiet's "find returns end" no-op absorbs this. This
    // test pins that contract: popoutClosed must fire exactly once
    // (from the caller-initiated close) and closeSurface must run
    // exactly once.
    FakeTransport t;
    PopoutController c(&t);
    const QString h = c.open(makeRequest(QStringLiteral("a")));
    QVERIFY(!h.isEmpty());

    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    const int callsBefore = t.closeSurfaceCalls;
    c.close(h);
    // Simulate the compositor's late dismiss for the handle that
    // we just asked to close. The controller's entries already lack
    // the handle; the callback finds nothing and no-ops.
    QVERIFY(t.dismissedCb);
    t.dismissedCb(h);

    QCOMPARE(closedSpy.count(), 1);
    QCOMPARE(t.closeSurfaceCalls, callsBefore + 1);
}

void TestPopoutControllerDismiss::destructor_detachesDismissedCallback()
{
    // The controller's destructor must clear the transport's
    // callback. Without that, any subsequent dismiss invokes a
    // lambda that captures the now-dangling controller and crashes.
    // Pin both observable consequences of the detach: (1) the
    // callback object is empty afterwards, and (2) the controller
    // actively drove the detach via setSurfaceDismissedCallback({})
    // rather than the lambda being discarded as a side effect.
    FakeTransport t;
    QCOMPARE(t.clearCallbackCalls, 0);
    {
        PopoutController c(&t);
        QVERIFY(t.dismissedCb);
        QCOMPARE(t.clearCallbackCalls, 0);
    }
    QVERIFY(!t.dismissedCb);
    QCOMPARE(t.clearCallbackCalls, 1);
}

void TestPopoutControllerDismiss::destructor_withOpenPopoutsDoesNotDrainTransport()
{
    // Pin the documented destruction behaviour: the controller does
    // NOT auto-close outstanding popouts on destruction. Callers that
    // want a clean teardown invoke closeAll() before letting the
    // controller die. The transport's destructor handles surface
    // teardown for any handles still alive at that point. This test
    // catches a regression that quietly adds an implicit closeAll
    // in the destructor and starts emitting popoutClosed signals
    // after the controller is already half-dead.
    //
    // Use distinct scopes plus a Detached so all three popouts
    // genuinely coexist and the arbitration doesn't close any of
    // them before the controller dies.
    FakeTransport t;
    std::unique_ptr<QSignalSpy> closedSpy;
    {
        PopoutController c(&t);
        QVERIFY(!c.open(makeRequest(QStringLiteral("a"), ExclusiveMode::Cooperative, QStringLiteral("s1"))).isEmpty());
        QVERIFY(!c.open(makeRequest(QStringLiteral("b"), ExclusiveMode::Cooperative, QStringLiteral("s2"))).isEmpty());
        QVERIFY(!c.open(makeRequest(QStringLiteral("c"), ExclusiveMode::Detached)).isEmpty());
        QCOMPARE(t.alive.size(), 3);
        closedSpy.reset(new QSignalSpy(&c, &PopoutController::popoutClosed));
        // controller goes out of scope here
    }
    QCOMPARE(closedSpy->count(), 0);
    // Transport still holds the surfaces. Its own destructor cleans
    // them; the controller does not call closeSurface for them.
    QCOMPARE(t.closeSurfaceCalls, 0);
    QCOMPARE(t.alive.size(), 3);
    // Destructor must still drive the callback detach even with open
    // popouts (i.e., the open-popouts path doesn't bypass the dtor's
    // setSurfaceDismissedCallback({}) call).
    QCOMPARE(t.clearCallbackCalls, 1);
}

QTEST_MAIN(TestPopoutControllerDismiss)
#include "test_popoutcontroller_dismiss.moc"
