// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Full socket roundtrip. Spins up an IpcRouter on a temp socket
// path, opens a QLocalSocket client in the same process, sends
// NDJSON requests, and asserts response bytes. Exercises the same
// wire bytes that bin/phosphorctl will produce.

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

#include <optional>

using namespace PhosphorIpc;

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

// Helper: connect a client, send one request line, await one
// response line, return the parsed object. Uses a QEventLoop with
// QTimer deadline so both the client AND server sides of the
// connection get fully processed, QLocalSocket::waitForReadyRead
// alone doesn't reliably pump the server's accept loop on Linux,
// which leaves the test hanging without the response.
//
// Failures (connect timeout, response timeout) populate `outError`
// and return std::nullopt. Callers QVERIFY the optional and propagate
// the error message via QFAIL so the failure points at the actual
// root cause rather than at a downstream QCOMPARE that just sees an
// empty QJsonObject.
std::optional<QJsonObject> roundtrip(const QString& socketPath, const QJsonObject& request, QString* outError,
                                     int timeoutMs = 2000)
{
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

    socket.connectToServer(socketPath);
    if (!socket.waitForConnected(timeoutMs)) {
        if (outError) {
            *outError = QStringLiteral("connect failed: %1").arg(socket.errorString());
        }
        return std::nullopt;
    }
    socket.write(writeLine(request));
    socket.flush();

    timeout.start(timeoutMs);
    loop.exec();

    if (!buffer.contains('\n')) {
        if (outError) {
            *outError = QStringLiteral("no response within %1ms").arg(timeoutMs);
        }
        return std::nullopt;
    }
    const int nl = buffer.indexOf('\n');
    QByteArray line = buffer.left(nl);
    while (!line.isEmpty() && line.endsWith('\r')) {
        line.chop(1);
    }
    socket.disconnectFromServer();
    return QJsonDocument::fromJson(line).object();
}

// Convenience wrapper that calls QFAIL on roundtrip failure so the
// failure surfaces with a precise message and at the call site
// rather than downstream as `Compared values are different: actual:
// "", expected: "reply"`.
#define ROUNDTRIP_OR_FAIL(out, sockPath, req)                                                                          \
    QJsonObject out;                                                                                                   \
    do {                                                                                                               \
        QString rtErr;                                                                                                 \
        const auto rtOpt = roundtrip((sockPath), (req), &rtErr);                                                       \
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
};

