// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorPopout/IPopoutTransport.h>
#include <PhosphorPopout/PopoutController.h>
#include <PhosphorPopout/PopoutRequest.h>

#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>

#include <functional>

using namespace PhosphorPopout;

// Minimal in-memory transport. Records every openSurface call, returns
// monotonically incrementing handles, and exposes a `dismiss()` helper
// that fires the controller's dismissed callback the way a real
// layer-shell would when a surface goes away on its own. No QML, no
// Wayland, no Qt event loop required.
class FakeTransport : public IPopoutTransport
{
public:
    QString openSurface(const PopoutRequest& request) override
    {
        if (refuseNextOpen) {
            refuseNextOpen = false;
            return {};
        }
        const QString handle = QStringLiteral("h%1").arg(++counter);
        alive.insert(handle, request.popoutId);
        openLog.append(request.popoutId);
        return handle;
    }

    void closeSurface(const QString& handle) override
    {
        // Counter is bumped on every entry, even for unknown
        // handles, so the test suite can pin "no transport call at
        // all" contracts. closeLog stays gated on alive so it only
        // captures handles the transport actually owned.
        ++closeSurfaceCalls;
        if (!alive.contains(handle)) {
            return;
        }
        closeLog.append(alive.value(handle));
        alive.remove(handle);
    }

    void setSurfaceDismissedCallback(std::function<void(const QString&)> cb) override
    {
        dismissedCb = std::move(cb);
    }

    // Test helper. Simulates the surface dismissing itself. Mirrors
    // how a real layer-shell signals focus loss, a click outside the
    // surface, or compositor revocation. Routes through the callback
    // the controller registered. The callback removes the handle from
    // the controller's tables.
    void dismiss(const QString& handle)
    {
        if (!alive.contains(handle)) {
            return;
        }
        alive.remove(handle);
        if (dismissedCb) {
            dismissedCb(handle);
        }
    }

    QHash<QString, QString> alive;
    QStringList openLog;
    QStringList closeLog;
    int closeSurfaceCalls = 0;
    int counter = 0;
    bool refuseNextOpen = false;
    std::function<void(const QString&)> dismissedCb;
};

namespace {

PopoutRequest makeRequest(const QString& id, ExclusiveMode mode = ExclusiveMode::Cooperative,
                          const QString& scope = QStringLiteral("default"))
{
    PopoutRequest req;
    req.popoutId = id;
    req.scope = scope;
    req.exclusive = mode;
    return req;
}

} // namespace

class TestPopoutController : public QObject
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
    void open_sameIdReturnsEmpty();
    void open_transportRefusalReturnsEmpty();
    void close_unknownHandleNoOp();
    void close_emptyHandleNoOp();
    void close_modalClearsModalActiveAfterStackDrain();
    void toggle_openWhenClosed();
    void toggle_closeWhenOpen();
    void toggle_emptyIdRoutesToOpen();
    void toggle_emptyIdAlwaysOpens();
    void isOpen_tracksByPopoutId();
    void handleFor_unknownIdReturnsEmpty();
    void closeAll_drainsEntriesAndClearsModalCount();
    void dismissedCallback_installedDuringConstruction();
    void dismissedCallback_removesEntryAndUpdatesModalState();
    void dismissedCallback_cooperativeDoesNotEmitModalChange();
    void dismissedCallback_unknownHandleNoOp();
    void dismissedCallback_reentrantTransportSuppressedDuringSelfTeardown();
    void dismissedCallback_reentrantTransportSuppressedDuringCloseAll();
    void modalActiveChanged_firesOnFirstAndLast();
    void destructor_detachesDismissedCallback();
    void destructor_withOpenPopoutsDoesNotDrainTransport();
    void closeAll_onEmptyControllerIsNoOp();
    void close_detachedFiresClosedAndUpdatesIsOpen();
    void open_modalTransportRefusalLeavesCooperativesDrainedAndModalInactive();
    void open_modalSameIdRefusalWhileModalUpDoesNotTouchCount();
    void close_thenLateDismissDoesNotDoubleFire();
    void popoutOpened_handlerObservesConsistentState();
    void popoutClosed_handlerObservesConsistentState();
    void popoutOpened_handlerCanCallOpen();
    void popoutClosed_handlerCanCallCloseAll();
    void isOpen_emptyPopoutIdMatchesAnonymous();
    void defaultScope_isSharedInstance();
};

