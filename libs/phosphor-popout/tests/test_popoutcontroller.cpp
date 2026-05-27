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

    bool isSurfaceAlive(const QString& handle) const override
    {
        return alive.contains(handle);
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
    t.dismissedCb(QStringLiteral("never-existed"));
    QCOMPARE(closedSpy.count(), 0);
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
        if (!alive.contains(handle)) {
            return;
        }
        alive.remove(handle);
        // Contract violation, on purpose. Real transports must not
        // do this. The guard exists so a misbehaving transport
        // cannot corrupt the controller's tables.
        if (dismissedCb) {
            dismissedCb(handle);
        }
    }
    bool isSurfaceAlive(const QString& handle) const override
    {
        return alive.contains(handle);
    }
    void setSurfaceDismissedCallback(std::function<void(const QString&)> cb) override
    {
        dismissedCb = std::move(cb);
    }
    QHash<QString, QString> alive;
    int counter = 0;
    std::function<void(const QString&)> dismissedCb;
};

void TestPopoutController::dismissedCallback_reentrantTransportSuppressedDuringSelfTeardown()
{
    // A transport that synchronously fires dismissed inside
    // closeSurface would re-enter the controller's mutation paths
    // and double-emit popoutClosed or corrupt iteration. The
    // inSelfTeardown guard suppresses the callback during open's
    // suppression closes and during closeAll.
    ReentrantFakeTransport t;
    PopoutController c(&t);
    QVERIFY(!c.open(makeRequest(QStringLiteral("coop-1"), ExclusiveMode::Cooperative, QStringLiteral("s1"))).isEmpty());
    QVERIFY(!c.open(makeRequest(QStringLiteral("coop-2"), ExclusiveMode::Cooperative, QStringLiteral("s2"))).isEmpty());

    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    // Opening a Modal closes both cooperatives via the suppression
    // path. The re-entrant transport's dismissed-from-closeSurface
    // must be suppressed by the guard so popoutClosed fires exactly
    // twice, not four times.
    QVERIFY(!c.open(makeRequest(QStringLiteral("alert"), ExclusiveMode::Modal)).isEmpty());
    QCOMPARE(closedSpy.count(), 2);
}

void TestPopoutController::dismissedCallback_reentrantTransportSuppressedDuringCloseAll()
{
    // closeAll has its own ScopedTrue guard. Without it, a re-entrant
    // transport that fires dismissed-from-closeSurface would loop
    // into the callback and double-emit popoutClosed for every
    // entry being drained. This test exercises the closeAll guard
    // specifically; the open-Modal path is covered separately by
    // dismissedCallback_reentrantTransportSuppressedDuringSelfTeardown.
    ReentrantFakeTransport t;
    PopoutController c(&t);
    QVERIFY(!c.open(makeRequest(QStringLiteral("coop-1"), ExclusiveMode::Cooperative, QStringLiteral("s1"))).isEmpty());
    QVERIFY(!c.open(makeRequest(QStringLiteral("alert"), ExclusiveMode::Modal)).isEmpty());
    QVERIFY(c.isModalActive());

    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    QSignalSpy modalSpy(&c, &PopoutController::modalActiveChanged);
    c.closeAll();

    // alert + a fresh cooperative opened after the modal would also
    // be closed. We only opened one cooperative and the modal closed
    // it before opening, so closeAll drains the modal alone.
    QCOMPARE(closedSpy.count(), 1);
    QCOMPARE(closedSpy.first().at(0).toString(), QStringLiteral("alert"));
    QVERIFY(!c.isModalActive());
    QCOMPARE(modalSpy.count(), 1);
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

QTEST_MAIN(TestPopoutController)
#include "test_popoutcontroller.moc"
