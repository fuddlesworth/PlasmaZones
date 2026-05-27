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

    // Test helper. Simulates the surface dismissing itself (focus loss,
    // user click-outside, compositor revocation). Routes through the
    // callback the controller registered, which removes the handle from
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
    void open_detachedSurvivesArbitration();
    void open_sameIdReturnsEmpty();
    void open_transportRefusalReturnsEmpty();
    void close_unknownHandleNoOp();
    void close_modalClearsModalActiveAfterStackDrain();
    void toggle_openWhenClosed();
    void toggle_closeWhenOpen();
    void toggle_emptyIdRoutesToOpen();
    void isOpen_tracksByPopoutId();
    void closeAll_drainsEntriesAndClearsModalCount();
    void dismissedCallback_removesEntryAndUpdatesModalState();
    void dismissedCallback_unknownHandleNoOp();
    void modalActiveChanged_firesOnFirstAndLast();
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

    const QString h1 = c.open(makeRequest(QStringLiteral("a"), ExclusiveMode::Cooperative, QStringLiteral("scope-1")));
    const QString h2 = c.open(makeRequest(QStringLiteral("b"), ExclusiveMode::Cooperative, QStringLiteral("scope-2")));

    QVERIFY(!h1.isEmpty());
    QVERIFY(!h2.isEmpty());
    QVERIFY(c.isOpen(QStringLiteral("a")));
    QVERIFY(c.isOpen(QStringLiteral("b")));
    QCOMPARE(t.alive.size(), 2);
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
    // (One open already, for the alert; this is the only one.)
    QCOMPARE(t.openLog.size(), 1);
}

void TestPopoutController::open_modalStacksOnModal()
{
    FakeTransport t;
    PopoutController c(&t);

    const QString m1 = c.open(makeRequest(QStringLiteral("alert-1"), ExclusiveMode::Modal));
    QSignalSpy modalSpy(&c, &PopoutController::modalActiveChanged);
    const QString m2 = c.open(makeRequest(QStringLiteral("alert-2"), ExclusiveMode::Modal));

    QVERIFY(!m1.isEmpty());
    QVERIFY(!m2.isEmpty());
    QVERIFY(c.isOpen(QStringLiteral("alert-1")));
    QVERIFY(c.isOpen(QStringLiteral("alert-2")));
    QVERIFY(c.isModalActive());
    // modalActiveChanged should NOT fire on the second modal because
    // the state is already "active".
    QCOMPARE(modalSpy.count(), 0);
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
    // silent duplicate. Callers must close() first to reopen.
    QVERIFY(h2.isEmpty());
    QCOMPARE(t.alive.size(), 1);
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
    t.dismiss(h);

    QCOMPARE(closedSpy.count(), 1);
    QCOMPARE(closedSpy.first().at(0).toString(), QStringLiteral("alert"));
    QCOMPARE(closedSpy.first().at(1).toString(), h);
    QVERIFY(!c.isOpen(QStringLiteral("alert")));
    QVERIFY(!c.isModalActive());
    QCOMPARE(modalSpy.count(), 1);
}

void TestPopoutController::dismissedCallback_unknownHandleNoOp()
{
    FakeTransport t;
    PopoutController c(&t);
    QSignalSpy closedSpy(&c, &PopoutController::popoutClosed);
    if (t.dismissedCb) {
        t.dismissedCb(QStringLiteral("never-existed"));
    }
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

QTEST_MAIN(TestPopoutController)
#include "test_popoutcontroller.moc"