void TestPhosphorIpcE2e::roundtrip_call()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sockPath = QDir(dir.path()).filePath(QStringLiteral("test.sock"));

    IpcRouter router;
    EchoTarget echo;
    QVERIFY(router.registerTarget(QStringLiteral("echo"), &echo));
    QVERIFY(router.start(sockPath));

    QJsonObject req;
    req.insert(QStringLiteral("type"), QStringLiteral("call"));
    req.insert(QStringLiteral("id"), 1);
    req.insert(QStringLiteral("target"), QStringLiteral("echo"));
    req.insert(QStringLiteral("fn"), QStringLiteral("echo"));
    QJsonArray args;
    args.append(QStringLiteral("hello"));
    req.insert(QStringLiteral("args"), args);

    ROUNDTRIP_OR_FAIL(resp, sockPath, req);
    QCOMPARE(resp.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(resp.value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(resp.value(QStringLiteral("result")).toString(), QStringLiteral("hello"));
}

void TestPhosphorIpcE2e::roundtrip_list()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sockPath = QDir(dir.path()).filePath(QStringLiteral("test.sock"));

    IpcRouter router;
    EchoTarget a;
    EchoTarget b;
    QVERIFY(router.registerTarget(QStringLiteral("a"), &a));
    QVERIFY(router.registerTarget(QStringLiteral("b"), &b));
    QVERIFY(router.start(sockPath));

    QJsonObject req;
    req.insert(QStringLiteral("type"), QStringLiteral("list"));
    req.insert(QStringLiteral("id"), 2);

    ROUNDTRIP_OR_FAIL(resp, sockPath, req);
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
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sockPath = QDir(dir.path()).filePath(QStringLiteral("test.sock"));

    IpcRouter router;
    EchoTarget echo;
    QVERIFY(router.registerTarget(QStringLiteral("echo"), &echo));
    QVERIFY(router.start(sockPath));

    QJsonObject req;
    req.insert(QStringLiteral("type"), QStringLiteral("schema"));
    req.insert(QStringLiteral("id"), 3);
    req.insert(QStringLiteral("target"), QStringLiteral("echo"));

    ROUNDTRIP_OR_FAIL(resp, sockPath, req);
    QCOMPARE(resp.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    const QJsonObject schema = resp.value(QStringLiteral("result")).toObject();
    QCOMPARE(schema.value(QStringLiteral("target")).toString(), QStringLiteral("echo"));
    QCOMPARE(schema.value(QStringLiteral("functions")).toArray().size(), 2);
}

void TestPhosphorIpcE2e::roundtrip_unknownTargetError()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sockPath = QDir(dir.path()).filePath(QStringLiteral("test.sock"));

    IpcRouter router;
    QVERIFY(router.start(sockPath));

    QJsonObject req;
    req.insert(QStringLiteral("type"), QStringLiteral("call"));
    req.insert(QStringLiteral("id"), 4);
    req.insert(QStringLiteral("target"), QStringLiteral("ghost"));
    req.insert(QStringLiteral("fn"), QStringLiteral("fn"));

    ROUNDTRIP_OR_FAIL(resp, sockPath, req);
    QCOMPARE(resp.value(QStringLiteral("type")).toString(), QStringLiteral("error"));
    QCOMPARE(resp.value(QStringLiteral("code")).toString(), QStringLiteral("NO_SUCH_TARGET"));
}

