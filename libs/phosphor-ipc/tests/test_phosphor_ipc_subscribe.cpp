// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Subscribe / broadcast / unsubscribe roundtrip tests. A test target
// QObject exposes one signal-shaped surface; the router subscribes
// a real QLocalSocket client to it; the test triggers
// broadcastEvent and asserts events arrive on the wire in order.

#include <PhosphorIpc/IpcProtocol.h>
#include <PhosphorIpc/IpcRouter.h>

#include <QByteArray>
#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QtCore/qtclasshelpermacros.h>

using namespace PhosphorIpc;

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
    // Declared so subscribe's signal-existence check passes.
    // Subscribers receive events via the router's broadcastEvent
    // path, not via Qt's signal/slot mechanism — so this signal is
    // never actually emitted from the C++ side; broadcastEvent is
    // called directly in the test.
    void countChanged(int v);

private:
    int m_value = 0;
};

// Connect a client and pump until N response lines arrive or
// timeout fires. Returns the parsed objects in arrival order.
// Drains any pre-buffered bytes upfront before entering the
// event loop, because readyRead only fires on NEW arrivals —
// data that landed during an earlier readLines() call on a
// different socket would otherwise sit unread.
QList<QJsonObject> readLines(QLocalSocket& socket, int expectedCount, int timeoutMs = 2000)
{
    QList<QJsonObject> out;
    QByteArray buffer;

    auto drain = [&]() {
        buffer.append(socket.readAll());
        while (true) {
            const int nl = buffer.indexOf('\n');
            if (nl < 0) {
                break;
            }
            QByteArray line = buffer.left(nl);
            buffer.remove(0, nl + 1);
            while (!line.isEmpty() && line.endsWith('\r')) {
                line.chop(1);
            }
            if (line.isEmpty()) {
                continue;
            }
            out.append(QJsonDocument::fromJson(line).object());
        }
    };

    drain();
    if (out.size() >= expectedCount) {
        return out;
    }

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QLocalSocket::readyRead, &loop, [&] {
        drain();
        if (out.size() >= expectedCount) {
            loop.quit();
        }
    });
    timeout.start(timeoutMs);
    loop.exec();
    return out;
}

QJsonObject makeReq(const QString& type, qint64 id, const QString& target = {}, const QString& signal = {},
                    qint64 subId = 0)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), type);
    o.insert(QStringLiteral("id"), static_cast<double>(id));
    if (!target.isEmpty()) {
        o.insert(QStringLiteral("target"), target);
    }
    if (!signal.isEmpty()) {
        o.insert(QStringLiteral("signal"), signal);
    }
    if (subId != 0) {
        o.insert(QStringLiteral("subscriptionId"), static_cast<double>(subId));
    }
    return o;
}

} // namespace

class TestPhosphorIpcSubscribe : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void subscribe_replies_then_streams_events();
    void subscribe_unknownTarget();
    void subscribe_unknownSignal();
    void unsubscribe_stops_events();
    void unsubscribe_unknownId();
    void multipleSubscribers_eachReceiveEvent();
    void disconnect_pruneSubscriptions();
};

void TestPhosphorIpcSubscribe::subscribe_replies_then_streams_events()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sockPath = QDir(dir.path()).filePath(QStringLiteral("test.sock"));

    IpcRouter router;
    CounterTarget c;
    router.registerTarget(QStringLiteral("count"), &c);
    QVERIFY(router.start(sockPath));

    QLocalSocket socket;
    socket.connectToServer(sockPath);
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
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sockPath = QDir(dir.path()).filePath(QStringLiteral("test.sock"));

    IpcRouter router;
    QVERIFY(router.start(sockPath));

    QLocalSocket socket;
    socket.connectToServer(sockPath);
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
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sockPath = QDir(dir.path()).filePath(QStringLiteral("test.sock"));

    IpcRouter router;
    CounterTarget c;
    router.registerTarget(QStringLiteral("count"), &c);
    QVERIFY(router.start(sockPath));

    QLocalSocket socket;
    socket.connectToServer(sockPath);
    QVERIFY(socket.waitForConnected(2000));

    socket.write(
        writeLine(makeReq(QStringLiteral("subscribe"), 1, QStringLiteral("count"), QStringLiteral("ghostSignal"))));
    socket.flush();
    const QList<QJsonObject> resp = readLines(socket, 1);
    QCOMPARE(resp.size(), 1);
    QCOMPARE(resp.first().value(QStringLiteral("type")).toString(), QStringLiteral("error"));
    QCOMPARE(resp.first().value(QStringLiteral("code")).toString(), QStringLiteral("NO_SUCH_SIGNAL"));
}