void TestPopoutController::open_cooperativeReturnsNonEmptyHandle()
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

void TestPopoutController::open_secondCooperativeSameScopeReplacesFirst()
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

void TestPopoutController::open_cooperativeDifferentScopesCoexist()
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

void TestPopoutController::open_modalClosesAllCooperatives()
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

void TestPopoutController::open_cooperativeRejectedWhileModalActive()
{
    FakeTransport t;
    PopoutController c(&t);
    QVERIFY(!c.open(makeRequest(QStringLiteral("alert"), ExclusiveMode::Modal)).isEmpty());
    QVERIFY(c.isModalActive());

    QSignalSpy openedSpy(&c, &PopoutController::popoutOpened);
    const QString h = c.open(makeRequest(QStringLiteral("calendar")));

    QVERIFY(h.isEmpty());
    QCOMPARE(openedSpy.count(), 0);
    QVERIFY(!c.isOpen(QStringLiteral("calendar")));
    // Transport must NOT have been invoked for the rejected request.
    // The only entry in openLog is the alert that opened up first.
    QCOMPARE(t.openLog.size(), 1);
}

void TestPopoutController::open_modalStacksOnModal()
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

void TestPopoutController::open_detachedSurvivesArbitration()
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

void TestPopoutController::open_sameIdReturnsEmpty()
{
    FakeTransport t;
    PopoutController c(&t);

    const QString h1 = c.open(makeRequest(QStringLiteral("a")));
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
}

void TestPopoutController::open_transportRefusalReturnsEmpty()
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

void TestPopoutController::close_unknownHandleNoOp()
{
    FakeTransport t;
    PopoutController c(&t);
    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    c.close(QStringLiteral("never-existed"));
    QCOMPARE(closedSpy.count(), 0);
}

void TestPopoutController::close_modalClearsModalActiveAfterStackDrain()
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

void TestPopoutController::toggle_openWhenClosed()
{
    FakeTransport t;
    PopoutController c(&t);
    const QString h = c.toggle(makeRequest(QStringLiteral("a")));
    QVERIFY(!h.isEmpty());
    QVERIFY(c.isOpen(QStringLiteral("a")));
}

void TestPopoutController::toggle_closeWhenOpen()
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

void TestPopoutController::toggle_emptyIdRoutesToOpen()
{
    FakeTransport t;
    PopoutController c(&t);
    PopoutRequest req; // empty popoutId
    const QString h = c.toggle(req);
    QVERIFY(!h.isEmpty());
}

void TestPopoutController::isOpen_tracksByPopoutId()
{
    FakeTransport t;
    PopoutController c(&t);
    QVERIFY(!c.isOpen(QStringLiteral("a")));
    QVERIFY(!c.open(makeRequest(QStringLiteral("a"))).isEmpty());
    QVERIFY(c.isOpen(QStringLiteral("a")));
    QVERIFY(!c.isOpen(QStringLiteral("b")));
}

void TestPopoutController::closeAll_drainsEntriesAndClearsModalCount()
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

void TestPopoutController::dismissedCallback_removesEntryAndUpdatesModalState()
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

void TestPopoutController::dismissedCallback_unknownHandleNoOp()
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

void TestPopoutController::modalActiveChanged_firesOnFirstAndLast()
{
    FakeTransport t;
    PopoutController c(&t);

    QSignalSpy modalSpy(&c, &PopoutController::modalActiveChanged);
    const QString h1 = c.open(makeRequest(QStringLiteral("a"), ExclusiveMode::Modal));
    QVERIFY(!h1.isEmpty());
    QCOMPARE(modalSpy.count(), 1);

    const QString h2 = c.open(makeRequest(QStringLiteral("b"), ExclusiveMode::Modal));
    QVERIFY(!h2.isEmpty());
    QCOMPARE(modalSpy.count(), 1); // second modal does not refire

    c.close(h1);
    QCOMPARE(modalSpy.count(), 1); // still one modal up

    c.close(h2);
    QCOMPARE(modalSpy.count(), 2); // last modal cleared
}

