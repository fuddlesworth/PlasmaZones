// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Full socket roundtrip. Spins up an IpcRouter on a temp socket
// path, opens a QLocalSocket client in the same process, sends
// NDJSON requests, and asserts response bytes. Exercises the same
// wire bytes that bin/phosphorctl will produce.

#include "ipctesthelpers.h"

#include <PhosphorIpc/IpcProtocol.h>
#include <PhosphorIpc/IpcRouter.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QLatin1Char>
#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QTest>
#include <QTimer>
#include <QtCore/qtclasshelpermacros.h>

using namespace PhosphorIpc;
using PhosphorIpcTests::makeCallReq;
using PhosphorIpcTests::makeReq;
using PhosphorIpcTests::readLines;
using PhosphorIpcTests::roundtrip;
using PhosphorIpcTests::RouterFixture;

namespace {

class EchoTarget : public QObject
{
    Q_OBJECT
public:
    explicit EchoTarget(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    Q_DISABLE_COPY_MOVE(EchoTarget)

    Q_INVOKABLE QString echo(const QString& s)
    {
        return s;
    }
    Q_INVOKABLE int sum(int a, int b)
    {
        return a + b;
    }
};

class BroadcastTarget : public QObject
{
    Q_OBJECT
public:
    explicit BroadcastTarget(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    Q_DISABLE_COPY_MOVE(BroadcastTarget)

Q_SIGNALS:
    // Existence-of-signal marker for the write-cap test. Never
    // actually emitted from C++; broadcastEvent is called directly
    // from the test body. Same pattern as
    // test_phosphor_ipc_subscribe.cpp::CounterTarget::countChanged.
    void payload(QString blob); // NOLINT(modernize-use-trailing-return-type)
};

// Convenience macro: call PhosphorIpcTests::roundtrip and QFAIL with
// a precise diagnostic on failure. The `return` inside QFAIL must be
// lexically inside the test slot, so this stays a macro (a helper
// function would return-void back to the slot with the failure
// recorded but execution continuing into stale state). Kept thin —
// the actual round-trip lives in ipctesthelpers.h.
#define ROUNDTRIP_OR_FAIL(out, path, req)                                                                              \
    QJsonObject out;                                                                                                   \
    do {                                                                                                               \
        QString rtErr;                                                                                                 \
        const auto rtOpt = roundtrip((path), (req), &rtErr);                                                           \
        if (!rtOpt.has_value()) {                                                                                      \
            QFAIL(qPrintable(QStringLiteral("roundtrip failed: %1").arg(rtErr)));                                      \
        }                                                                                                              \
        out = *rtOpt;                                                                                                  \
    } while (0)

} // namespace

class TestPhosphorIpcE2e : public QObject
{
    Q_OBJECT
public:
    Q_DISABLE_COPY_MOVE(TestPhosphorIpcE2e)
    TestPhosphorIpcE2e() = default;
private Q_SLOTS:
    void roundtrip_call();
    void roundtrip_list();
    void roundtrip_schema();
    void roundtrip_unknownTargetError();
    void roundtrip_malformedRequest();
    void start_failsWhenPathConflict();
    void stop_idempotent();
    void start_afterStop_succeeds();
    void oversizedLineClosesConnection();
    void consecutiveMalformedFramesClosesConnection();
    void pipelinedFramesAllReceiveReplies();
    void peerDisconnectMidRequest();
    void slowSubscriberHitsWriteCap();
};

void TestPhosphorIpcE2e::roundtrip_call()
{
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    EchoTarget echo;
    QVERIFY(router.registerTarget(QStringLiteral("echo"), &echo));
    QVERIFY(router.start(fx.sockPath));

    const QJsonObject req =
        makeCallReq(1, QStringLiteral("echo"), QStringLiteral("echo"), QJsonArray{QStringLiteral("hello")});

    ROUNDTRIP_OR_FAIL(resp, fx.sockPath, req);
    QCOMPARE(resp.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(resp.value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(resp.value(QStringLiteral("result")).toString(), QStringLiteral("hello"));
}

void TestPhosphorIpcE2e::roundtrip_list()
{
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    EchoTarget a;
    EchoTarget b;
    QVERIFY(router.registerTarget(QStringLiteral("a"), &a));
    QVERIFY(router.registerTarget(QStringLiteral("b"), &b));
    QVERIFY(router.start(fx.sockPath));

    const QJsonObject req = makeReq(QStringLiteral("list"), 2);

    ROUNDTRIP_OR_FAIL(resp, fx.sockPath, req);
    QCOMPARE(resp.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    const QJsonArray names = resp.value(QStringLiteral("result")).toArray();
    QCOMPARE(names.size(), 2);
    // Hash order is unspecified; sort before comparing.
    QStringList sorted;
    for (const QJsonValue& v : names) {
        sorted.append(v.toString());
    }
    sorted.sort();
    QCOMPARE(sorted, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
}

void TestPhosphorIpcE2e::roundtrip_schema()
{
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    EchoTarget echo;
    QVERIFY(router.registerTarget(QStringLiteral("echo"), &echo));
    QVERIFY(router.start(fx.sockPath));

    const QJsonObject req = makeReq(QStringLiteral("schema"), 3, QStringLiteral("echo"));

    ROUNDTRIP_OR_FAIL(resp, fx.sockPath, req);
    QCOMPARE(resp.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    const QJsonObject schema = resp.value(QStringLiteral("result")).toObject();
    QCOMPARE(schema.value(QStringLiteral("target")).toString(), QStringLiteral("echo"));
    QCOMPARE(schema.value(QStringLiteral("functions")).toArray().size(), 2);
}

void TestPhosphorIpcE2e::roundtrip_unknownTargetError()
{
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    QVERIFY(router.start(fx.sockPath));

    const QJsonObject req = makeCallReq(4, QStringLiteral("ghost"), QStringLiteral("fn"));

    ROUNDTRIP_OR_FAIL(resp, fx.sockPath, req);
    QCOMPARE(resp.value(QStringLiteral("type")).toString(), QStringLiteral("error"));
    QCOMPARE(resp.value(QStringLiteral("code")).toString(), QStringLiteral("NO_SUCH_TARGET"));
}

void TestPhosphorIpcE2e::roundtrip_malformedRequest()
{
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    QVERIFY(router.start(fx.sockPath));

    // Use the same QEventLoop pattern as roundtrip(); raw byte
    // payload bypasses writeLine.
    QLocalSocket socket;
    QByteArray buffer;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QLocalSocket::readyRead, &loop, [&] {
        buffer.append(socket.readAll());
        if (buffer.contains('\n')) {
            loop.quit();
        }
    });

    socket.connectToServer(fx.sockPath);
    QVERIFY(socket.waitForConnected(2000));
    socket.write("{ not json\n");
    socket.flush();
    timeout.start(2000);
    loop.exec();

    QVERIFY(buffer.contains('\n'));
    const QByteArray line = buffer.left(buffer.indexOf('\n'));
    const QJsonObject resp = QJsonDocument::fromJson(line).object();
    QCOMPARE(resp.value(QStringLiteral("type")).toString(), QStringLiteral("error"));
    QCOMPARE(resp.value(QStringLiteral("code")).toString(), QStringLiteral("MALFORMED_REQUEST"));
    socket.disconnectFromServer();
}

void TestPhosphorIpcE2e::start_failsWhenPathConflict()
{
    // Fixture used for the isolated dir/path only — routerA/routerB are
    // deliberately LOCAL so the test controls which one binds first;
    // fx.router stays idle.
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter routerA;
    EchoTarget echo;
    QVERIFY(routerA.registerTarget(QStringLiteral("echo"), &echo));
    QVERIFY(routerA.start(fx.sockPath));

    IpcRouter routerB;
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("listen\\(\\) failed")));
    QVERIFY(!routerB.start(fx.sockPath));

    // Critical assertion: routerA must still serve traffic. A
    // regression where start() unlinks the live socket file and
    // re-binds (clobbering routerA) would pass the "B fails"
    // check above but break this roundtrip. Send a real call
    // and assert the reply lands.
    const QJsonObject req =
        makeCallReq(1, QStringLiteral("echo"), QStringLiteral("echo"), QJsonArray{QStringLiteral("alive")});

    ROUNDTRIP_OR_FAIL(resp, fx.sockPath, req);
    QCOMPARE(resp.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(resp.value(QStringLiteral("result")).toString(), QStringLiteral("alive"));
}

void TestPhosphorIpcE2e::stop_idempotent()
{
    IpcRouter router;
    router.stop(); // not started, should be safe no-op
    router.stop();
    QVERIFY(router.socketPath().isEmpty());
}

void TestPhosphorIpcE2e::start_afterStop_succeeds()
{
    // The header documents "safe to retry after stop()" but the
    // original test only checked that stop() doesn't crash. Verify
    // the full start -> stop -> start -> serve-traffic cycle so a
    // regression in stop()'s teardown order (e.g. dangling
    // m_subscriptionsBySocket entries blocking re-bind) is caught.
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    EchoTarget echo;
    QVERIFY(router.registerTarget(QStringLiteral("echo"), &echo));
    QVERIFY(router.start(fx.sockPath));
    router.stop();
    QVERIFY(router.socketPath().isEmpty());
    QVERIFY(router.start(fx.sockPath));

    const QJsonObject req =
        makeCallReq(1, QStringLiteral("echo"), QStringLiteral("echo"), QJsonArray{QStringLiteral("after-restart")});

    ROUNDTRIP_OR_FAIL(resp, fx.sockPath, req);
    QCOMPARE(resp.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(resp.value(QStringLiteral("result")).toString(), QStringLiteral("after-restart"));
}

void TestPhosphorIpcE2e::oversizedLineClosesConnection()
{
    // Verify the 1 MiB line-cap path in IpcRouter::handleClientReadyRead.
    // A peer that streams over 1 MiB of bytes without a newline must
    // receive a MALFORMED_REQUEST diagnostic and be force-closed; without
    // this guard a misbehaving client could pin unbounded memory in the
    // QLocalSocket internal buffer until canReadLine() finally returned
    // true.
    //
    // The flood is written in 64 KiB chunks with explicit flush +
    // processEvents between chunks so the test exercises the read-side
    // cap directly rather than depending on the kernel's AF_UNIX send-
    // buffer sizing (a regression that added a write-timeout in the
    // single-write path could break this test for the wrong reason).
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    QVERIFY(router.start(fx.sockPath));

    QLocalSocket socket;
    QByteArray buffer;
    bool disconnected = false;
    QObject::connect(&socket, &QLocalSocket::readyRead, &socket, [&] {
        buffer.append(socket.readAll());
    });
    QObject::connect(&socket, &QLocalSocket::disconnected, &socket, [&] {
        disconnected = true;
        buffer.append(socket.readAll());
    });

    socket.connectToServer(fx.sockPath);
    QVERIFY(socket.waitForConnected(2000));

    // Write 1 MiB + 1 byte without a newline in 64 KiB chunks. The
    // router's 1 MiB cap fires when server-side bytesAvailable
    // reaches MaxLineBytes without a newline. Between chunks we
    // wait for the kernel to actually accept the bytes
    // (waitForBytesWritten) so the test exercises the read-side cap
    // deterministically rather than depending on the kernel send
    // buffer's ability to swallow the whole flood in one syscall.
    constexpr int ChunkSize = 64 * 1024;
    constexpr int TotalBytes = 1024 * 1024 + 1;
    const QByteArray chunk(ChunkSize, 'A');
    int written = 0;
    while (written < TotalBytes && !disconnected) {
        const int toWrite = qMin(ChunkSize, TotalBytes - written);
        socket.write(chunk.constData(), toWrite);
        socket.waitForBytesWritten(2000);
        QCoreApplication::processEvents();
        written += toWrite;
    }

    // Wait for both halves of the router's response: the diagnostic
    // frame on the wire AND the server-driven disconnect. Asserting
    // on "either" hides regressions where the error path silently
    // loses the diagnostic.
    QTRY_VERIFY_WITH_TIMEOUT(disconnected && buffer.contains('\n'), 5000);

    const QByteArray firstLine = buffer.left(buffer.indexOf('\n'));
    const QJsonObject errFrame = QJsonDocument::fromJson(firstLine).object();
    QCOMPARE(errFrame.value(QStringLiteral("type")).toString(), QStringLiteral("error"));
    QCOMPARE(errFrame.value(QStringLiteral("code")).toString(), QStringLiteral("MALFORMED_REQUEST"));
}

void TestPhosphorIpcE2e::consecutiveMalformedFramesClosesConnection()
{
    // Pin the MaxConsecutiveMalformedFrames=16 per-socket cap. A
    // peer that streams 16 consecutive garbage frames must receive
    // 16 MALFORMED_REQUEST diagnostics with the connection still
    // open; the 17th frame triggers force-close.
    //
    // Driven deterministically: write one frame, pump events, drain
    // and assert the error frame arrived, repeat. This pins the cap
    // value at exactly 16 (the previous flake-prone 16..17 tolerance
    // was a symptom of writing all frames at once and racing the
    // waitForBytesWritten flush window on the close iteration).
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    QVERIFY(router.start(fx.sockPath));

    QLocalSocket socket;
    socket.connectToServer(fx.sockPath);
    QVERIFY(socket.waitForConnected(2000));

    // Frames 1..16: each must produce a MALFORMED_REQUEST diagnostic
    // and the connection MUST remain open. A regression that closes
    // earlier (cap < 16) fails the connected-state assertion here.
    constexpr int Cap = 16;
    for (int i = 0; i < Cap; ++i) {
        socket.write("{ garbage line\n");
        socket.flush();
        const QList<QJsonObject> diag = readLines(socket, 1);
        QVERIFY2(diag.size() == 1, qPrintable(QStringLiteral("frame %1: expected one diagnostic").arg(i + 1)));
        QCOMPARE(diag.first().value(QStringLiteral("code")).toString(), QStringLiteral("MALFORMED_REQUEST"));
        QVERIFY2(
            socket.state() == QLocalSocket::ConnectedState,
            qPrintable(QStringLiteral("connection closed prematurely on frame %1 (cap should fire at 17)").arg(i + 1)));
    }

    // Frame 17: the router writes its diagnostic (best-effort) and
    // aborts the connection. The router's writeBeforeAbort flush
    // window means the 17th diagnostic may or may not land before
    // close; we don't assert on its presence. We DO assert that the
    // server closes the connection.
    socket.write("{ garbage line\n");
    socket.flush();
    QTRY_VERIFY_WITH_TIMEOUT(socket.state() == QLocalSocket::UnconnectedState, 5000);
}

void TestPhosphorIpcE2e::pipelinedFramesAllReceiveReplies()
{
    // Pin the MaxFramesPerReadyRead=64 yield-and-reschedule path.
    // A peer that pipelines >64 valid frames in a single readyRead
    // burst must eventually receive replies for ALL of them — the
    // router's queued reschedule must dispatch the leftover frames
    // on a subsequent event-loop iteration. A regression that lost
    // the queued kick would leave frames 65..N stranded.
    //
    // The id-set assertion below pins per-frame correlation: it's
    // not enough that 100 newlines arrive — each request's id MUST
    // be paired to a reply with the same id, and each reply MUST be
    // type="reply". A regression that reordered, duplicated, or
    // dropped+padded the reply stream would pass a bare count-check
    // but fail the id set.
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    QVERIFY(router.start(fx.sockPath));

    QLocalSocket socket;
    socket.connectToServer(fx.sockPath);
    QVERIFY(socket.waitForConnected(2000));

    // Pipeline FrameCount list requests in a single write so they
    // arrive as one readyRead burst on the server side.
    constexpr int FrameCount = 100;
    QByteArray pipeline;
    for (int i = 1; i <= FrameCount; ++i) {
        pipeline.append(writeLine(makeReq(QStringLiteral("list"), i)));
    }
    socket.write(pipeline);
    socket.flush();

    const QList<QJsonObject> replies = readLines(socket, FrameCount, 10000);
    QCOMPARE(replies.size(), FrameCount);

    QSet<int> seenIds;
    for (const QJsonObject& reply : replies) {
        QCOMPARE(reply.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
        const int id = reply.value(QStringLiteral("id")).toInt();
        QVERIFY2(id >= 1 && id <= FrameCount, qPrintable(QStringLiteral("reply id %1 out of range").arg(id)));
        QVERIFY2(!seenIds.contains(id), qPrintable(QStringLiteral("duplicate reply for id %1").arg(id)));
        seenIds.insert(id);
    }
    QCOMPARE(seenIds.size(), FrameCount);
    socket.disconnectFromServer();
}

void TestPhosphorIpcE2e::peerDisconnectMidRequest()
{
    // A peer that writes a partial NDJSON line (no trailing newline)
    // and then aborts must NOT crash, deadlock, or leak resources on
    // the server side; subsequent clients must continue to be served
    // normally.
    //
    // Internal contract being verified: handleClientDisconnected is
    // robust against the "buffer has unconsumed bytes" state, and
    // the per-socket subscription / malformed-counter maps get cleaned
    // up so a fresh accept slot is ready.
    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    EchoTarget echo;
    QVERIFY(router.registerTarget(QStringLiteral("echo"), &echo));
    QVERIFY(router.start(fx.sockPath));

    // Client A: partial JSON, then abort. The partial frame leaves
    // server-side state with bytes buffered but no '\n'. Note: we
    // don't QVERIFY waitForBytesWritten here — flush() may have
    // already pushed the small payload through the kernel
    // synchronously, leaving bytesToWrite at zero and the wait
    // returning false despite the bytes being on the wire.
    {
        QLocalSocket a;
        a.connectToServer(fx.sockPath);
        QVERIFY(a.waitForConnected(2000));
        a.write("{\"type\":\"list\",\"id\":1");
        a.flush();
        a.waitForBytesWritten(2000);
        a.abort();
    }

    // Pump the event loop so the server-side disconnected signal
    // fires and handleClientDisconnected runs.
    QCoreApplication::processEvents();

    // Client B: a normal round-trip must still succeed. If client A
    // had wedged the router (deadlock, freed-pointer access, etc.),
    // this round-trip would hang or fail.
    const QJsonObject req = makeReq(QStringLiteral("list"), 42);
    ROUNDTRIP_OR_FAIL(resp, fx.sockPath, req);
    QCOMPARE(resp.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(resp.value(QStringLiteral("id")).toInt(), 42);
}

void TestPhosphorIpcE2e::slowSubscriberHitsWriteCap()
{
    // Pin the write-side backpressure cap (MaxBytesToWrite = 16 MiB
    // in broadcastEvent). A subscriber that never reads from its
    // socket while the server broadcasts large events must be
    // force-closed once the per-socket bytesToWrite exceeds the cap.
    //
    // We trigger the cap by broadcasting ~1 MiB events without
    // pumping the subscriber's event loop between broadcasts. The
    // kernel AF_UNIX send/recv buffer fills quickly (a few hundred
    // KiB), then Qt accumulates the rest in its userspace write
    // buffer, growing the server's bytesToWrite on the subscriber's
    // socket until the cap fires.
    //
    // We expect the cap test to use the warning that broadcastEvent
    // logs when force-closing the subscriber; suppress it from the
    // test output so the run isn't confused with a real regression.
    QTest::ignoreMessage(
        QtWarningMsg,
        QRegularExpression(QStringLiteral("subscriber on '.*' is .* bytes behind; closing the connection")));

    RouterFixture fx;
    QVERIFY(fx.valid());

    IpcRouter& router = fx.router;
    BroadcastTarget t;
    QVERIFY(router.registerTarget(QStringLiteral("bcast"), &t));
    QVERIFY(router.start(fx.sockPath));

    QLocalSocket subscriber;
    subscriber.connectToServer(fx.sockPath);
    QVERIFY(subscriber.waitForConnected(2000));
    subscriber.write(
        writeLine(makeReq(QStringLiteral("subscribe"), 1, QStringLiteral("bcast"), QStringLiteral("payload"))));
    subscriber.flush();
    const QList<QJsonObject> ack = readLines(subscriber, 1);
    QCOMPARE(ack.size(), 1);
    QCOMPARE(ack.first().value(QStringLiteral("type")).toString(), QStringLiteral("reply"));

    // Construct a ~1 MiB payload per broadcast: 512 KiB string × 2.
    // The JSON-encoded event payload (subscriptionId + args array)
    // adds a few dozen bytes of framing, well under any single-
    // broadcast budget.
    const QString blob(512 * 1024, QLatin1Char('x'));
    QJsonArray big;
    big.append(blob);
    big.append(blob);

    // Broadcast until the router closes the subscriber. Cap at 64
    // iterations to bound the test if the mechanism is broken; the
    // expected close happens around iteration 16-18.
    constexpr int MaxIterations = 64;
    bool closed = false;
    for (int i = 0; i < MaxIterations && !closed; ++i) {
        router.broadcastEvent(QStringLiteral("bcast"), QStringLiteral("payload"), big);
        // Pump once so socket state can propagate, but don't drain
        // the subscriber (that would empty bytesToWrite and the cap
        // would never fire). The router's broadcastEvent and any
        // chained disconnect must propagate via processEvents.
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (subscriber.state() == QLocalSocket::UnconnectedState) {
            closed = true;
        }
    }
    QVERIFY2(closed, "router must close the subscriber once pending writes exceed the 16 MiB cap");
}

QTEST_GUILESS_MAIN(TestPhosphorIpcE2e)
#include "test_phosphor_ipc_e2e.moc"