void TestPhosphorIpcE2e::roundtrip_malformedRequest()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sockPath = QDir(dir.path()).filePath(QStringLiteral("test.sock"));

    IpcRouter router;
    QVERIFY(router.start(sockPath));

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

    socket.connectToServer(sockPath);
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
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sockPath = QDir(dir.path()).filePath(QStringLiteral("test.sock"));

    IpcRouter routerA;
    EchoTarget echo;
    QVERIFY(routerA.registerTarget(QStringLiteral("echo"), &echo));
    QVERIFY(routerA.start(sockPath));

    IpcRouter routerB;
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("listen\\(\\) failed")));
    QVERIFY(!routerB.start(sockPath));

    // Critical assertion: routerA must still serve traffic. A
    // regression where start() unlinks the live socket file and
    // re-binds (clobbering routerA) would pass the "B fails"
    // check above but break this roundtrip. Send a real call
    // and assert the reply lands.
    QJsonObject req;
    req.insert(QStringLiteral("type"), QStringLiteral("call"));
    req.insert(QStringLiteral("id"), 1);
    req.insert(QStringLiteral("target"), QStringLiteral("echo"));
    req.insert(QStringLiteral("fn"), QStringLiteral("echo"));
    QJsonArray args;
    args.append(QStringLiteral("alive"));
    req.insert(QStringLiteral("args"), args);

    ROUNDTRIP_OR_FAIL(resp, sockPath, req);
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
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sockPath = QDir(dir.path()).filePath(QStringLiteral("test.sock"));

    IpcRouter router;
    EchoTarget echo;
    QVERIFY(router.registerTarget(QStringLiteral("echo"), &echo));
    QVERIFY(router.start(sockPath));
    router.stop();
    QVERIFY(router.socketPath().isEmpty());
    QVERIFY(router.start(sockPath));

    QJsonObject req;
    req.insert(QStringLiteral("type"), QStringLiteral("call"));
    req.insert(QStringLiteral("id"), 1);
    req.insert(QStringLiteral("target"), QStringLiteral("echo"));
    req.insert(QStringLiteral("fn"), QStringLiteral("echo"));
    QJsonArray args;
    args.append(QStringLiteral("after-restart"));
    req.insert(QStringLiteral("args"), args);

    ROUNDTRIP_OR_FAIL(resp, sockPath, req);
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
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sockPath = QDir(dir.path()).filePath(QStringLiteral("test.sock"));

    IpcRouter router;
    QVERIFY(router.start(sockPath));

    QLocalSocket socket;
    QByteArray buffer;
    bool disconnected = false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QLocalSocket::readyRead, &loop, [&] {
        buffer.append(socket.readAll());
        // Quit only once BOTH the diagnostic frame has arrived AND
        // the server has closed the connection (the router contract
        // produces both; "either" hides regressions). Typical Qt
        // ordering on Linux for a peer-side abort is readyRead
        // first, then disconnected, so this gate flips at the
        // disconnected slot below in the common path.
        if (buffer.contains('\n') && disconnected) {
            loop.quit();
        }
    });
    QObject::connect(&socket, &QLocalSocket::disconnected, &loop, [&] {
        disconnected = true;
        // Drain any final bytes the kernel held when the disconnect
        // arrived; readyRead may not re-fire post-close. Quit when
        // we have both halves; if disconnect arrived without the
        // diagnostic (regression we want to catch), the timer fires
        // and the post-loop assertion at QVERIFY2(buffer.contains)
        // pins the failure.
        buffer.append(socket.readAll());
        if (buffer.contains('\n')) {
            loop.quit();
        }
    });

    socket.connectToServer(sockPath);
    QVERIFY(socket.waitForConnected(2000));

    // Write 1 MiB + 1 byte without a newline. The router's 1 MiB cap
    // should trigger MALFORMED_REQUEST + abort().
    QByteArray flood(1024 * 1024 + 1, 'A');
    socket.write(flood);
    socket.flush();

    timeout.start(5000);
    loop.exec();

    // The router contract is: error frame written, brief
    // waitForBytesWritten, then abort(). Both the frame and the
    // disconnect MUST surface when the cap fires; accepting "either
    // or" would hide regressions where the error path silently
    // loses the diagnostic. Pump one more readAll in case bytes
    // landed between the loop's last readyRead and exit.
    buffer.append(socket.readAll());
    QVERIFY2(buffer.contains('\n'),
             "oversized line cap must write the MALFORMED_REQUEST diagnostic frame before closing");
    const QByteArray firstLine = buffer.left(buffer.indexOf('\n'));
    const QJsonObject errFrame = QJsonDocument::fromJson(firstLine).object();
    QCOMPARE(errFrame.value(QStringLiteral("type")).toString(), QStringLiteral("error"));
    QCOMPARE(errFrame.value(QStringLiteral("code")).toString(), QStringLiteral("MALFORMED_REQUEST"));
    QVERIFY2(disconnected, "router must close the connection after the oversize-line diagnostic");
    // socket is already in UnconnectedState (the disconnect
    // assertion above verifies that); no explicit abort() needed
    // before the QLocalSocket destructor runs.
}