void TestPopoutController::open_modalSameIdRejected()
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

void TestPopoutController::open_detachedAcceptedWhileModalActive()
{
    FakeTransport t;
    PopoutController c(&t);
    QVERIFY(!c.open(makeRequest(QStringLiteral("alert"), ExclusiveMode::Modal)).isEmpty());
    QVERIFY(c.isModalActive());

    // Detached bypasses the modal-suppression check entirely.
    const QString h = c.open(makeRequest(QStringLiteral("note"), ExclusiveMode::Detached));
    QVERIFY(!h.isEmpty());
    QVERIFY(c.isOpen(QStringLiteral("note")));
}

void TestPopoutController::open_emptyIdAllowsMultiple()
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

void TestPopoutController::close_emptyHandleNoOp()
{
    FakeTransport t;
    PopoutController c(&t);
    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    c.close(QString());
    QCOMPARE(closedSpy.count(), 0);
    QCOMPARE(t.closeLog.size(), 0);
}

void TestPopoutController::toggle_emptyIdAlwaysOpens()
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

void TestPopoutController::handleFor_unknownIdReturnsEmpty()
{
    FakeTransport t;
    PopoutController c(&t);
    QVERIFY(c.handleFor(QStringLiteral("ghost")).isEmpty());
    QVERIFY(!c.open(makeRequest(QStringLiteral("a"))).isEmpty());
    QVERIFY(c.handleFor(QStringLiteral("ghost")).isEmpty());
}

void TestPopoutController::dismissedCallback_installedDuringConstruction()
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

void TestPopoutController::dismissedCallback_cooperativeDoesNotEmitModalChange()
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

// Re-entrant fake. Synchronously fires the dismissed callback from
// inside closeSurface, violating the IPopoutTransport contract on
// purpose so the controller's `inSelfTeardown` guard is exercised.
class ReentrantFakeTransport : public IPopoutTransport
{
public:
    QString openSurface(const PopoutRequest& request) override
    {
        const QString handle = QStringLiteral("rh%1").arg(++counter);
        alive.insert(handle, request.popoutId);
        return handle;
    }
    void closeSurface(const QString& handle) override
    {
        ++closeSurfaceCalls;
        if (!alive.contains(handle)) {
            return;
        }
        alive.remove(handle);
        // Contract violation, on purpose. Real transports must not
        // do this. The guard exists so a misbehaving transport
        // cannot corrupt the controller's tables.
        if (dismissedCb) {
            // Optional cross-handle echo: when set, fire dismissed
            // for a DIFFERENT live handle than the one being closed.
            // This is the scenario the ScopedTrue guard actually
            // protects against. Same-handle echo is benign because
            // removeEntry erases the entry before calling closeSurface.
            // Cross-handle echo would let the re-entrant callback
            // remove a sibling out from under an in-flight closeAll
            // iteration if the guard were absent.
            const QString echoHandle = crossHandleEcho.value(handle, handle);
            dismissedCb(echoHandle);
        }
    }
    void setSurfaceDismissedCallback(std::function<void(const QString&)> cb) override
    {
        dismissedCb = std::move(cb);
    }
    QHash<QString, QString> alive;
    QHash<QString, QString> crossHandleEcho; // closed-handle -> echo-handle
    int closeSurfaceCalls = 0;
    int counter = 0;
    std::function<void(const QString&)> dismissedCb;
};

void TestPopoutController::dismissedCallback_reentrantTransportSuppressedDuringSelfTeardown()
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

void TestPopoutController::dismissedCallback_reentrantTransportSuppressedDuringCloseAll()
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

