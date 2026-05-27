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
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QtCore/qtclasshelpermacros.h>

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
// connection get fully processed — QLocalSocket::waitForReadyRead
// alone doesn't reliably pump the server's accept loop on Linux,
// which leaves the test hanging without the response.
QJsonObject roundtrip(const QString& socketPath, const QJsonObject& request, int timeoutMs = 2000)
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
        qWarning("test e2e: connect failed: %s", qPrintable(socket.errorString()));
        return {};
    }
    socket.write(writeLine(request));
    socket.flush();

    timeout.start(timeoutMs);
    loop.exec();

    if (!buffer.contains('\n')) {
        qWarning("test e2e: no response within %dms", timeoutMs);
        return {};
    }
    const int nl = buffer.indexOf('\n');
    QByteArray line = buffer.left(nl);
    while (!line.isEmpty() && line.endsWith('\r')) {
        line.chop(1);
    }
    socket.disconnectFromServer();
    return QJsonDocument::fromJson(line).object();
}

} // namespace

class TestPhosphorIpcE2e : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void roundtrip_call();
    void roundtrip_list();
    void roundtrip_schema();
    void roundtrip_unknownTargetError();
    void roundtrip_malformedRequest();
    void start_failsWhenPathConflict();
    void stop_idempotent();
};

void TestPhosphorIpcE2e::roundtrip_call()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString sockPath = QDir(dir.path()).filePath(QStringLiteral("test.sock"));

    IpcRouter router;
    EchoTarget echo;
    router.registerTarget(QStringLiteral("echo"), &echo);
    QVERIFY(router.start(sockPath));

    QJsonObject req;
    req.insert(QStringLiteral("type"), QStringLiteral("call"));
    req.insert(QStringLiteral("id"), 1);
    req.insert(QStringLiteral("target"), QStringLiteral("echo"));
    req.insert(QStringLiteral("fn"), QStringLiteral("echo"));
    QJsonArray args;
    args.append(QStringLiteral("hello"));
    req.insert(QStringLiteral("args"), args);

    const QJsonObject resp = roundtrip(sockPath, req);
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
    router.registerTarget(QStringLiteral("a"), &a);
    router.registerTarget(QStringLiteral("b"), &b);
    QVERIFY(router.start(sockPath));

    QJsonObject req;
    req.insert(QStringLiteral("type"), QStringLiteral("list"));
    req.insert(QStringLiteral("id"), 2);

    const QJsonObject resp = roundtrip(sockPath, req);
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
    router.registerTarget(QStringLiteral("echo"), &echo);
    QVERIFY(router.start(sockPath));

    QJsonObject req;
    req.insert(QStringLiteral("type"), QStringLiteral("schema"));
    req.insert(QStringLiteral("id"), 3);
    req.insert(QStringLiteral("target"), QStringLiteral("echo"));

    const QJsonObject resp = roundtrip(sockPath, req);
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

    const QJsonObject resp = roundtrip(sockPath, req);
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
    routerA.registerTarget(QStringLiteral("echo"), &echo);
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

    const QJsonObject resp = roundtrip(sockPath, req);
    QCOMPARE(resp.value(QStringLiteral("type")).toString(), QStringLiteral("reply"));
    QCOMPARE(resp.value(QStringLiteral("result")).toString(), QStringLiteral("alive"));
}

void TestPhosphorIpcE2e::stop_idempotent()
{
    IpcRouter router;
    router.stop(); // not started — should be safe no-op
    router.stop();
    QVERIFY(router.socketPath().isEmpty());
}

QTEST_MAIN(TestPhosphorIpcE2e)
#include "test_phosphor_ipc_e2e.moc"
