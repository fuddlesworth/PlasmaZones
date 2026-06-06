// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Signal-ordering, slot-state-consistency, modalActiveChanged, and
// PopoutRequest::DefaultScope tests for PopoutController. Arbitration
// coverage lives in test_popoutcontroller_arbitration.cpp;
// dismissed-callback / destructor coverage lives in
// test_popoutcontroller_dismiss.cpp.

#include "test_popoutcontroller_helpers.h"

#include <PhosphorPopout/PopoutController.h>

#include <QSignalSpy>
#include <QTest>

using namespace PhosphorPopout;
using PhosphorPopoutTest::makeRequest;

class TestPopoutControllerSignals : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void modalActiveChanged_firesOnFirstAndLast();
    void popoutOpened_handlerObservesConsistentState();
    void popoutClosed_handlerObservesConsistentState();
    void popoutOpened_handlerCanCallOpen();
    void popoutClosed_handlerCanCallCloseAll();
    void isOpen_emptyPopoutIdMatchesAnonymous();
    void defaultScope_isCanonicalLiteral();
};

void TestPopoutControllerSignals::modalActiveChanged_firesOnFirstAndLast()
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

void TestPopoutControllerSignals::popoutOpened_handlerObservesConsistentState()
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

void TestPopoutControllerSignals::popoutClosed_handlerObservesConsistentState()
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

void TestPopoutControllerSignals::popoutOpened_handlerCanCallOpen()
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

void TestPopoutControllerSignals::popoutClosed_handlerCanCallCloseAll()
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

void TestPopoutControllerSignals::isOpen_emptyPopoutIdMatchesAnonymous()
{
    // Anonymous popouts (popoutId empty) are allowed per the
    // arbitration tests' open_emptyIdAllowsMultiple. isOpen("") then
    // returns true so long as any anonymous popout is alive.
    // handleFor("") returns the handle of one of them. Pin this so
    // callers can reason about the empty-id semantics without
    // surprises.
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

void TestPopoutControllerSignals::defaultScope_isCanonicalLiteral()
{
    // PopoutRequest::DefaultScope is the canonical default. Pin the
    // value so a future refactor that drops the static is caught.
    // Earlier passes pinned constData()-equality between two default-
    // constructed requests, but that asserted QString implementation
    // detail (literal-pool interning) that can change across Qt
    // patch releases and across local-vs-static QStringLiteral sites.
    // Behavioural equality is the contract; pin that.
    PopoutRequest a;
    PopoutRequest b;
    QCOMPARE(a.scope, QStringLiteral("default"));
    QCOMPARE(a.scope, PopoutRequest::DefaultScope);
    QCOMPARE(b.scope, PopoutRequest::DefaultScope);
    QCOMPARE(a.scope, b.scope);
}

QTEST_MAIN(TestPopoutControllerSignals)
#include "test_popoutcontroller_signals.moc"