void TestPopoutController::destructor_detachesDismissedCallback()
{
    // The controller's destructor must clear the transport's
    // callback. Without that, any subsequent dismiss invokes a
    // lambda that captures the now-dangling controller and crashes.
    FakeTransport t;
    {
        PopoutController c(&t);
        QVERIFY(t.dismissedCb);
    }
    QVERIFY(!t.dismissedCb);
}

void TestPopoutController::destructor_withOpenPopoutsDoesNotDrainTransport()
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
    QSignalSpy* closedSpy = nullptr;
    {
        PopoutController c(&t);
        QVERIFY(!c.open(makeRequest(QStringLiteral("a"), ExclusiveMode::Cooperative, QStringLiteral("s1"))).isEmpty());
        QVERIFY(!c.open(makeRequest(QStringLiteral("b"), ExclusiveMode::Cooperative, QStringLiteral("s2"))).isEmpty());
        QVERIFY(!c.open(makeRequest(QStringLiteral("c"), ExclusiveMode::Detached)).isEmpty());
        QCOMPARE(t.alive.size(), 3);
        const int callsBeforeSpy = t.closeSurfaceCalls;
        closedSpy = new QSignalSpy(&c, &PopoutController::popoutClosed);
        // controller goes out of scope here
        QCOMPARE(callsBeforeSpy, 0); // sanity: no close calls so far
    }
    QCOMPARE(closedSpy->count(), 0);
    // Transport still holds the surfaces. Its own destructor cleans
    // them; the controller does not call closeSurface for them.
    QCOMPARE(t.closeSurfaceCalls, 0);
    QCOMPARE(t.alive.size(), 3);
    delete closedSpy;
}

void TestPopoutController::closeAll_onEmptyControllerIsNoOp()
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

void TestPopoutController::close_detachedFiresClosedAndUpdatesIsOpen()
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

void TestPopoutController::open_modalTransportRefusalLeavesCooperativesDrainedAndModalInactive()
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

void TestPopoutController::open_modalSameIdRefusalWhileModalUpDoesNotTouchCount()
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

void TestPopoutController::close_thenLateDismissDoesNotDoubleFire()
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

void TestPopoutController::popoutOpened_handlerObservesConsistentState()
{
    // The controller's signal contract says popoutOpened fires AFTER
    // the entry is added to the table. A slot inspecting isOpen()
    // and handleFor() inside the handler must see the new state. A
    // regression that emits the signal before the insert would fail
    // these assertions.
    FakeTransport t;
    PopoutController c(&t);
    bool slotRan = false;
    QObject::connect(&c, &PopoutController::popoutOpened, [&](const QString& popoutId, const QString& handle) {
        slotRan = true;
        QVERIFY(c.isOpen(popoutId));
        QCOMPARE(c.handleFor(popoutId), handle);
    });
    QVERIFY(!c.open(makeRequest(QStringLiteral("a"))).isEmpty());
    QVERIFY(slotRan);
}

void TestPopoutController::popoutClosed_handlerObservesConsistentState()
{
    // Symmetric assertion for popoutClosed: isOpen() must return false
    // and isModalActive() must reflect the post-decrement state when
    // a slot runs. The Q_EMIT ordering in removeEntry/removeEntryQuiet
    // pins this.
    FakeTransport t;
    PopoutController c(&t);
    QVERIFY(!c.open(makeRequest(QStringLiteral("alert"), ExclusiveMode::Modal)).isEmpty());
    QVERIFY(c.isModalActive());

    bool slotRan = false;
    QObject::connect(&c, &PopoutController::popoutClosed, [&](const QString& popoutId, const QString&) {
        slotRan = true;
        QVERIFY(!c.isOpen(popoutId));
        QVERIFY(!c.isModalActive());
    });
    const QString h = c.handleFor(QStringLiteral("alert"));
    QVERIFY(!h.isEmpty());
    c.close(h);
    QVERIFY(slotRan);
}

