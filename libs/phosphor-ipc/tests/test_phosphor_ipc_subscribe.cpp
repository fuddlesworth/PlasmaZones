// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Subscribe / broadcast / unsubscribe roundtrip tests. A test target
// QObject exposes one signal-shaped surface; the router subscribes
// a real QLocalSocket client to it; the test triggers
// broadcastEvent and asserts events arrive on the wire in order.

#include "ipctesthelpers.h"

#include <PhosphorIpc/IpcProtocol.h>
#include <PhosphorIpc/IpcRouter.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QTest>
#include <QtCore/qtclasshelpermacros.h>

using namespace PhosphorIpc;
using PhosphorIpcTests::makeReq;
using PhosphorIpcTests::readLines;
using PhosphorIpcTests::RouterFixture;

namespace {

class CounterTarget : public QObject
{
    Q_OBJECT
public:
    explicit CounterTarget(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    Q_DISABLE_COPY_MOVE(CounterTarget)

    Q_INVOKABLE int increment()
    {
        ++m_value;
        return m_value;
    }

Q_SIGNALS:
    // Declared so subscribe's signal-existence check (findSignal
    // against the target's metaObject) succeeds. Subscribers
    // receive events via the router's broadcastEvent path, not via
    // Qt's signal/slot mechanism — broadcastEvent is called
    // directly from each test body — so this signal never actually
    // fires from C++. Its sole purpose is to make the metaobject
    // advertise "countChanged" as a Public signal name; the dispatch
    // path is otherwise correct end-to-end.
    void countChanged(int v); // NOLINT(modernize-use-trailing-return-type): Q_SIGNALS shape

private:
    int m_value = 0;
};

} // namespace

class TestPhosphorIpcSubscribe : public QObject
{
    Q_OBJECT
public:
    Q_DISABLE_COPY_MOVE(TestPhosphorIpcSubscribe)
    TestPhosphorIpcSubscribe() = default;
private Q_SLOTS:
    void subscribe_replies_then_streams_events();
    void subscribe_unknownTarget();
    void subscribe_unknownSignal();
    void subscribe_duplicateRejected();
    void subscribe_perSocketCapExceeded();
    void unsubscribe_stops_events();
    void unsubscribe_unknownId();
    void multipleSubscribers_eachReceiveEvent();
    void disconnect_pruneSubscriptions();
};

void TestPhosphorIpcSubscribe::subscribe_replies_then_streams_events()
{
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    CounterTarget c;
    QVERIFY(router.registerTarget(QStringLiteral("count"), &c));
    QVERIFY(router.start(fx.sockPath));

    QLocalSocket socket;
    socket.connectToServer(fx.sockPath);
    QVERIFY(socket.waitForConnected(2000));

    socket.write(
        writeLine(makeReq(QStringLiteral("subscribe"), 1, QStringLiteral("count"), QStringLiteral("countChanged"))));
    socket.flush();
    // First line is the subscribe ack.
    const QList<QJsonObject> ack = readLines(socket, 1);
    QCOMPARE(ack.size(), 1);
    QCOMPARE(ack.first().value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(ack.first().value(QStringLiteral("id")).toInt(), 1);

    // Now broadcast two events and assert they arrive in order.
    QJsonArray args1;
    args1.append(1);
    router.broadcastEvent(QStringLiteral("count"), QStringLiteral("countChanged"), args1);
    QJsonArray args2;
    args2.append(2);
    router.broadcastEvent(QStringLiteral("count"), QStringLiteral("countChanged"), args2);

    const QList<QJsonObject> events = readLines(socket, 2);
    QCOMPARE(events.size(), 2);
    QCOMPARE(events.at(0).value(QStringLiteral("type")).toString(), QStringLiteral("event"));
    QCOMPARE(events.at(0).value(QStringLiteral("subscriptionId")).toInt(), 1);
    QCOMPARE(events.at(0).value(QStringLiteral("args")).toArray().first().toInt(), 1);
    QCOMPARE(events.at(1).value(QStringLiteral("args")).toArray().first().toInt(), 2);
}

void TestPhosphorIpcSubscribe::subscribe_unknownTarget()
{
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    QVERIFY(router.start(fx.sockPath));

    QLocalSocket socket;
    socket.connectToServer(fx.sockPath);
    QVERIFY(socket.waitForConnected(2000));

    socket.write(
        writeLine(makeReq(QStringLiteral("subscribe"), 1, QStringLiteral("ghost"), QStringLiteral("countChanged"))));
    socket.flush();
    const QList<QJsonObject> resp = readLines(socket, 1);
    QCOMPARE(resp.size(), 1);
    QCOMPARE(resp.first().value(QStringLiteral("type")).toString(), QStringLiteral("error"));
    QCOMPARE(resp.first().value(QStringLiteral("code")).toString(), QStringLiteral("NO_SUCH_TARGET"));
}

void TestPhosphorIpcSubscribe::subscribe_unknownSignal()
{
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    CounterTarget c;
    QVERIFY(router.registerTarget(QStringLiteral("count"), &c));
    QVERIFY(router.start(fx.sockPath));

    QLocalSocket socket;
    socket.connectToServer(fx.sockPath);
    QVERIFY(socket.waitForConnected(2000));

    socket.write(
        writeLine(makeReq(QStringLiteral("subscribe"), 1, QStringLiteral("count"), QStringLiteral("ghostSignal"))));
    socket.flush();
    const QList<QJsonObject> resp = readLines(socket, 1);
    QCOMPARE(resp.size(), 1);
    QCOMPARE(resp.first().value(QStringLiteral("type")).toString(), QStringLiteral("error"));
    QCOMPARE(resp.first().value(QStringLiteral("code")).toString(), QStringLiteral("NO_SUCH_SIGNAL"));
}

void TestPhosphorIpcSubscribe::subscribe_duplicateRejected()
{
    // Pin the per-socket duplicate-(target, signal) rejection. A
    // peer subscribing twice to the same (target, signal) on the
    // same connection must get a structured error on the second
    // call, and each broadcast must produce exactly one event on
    // the socket (not two). The cap prevents subscribe-fan
    // amplification.
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    CounterTarget c;
    QVERIFY(router.registerTarget(QStringLiteral("count"), &c));
    QVERIFY(router.start(fx.sockPath));

    QLocalSocket socket;
    socket.connectToServer(fx.sockPath);
    QVERIFY(socket.waitForConnected(2000));

    socket.write(
        writeLine(makeReq(QStringLiteral("subscribe"), 1, QStringLiteral("count"), QStringLiteral("countChanged"))));
    socket.flush();
    readLines(socket, 1); // first ack

    socket.write(
        writeLine(makeReq(QStringLiteral("subscribe"), 2, QStringLiteral("count"), QStringLiteral("countChanged"))));
    socket.flush();
    const QList<QJsonObject> resp = readLines(socket, 1);
    QCOMPARE(resp.size(), 1);
    QCOMPARE(resp.first().value(QStringLiteral("type")).toString(), QStringLiteral("error"));
    QCOMPARE(resp.first().value(QStringLiteral("code")).toString(), QStringLiteral("MALFORMED_REQUEST"));

    // One broadcast must yield exactly one event on the socket
    // (the dedup ensured the second subscribe didn't append).
    QJsonArray args;
    args.append(7);
    router.broadcastEvent(QStringLiteral("count"), QStringLiteral("countChanged"), args);
    const QList<QJsonObject> events = readLines(socket, 1);
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.first().value(QStringLiteral("subscriptionId")).toInt(), 1);
}

void TestPhosphorIpcSubscribe::unsubscribe_stops_events()
{
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    CounterTarget c;
    QVERIFY(router.registerTarget(QStringLiteral("count"), &c));
    QVERIFY(router.start(fx.sockPath));

    QLocalSocket socket;
    socket.connectToServer(fx.sockPath);
    QVERIFY(socket.waitForConnected(2000));

    socket.write(
        writeLine(makeReq(QStringLiteral("subscribe"), 1, QStringLiteral("count"), QStringLiteral("countChanged"))));
    socket.flush();
    readLines(socket, 1); // discard the ack

    QJsonArray args;
    args.append(1);
    router.broadcastEvent(QStringLiteral("count"), QStringLiteral("countChanged"), args);
    QList<QJsonObject> events = readLines(socket, 1);
    QCOMPARE(events.size(), 1);

    // Send unsubscribe.
    socket.write(writeLine(makeReq(QStringLiteral("unsubscribe"), 2, {}, {}, 1)));
    socket.flush();
    const QList<QJsonObject> unsubAck = readLines(socket, 1);
    QCOMPARE(unsubAck.first().value(QStringLiteral("type")).toString(), QStringLiteral("reply"));

    // Subsequent broadcasts must NOT generate events on this socket.
    // To verify this deterministically (rather than waiting an
    // arbitrary timeout for nothing to arrive), broadcast the event,
    // then send a synchronous `list` request immediately after. The
    // server processes requests in order on a given socket; if the
    // broadcast had erroneously produced an event, it would arrive
    // before the list reply. Read exactly one frame and assert it's
    // the list reply, never the stray event.
    router.broadcastEvent(QStringLiteral("count"), QStringLiteral("countChanged"), args);
    socket.write(writeLine(makeReq(QStringLiteral("list"), 3)));
    socket.flush();
    const QList<QJsonObject> next = readLines(socket, 1);
    QCOMPARE(next.size(), 1);
    QCOMPARE(next.first().value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(next.first().value(QStringLiteral("id")).toInt(), 3);
}

void TestPhosphorIpcSubscribe::unsubscribe_unknownId()
{
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    QVERIFY(router.start(fx.sockPath));

    QLocalSocket socket;
    socket.connectToServer(fx.sockPath);
    QVERIFY(socket.waitForConnected(2000));

    // Unsubscribe with no prior subscribe on this connection.
    socket.write(writeLine(makeReq(QStringLiteral("unsubscribe"), 1, {}, {}, 42)));
    socket.flush();
    const QList<QJsonObject> resp = readLines(socket, 1);
    QCOMPARE(resp.first().value(QStringLiteral("type")).toString(), QStringLiteral("error"));
    QCOMPARE(resp.first().value(QStringLiteral("code")).toString(), QStringLiteral("NO_SUCH_SUBSCRIPTION"));
}

void TestPhosphorIpcSubscribe::multipleSubscribers_eachReceiveEvent()
{
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    CounterTarget c;
    QVERIFY(router.registerTarget(QStringLiteral("count"), &c));
    QVERIFY(router.start(fx.sockPath));

    QLocalSocket a;
    QLocalSocket b;
    a.connectToServer(fx.sockPath);
    b.connectToServer(fx.sockPath);
    QVERIFY(a.waitForConnected(2000));
    QVERIFY(b.waitForConnected(2000));

    a.write(
        writeLine(makeReq(QStringLiteral("subscribe"), 1, QStringLiteral("count"), QStringLiteral("countChanged"))));
    b.write(
        writeLine(makeReq(QStringLiteral("subscribe"), 2, QStringLiteral("count"), QStringLiteral("countChanged"))));
    a.flush();
    b.flush();
    readLines(a, 1);
    readLines(b, 1);

    QJsonArray args;
    args.append(5);
    router.broadcastEvent(QStringLiteral("count"), QStringLiteral("countChanged"), args);
    const QList<QJsonObject> ae = readLines(a, 1);
    const QList<QJsonObject> be = readLines(b, 1);
    QCOMPARE(ae.size(), 1);
    QCOMPARE(be.size(), 1);
    QCOMPARE(ae.first().value(QStringLiteral("subscriptionId")).toInt(), 1);
    QCOMPARE(be.first().value(QStringLiteral("subscriptionId")).toInt(), 2);
}

void TestPhosphorIpcSubscribe::disconnect_pruneSubscriptions()
{
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    CounterTarget c;
    QVERIFY(router.registerTarget(QStringLiteral("count"), &c));
    QVERIFY(router.start(fx.sockPath));

    // First subscriber connects + subscribes + disconnects. If the
    // router's disconnect path FAILS to prune m_subscriptionsBySocket
    // for the dead socket, broadcastEvent later would either
    // crash writing to a dead QPointer or silently no-op against
    // the now-disconnected socket, neither outcome is detectable
    // from "did it not crash."
    {
        QLocalSocket sock;
        sock.connectToServer(fx.sockPath);
        QVERIFY(sock.waitForConnected(2000));
        sock.write(writeLine(
            makeReq(QStringLiteral("subscribe"), 1, QStringLiteral("count"), QStringLiteral("countChanged"))));
        sock.flush();
        readLines(sock, 1); // ack
        sock.disconnectFromServer();
        // Wait for the disconnected signal to actually fire (and
        // therefore for handleClientDisconnected to prune the
        // subscription record). QTRY_VERIFY pumps the event loop
        // until the predicate holds or it times out, replacing the
        // race-prone QTest::qWait(100) that assumed a fixed timing.
        QTRY_VERIFY(sock.state() == QLocalSocket::UnconnectedState);
    }
    // Pump the event loop one more time so the server-side
    // handleClientDisconnected slot, queued from the peer's
    // disconnect, gets a chance to run before the second subscriber
    // observes broadcast fan-out.
    QCoreApplication::processEvents();

    // Second subscriber on a fresh connection. Subscribe + broadcast
    // 3 events + assert all 3 land. If the disconnected first
    // subscriber's record was orphaned in m_subscriptionsBySocket,
    // broadcastEvent would iterate it; the QPointer guard would skip
    // it (no crash), but the iteration cost grows unbounded with
    // every disconnected client. The real evidence of correct
    // pruning is observable from the working second subscriber:
    // it sees exactly the 3 events it subscribed to, no more, no
    // less. (A regression that incorrectly fans dead-subscriber
    // events back onto the live socket would surface as extra
    // events arriving here.)
    QLocalSocket live;
    live.connectToServer(fx.sockPath);
    QVERIFY(live.waitForConnected(2000));
    live.write(
        writeLine(makeReq(QStringLiteral("subscribe"), 1, QStringLiteral("count"), QStringLiteral("countChanged"))));
    live.flush();
    readLines(live, 1); // ack

    for (int i = 1; i <= 3; ++i) {
        QJsonArray args;
        args.append(i);
        router.broadcastEvent(QStringLiteral("count"), QStringLiteral("countChanged"), args);
    }
    const QList<QJsonObject> events = readLines(live, 3);
    QCOMPARE(events.size(), 3);
    QCOMPARE(events.at(0).value(QStringLiteral("args")).toArray().first().toInt(), 1);
    QCOMPARE(events.at(2).value(QStringLiteral("args")).toArray().first().toInt(), 3);

    // No-extra-events check: rather than waiting for a timeout,
    // issue a synchronous `list` request on the live socket. The
    // server serves requests in FIFO order per-socket. If a stray
    // event from the disconnected first subscriber were being
    // misrouted here, it would arrive before the list reply. Read
    // exactly one more frame and assert it's the list reply, never
    // an event.
    live.write(writeLine(makeReq(QStringLiteral("list"), 99)));
    live.flush();
    const QList<QJsonObject> next = readLines(live, 1);
    QCOMPARE(next.size(), 1);
    QCOMPARE(next.first().value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(next.first().value(QStringLiteral("id")).toInt(), 99);
}

void TestPhosphorIpcSubscribe::subscribe_perSocketCapExceeded()
{
    // Pin the MaxSubscriptionsPerSocket=256 cap. After 256 successful
    // subscriptions on a single socket, the 257th must return a
    // MALFORMED_REQUEST diagnostic (the closest existing error code
    // that fits "server-side resource limit hit") and the existing
    // 256 subscriptions must remain live.
    //
    // To produce 257 distinct (target, signal) pairs without
    // declaring 257 signals, we register one CounterTarget under
    // 257 different names. The router allows the same QObject under
    // multiple names; each (name_i, countChanged) is a unique
    // subscription from the dedup-on-(target, signal) perspective.
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    CounterTarget c;
    constexpr int Cap = 256;
    for (int i = 0; i < Cap + 1; ++i) {
        QVERIFY(router.registerTarget(QStringLiteral("count_%1").arg(i), &c));
    }
    QVERIFY(router.start(fx.sockPath));

    QLocalSocket socket;
    socket.connectToServer(fx.sockPath);
    QVERIFY(socket.waitForConnected(2000));

    // Subscribe to the first 256 distinct (target, signal) pairs.
    // Each subscribe issues one round-trip with the ack reply.
    for (int i = 0; i < Cap; ++i) {
        socket.write(writeLine(makeReq(QStringLiteral("subscribe"), i + 1, QStringLiteral("count_%1").arg(i),
                                       QStringLiteral("countChanged"))));
        socket.flush();
        const QList<QJsonObject> ack = readLines(socket, 1);
        QVERIFY2(ack.size() == 1, qPrintable(QStringLiteral("subscribe %1: no ack").arg(i)));
        QCOMPARE(ack.first().value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    }

    // 257th subscription: must fail with MALFORMED_REQUEST.
    socket.write(writeLine(makeReq(QStringLiteral("subscribe"), Cap + 1, QStringLiteral("count_%1").arg(Cap),
                                   QStringLiteral("countChanged"))));
    socket.flush();
    const QList<QJsonObject> resp = readLines(socket, 1);
    QCOMPARE(resp.size(), 1);
    QCOMPARE(resp.first().value(QStringLiteral("type")).toString(), QStringLiteral("error"));
    QCOMPARE(resp.first().value(QStringLiteral("code")).toString(), QStringLiteral("MALFORMED_REQUEST"));

    // The existing 256 subscriptions MUST still be live. Broadcast
    // on the first target/signal pair and confirm an event arrives
    // (its subscriptionId == 1 = the request id of the first
    // subscribe call above). Followed by a sync `list` request so
    // the no-stray-events invariant is held without a wall-clock
    // wait.
    QJsonArray args;
    args.append(1);
    router.broadcastEvent(QStringLiteral("count_0"), QStringLiteral("countChanged"), args);
    socket.write(writeLine(makeReq(QStringLiteral("list"), Cap + 2)));
    socket.flush();
    const QList<QJsonObject> after = readLines(socket, 2);
    QCOMPARE(after.size(), 2);
    QCOMPARE(after.at(0).value(QStringLiteral("type")).toString(), QStringLiteral("event"));
    QCOMPARE(after.at(0).value(QStringLiteral("subscriptionId")).toInt(), 1);
    QCOMPARE(after.at(1).value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(after.at(1).value(QStringLiteral("id")).toInt(), Cap + 2);
}

QTEST_GUILESS_MAIN(TestPhosphorIpcSubscribe)
#include "test_phosphor_ipc_subscribe.moc"