void TestPhosphorIpcE2e::consecutiveMalformedFramesClosesConnection()
{
    // Pin the MaxMalformedFrames=16 per-socket cap. A peer that
    // streams 17 consecutive garbage lines must receive 16 error
    // frames (one per malformed input) and then be force-closed on
    // the 17th. The counter-reset-on-well-formed-frame branch in
    // dispatch() is exercised implicitly by other tests that send
    // valid frames after the parser would have surfaced a malformed
    // one — this test focuses on the cap-and-close path only.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sockPath = QDir(dir.path()).filePath(QStringLiteral("test.sock"));

    IpcRouter router;
    QVERIFY(router.start(sockPath));

    QLocalSocket socket;
    QByteArray buffer;
    bool disconnected = false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QLocalSocket::readyRead, &loop, [&] {
        buffer.append(socket.readAll());
    });
    QObject::connect(&socket, &QLocalSocket::disconnected, &loop, [&] {
        disconnected = true;
        buffer.append(socket.readAll());
        loop.quit();
    });

    socket.connectToServer(sockPath);
    QVERIFY(socket.waitForConnected(2000));

    // Write 17 consecutive malformed frames. The router writes an
    // error frame per malformed line (suppressed once the cap fires
    // and the socket is aborted). 16 are tolerated; the 17th
    // triggers close.
    for (int i = 0; i < 17; ++i) {
        socket.write("{ garbage line\n");
    }
    socket.flush();

    timeout.start(5000);
    loop.exec();
    buffer.append(socket.readAll());

    QVERIFY2(disconnected, "router must close the connection after consecutive malformed frames exceed the cap");
    // Count MALFORMED_REQUEST error frames that landed. The cap is
    // 16 — the 17th triggered close, so we should see ≥ 16 error
    // frames (the writeBeforeAbort + flush order may vary slightly
    // on the close iteration). 16 is the strict lower bound the
    // cap guarantees.
    int errorCount = 0;
    int pos = 0;
    while (true) {
        const int nl = buffer.indexOf('\n', pos);
        if (nl < 0) {
            break;
        }
        const QByteArray line = buffer.mid(pos, nl - pos);
        pos = nl + 1;
        const QJsonObject obj = QJsonDocument::fromJson(line).object();
        if (obj.value(QStringLiteral("code")).toString() == QStringLiteral("MALFORMED_REQUEST")) {
            ++errorCount;
        }
    }
    // The strict lower bound the cap guarantees is 16 (router
    // tolerates exactly 16 consecutive malformed frames before
    // closing on the 17th). Upper bound 17 covers the one-write-
    // then-abort race where the 17th's diagnostic frame either
    // does or doesn't land before the close depending on the
    // waitForBytesWritten flush window. A regression that closes
    // earlier (cap < 16) would fail the lower bound; a regression
    // that never closes would fail the disconnected check above.
    QVERIFY2(errorCount >= 16 && errorCount <= 17,
             qPrintable(QStringLiteral("expected 16..17 MALFORMED_REQUEST frames, got %1").arg(errorCount)));
}

void TestPhosphorIpcE2e::pipelinedFramesAllReceiveReplies()
{
    // Pin the MaxFramesPerReadyRead=64 yield-and-reschedule path.
    // A peer that pipelines >64 valid frames in a single readyRead
    // burst must eventually receive replies for ALL of them — the
    // router's queued reschedule must dispatch the leftover frames
    // on a subsequent event-loop iteration. A regression that lost
    // the queued kick would leave frames 65..N stranded.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sockPath = QDir(dir.path()).filePath(QStringLiteral("test.sock"));

    IpcRouter router;
    QVERIFY(router.start(sockPath));

    QLocalSocket socket;
    QByteArray buffer;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    constexpr int FrameCount = 100;
    QObject::connect(&socket, &QLocalSocket::readyRead, &loop, [&] {
        buffer.append(socket.readAll());
        if (buffer.count('\n') >= FrameCount) {
            loop.quit();
        }
    });

    socket.connectToServer(sockPath);
    QVERIFY(socket.waitForConnected(2000));

    // Pipeline FrameCount list requests in a single write so they
    // arrive as one readyRead burst on the server side.
    QByteArray pipeline;
    for (int i = 1; i <= FrameCount; ++i) {
        QJsonObject req;
        req.insert(QStringLiteral("type"), QStringLiteral("list"));
        req.insert(QStringLiteral("id"), i);
        pipeline.append(writeLine(req));
    }
    socket.write(pipeline);
    socket.flush();

    timeout.start(10000);
    loop.exec();

    QCOMPARE(buffer.count('\n'), FrameCount);
    socket.disconnectFromServer();
}

QTEST_MAIN(TestPhosphorIpcE2e)
#include "test_phosphor_ipc_e2e.moc"