void TestPopoutController::popoutOpened_handlerCanCallOpen()
{
    // QML shells observe popoutOpened and may chain a sibling open
    // (e.g. an OSD that fires a confirmation toast). The controller
    // must remain consistent across re-entrant open() from inside a
    // slot. Without proper re-entrant handling, the chained open
    // could leave the entries table inconsistent.
    FakeTransport t;
    PopoutController c(&t);
    int slotInvocations = 0;
    QObject::connect(&c, &PopoutController::popoutOpened, [&](const QString& popoutId, const QString&) {
        ++slotInvocations;
        if (popoutId == QLatin1String("first")) {
            const QString chained = c.open(makeRequest(QStringLiteral("second"), ExclusiveMode::Detached));
            QVERIFY(!chained.isEmpty());
        }
    });
    QVERIFY(!c.open(makeRequest(QStringLiteral("first"))).isEmpty());
    QCOMPARE(slotInvocations, 2);
    QVERIFY(c.isOpen(QStringLiteral("first")));
    QVERIFY(c.isOpen(QStringLiteral("second")));
    QCOMPARE(t.alive.size(), 2);
}

void TestPopoutController::popoutClosed_handlerCanCallCloseAll()
{
    // A panic-close slot connected to popoutClosed can call closeAll
    // to drain whatever else is up. closeAll's ScopedTrue guard plus
    // the snapshot-then-iterate loop must survive being invoked from
    // inside an outer close-emit. No double-emit, no crash.
    FakeTransport t;
    PopoutController c(&t);
    QVERIFY(!c.open(makeRequest(QStringLiteral("a"))).isEmpty());
    QVERIFY(!c.open(makeRequest(QStringLiteral("b"), ExclusiveMode::Cooperative, QStringLiteral("s2"))).isEmpty());
    QVERIFY(!c.open(makeRequest(QStringLiteral("c"), ExclusiveMode::Detached)).isEmpty());

    bool firstFire = true;
    QObject::connect(&c, &PopoutController::popoutClosed, [&](const QString&, const QString&) {
        if (firstFire) {
            firstFire = false;
            c.closeAll();
        }
    });

    const QString hA = c.handleFor(QStringLiteral("a"));
    QVERIFY(!hA.isEmpty());
    c.close(hA);

    QVERIFY(!c.isOpen(QStringLiteral("a")));
    QVERIFY(!c.isOpen(QStringLiteral("b")));
    QVERIFY(!c.isOpen(QStringLiteral("c")));
    QCOMPARE(t.alive.size(), 0);
}

void TestPopoutController::isOpen_emptyPopoutIdMatchesAnonymous()
{
    // Anonymous popouts (popoutId empty) are allowed per
    // open_emptyIdAllowsMultiple. isOpen("") then returns true so
    // long as any anonymous popout is alive. handleFor("") returns
    // the handle of one of them. Pin this so callers can reason
    // about the empty-id semantics without surprises.
    FakeTransport t;
    PopoutController c(&t);
    QVERIFY(!c.isOpen(QString()));
    QVERIFY(c.handleFor(QString()).isEmpty());

    PopoutRequest req;
    req.scope = QStringLiteral("scope-a");
    const QString h = c.open(req);
    QVERIFY(!h.isEmpty());
    QVERIFY(c.isOpen(QString()));
    QCOMPARE(c.handleFor(QString()), h);
}

void TestPopoutController::defaultScope_isSharedInstance()
{
    // PopoutRequest::DefaultScope is the canonical default. Two
    // default-constructed requests share the same QString refcount
    // rather than each allocating a fresh "default". Pin the value
    // and the sharing so a future refactor that drops the static
    // is caught.
    PopoutRequest a;
    PopoutRequest b;
    QCOMPARE(a.scope, QStringLiteral("default"));
    QCOMPARE(a.scope, PopoutRequest::DefaultScope);
    QCOMPARE(b.scope, PopoutRequest::DefaultScope);
    // Same data pointer = same refcount = no per-construction alloc.
    QCOMPARE(a.scope.constData(), b.scope.constData());
}

QTEST_MAIN(TestPopoutController)
#include "test_popoutcontroller.moc"