void TestPhosphorIpcSubscribe::unsubscribe_stops_events()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sockPath = QDir(dir.path()).filePath(QStringLiteral("test.sock"));

    IpcRouter router;
    CounterTarget c;
    router.registerTarget(QStringLiteral("count"), &c);
    QVERIFY(router.start(sockPath));

    QLocalSocket socket;
    socket.connectToServer(sockPath);
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
    router.broadcastEvent(QStringLiteral("count"), QStringLiteral("countChanged"), args);
    // Use readLines with a short deadline + expectedCount=1 so the
    // helper pumps the event loop properly. If anything arrives,
    // out.size() == 1 and we fail; if nothing arrives within the
    // deadline, out is empty (the desired state). The previous
    // qWait+bytesAvailable check was racy because qWait doesn't
    // pump the QLocalSocket's readyRead delivery reliably on
    // Linux.
    const QList<QJsonObject> postUnsub = readLines(socket, 1, 200);
    QCOMPARE(postUnsub.size(), 0);
}

void TestPhosphorIpcSubscribe::unsubscribe_unknownId()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sockPath = QDir(dir.path()).filePath(QStringLiteral("test.sock"));

    IpcRouter router;
    QVERIFY(router.start(sockPath));

    QLocalSocket socket;
    socket.connectToServer(sockPath);
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
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sockPath = QDir(dir.path()).filePath(QStringLiteral("test.sock"));

    IpcRouter router;
    CounterTarget c;
    router.registerTarget(QStringLiteral("count"), &c);
    QVERIFY(router.start(sockPath));

    QLocalSocket a;
    QLocalSocket b;
    a.connectToServer(sockPath);
    b.connectToServer(sockPath);
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
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sockPath = QDir(dir.path()).filePath(QStringLiteral("test.sock"));

    IpcRouter router;
    CounterTarget c;
    router.registerTarget(QStringLiteral("count"), &c);
    QVERIFY(router.start(sockPath));

    // First subscriber connects + subscribes + disconnects. If the
    // router's disconnect path FAILS to prune m_subscriptionsBySocket
    // for the dead socket, broadcastEvent later would either
    // crash writing to a dead QPointer or silently no-op against
    // the now-disconnected socket — neither outcome is detectable
    // from "did it not crash."
    {
        QLocalSocket sock;
        sock.connectToServer(sockPath);
        QVERIFY(sock.waitForConnected(2000));
        sock.write(writeLine(
            makeReq(QStringLiteral("subscribe"), 1, QStringLiteral("count"), QStringLiteral("countChanged"))));
        sock.flush();
        readLines(sock, 1); // ack
        sock.disconnectFromServer();
        QTest::qWait(100); // let router process the disconnect
    }

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
    live.connectToServer(sockPath);
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

    // No-extra-events check: give the loop a brief window to
    // deliver any spurious fan-out from a residual subscription;
    // assert nothing more arrives.
    const QList<QJsonObject> extra = readLines(live, 1, 200);
    QCOMPARE(extra.size(), 0);
}

QTEST_MAIN(TestPhosphorIpcSubscribe)
#include "test_phosphor_ipc_subscribe.moc"
