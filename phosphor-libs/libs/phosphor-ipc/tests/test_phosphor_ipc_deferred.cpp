// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Deferred replies, connection identity and events to one connection,
// over a real socket: an IpcRouter on a temp path and QLocalSocket
// clients in the same process.

#include "ipctesthelpers.h"

#include <PhosphorIpc/IpcCallContext.h>
#include <PhosphorIpc/IpcPendingReply.h>
#include <PhosphorIpc/IpcProtocol.h>
#include <PhosphorIpc/IpcRouter.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1Char>
#include <QList>
#include <QLocalSocket>
#include <QObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QtCore/qtclasshelpermacros.h>

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

using namespace PhosphorIpc;
using PhosphorIpcTests::makeCallReq;
using PhosphorIpcTests::makeReq;
using PhosphorIpcTests::readLines;
using PhosphorIpcTests::RouterFixture;

static_assert(!std::is_copy_constructible_v<IpcPendingReply>);
static_assert(!std::is_copy_assignable_v<IpcPendingReply>);
static_assert(std::is_nothrow_move_constructible_v<IpcPendingReply>);
static_assert(std::is_nothrow_move_assignable_v<IpcPendingReply>);

namespace {

// A target whose calls answer later. The test answers them through `jobs`.
class JobTarget : public QObject, protected IpcCallContext
{
    Q_OBJECT
public:
    explicit JobTarget(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    Q_DISABLE_COPY_MOVE(JobTarget)

    // Defers. The return value must never reach the client.
    Q_INVOKABLE int start(const QString& name)
    {
        lastConnection = connectionId();
        lastName = name;
        jobs.push_back(deferReply());
        return -1;
    }
    Q_INVOKABLE QString echo(const QString& text)
    {
        return text;
    }
    Q_INVOKABLE quint64 whoAmI() const
    {
        return connectionId();
    }
    // Defers and drops the pending reply before the method returns.
    Q_INVOKABLE int startAndDrop()
    {
        const IpcPendingReply dropped = deferReply();
        Q_UNUSED(dropped)
        return -1;
    }
    // Defers, then fails the call while it runs.
    Q_INVOKABLE int startAndFail()
    {
        jobs.push_back(deferReply());
        sendErrorReply(QStringLiteral("no such package"));
        return -1;
    }
    // Answers through the pending reply while the method runs, then fails
    // the call: the first answer stands.
    Q_INVOKABLE int answerThenFail(int value)
    {
        IpcPendingReply reply = deferReply();
        reply.finish(value);
        sendErrorReply(QStringLiteral("too late"));
        return -1;
    }
    // Defers twice: the second deferral holds no call.
    Q_INVOKABLE int deferTwice()
    {
        jobs.push_back(deferReply());
        secondDeferralValid = deferReply().isValid();
        return -1;
    }

    // Not wire-callable: what a target sees outside any call.
    bool deferOutsideCall()
    {
        return deferReply().isValid();
    }
    quint64 connectionOutsideCall() const
    {
        return connectionId();
    }

    std::vector<IpcPendingReply> jobs;
    quint64 lastConnection = 0;
    QString lastName;
    bool secondDeferralValid = true;

Q_SIGNALS:
    // The event the jobs send to their caller. Declared so the schema
    // lists it and so a client can also subscribe to it.
    void output(const QString& line); // NOLINT(modernize-use-trailing-return-type)
};

bool connectClient(QLocalSocket& socket, const QString& path)
{
    socket.connectToServer(path);
    return socket.waitForConnected(2000);
}

void send(QLocalSocket& socket, const QJsonObject& request)
{
    socket.write(writeLine(request));
    socket.flush();
}

// Frames that arrive within a short wait. For checks that nothing comes.
QList<QJsonObject> framesWithin(QLocalSocket& socket, int ms = 150)
{
    return readLines(socket, 1, ms);
}

QString typeOf(const QJsonObject& frame)
{
    return frame.value(QStringLiteral("type")).toString();
}

// The connection id of `socket`, as the router sees it, through a call.
quint64 connectionIdOf(QLocalSocket& socket, qint64 requestId)
{
    send(socket, makeCallReq(requestId, QStringLiteral("jobs"), QStringLiteral("whoAmI")));
    const QList<QJsonObject> frames = readLines(socket, 1);
    if (frames.size() != 1 || typeOf(frames.first()) != QLatin1String("reply")) {
        return 0;
    }
    return static_cast<quint64>(frames.first().value(QStringLiteral("result")).toDouble());
}

} // namespace

class TestPhosphorIpcDeferred : public QObject
{
    Q_OBJECT
public:
    Q_DISABLE_COPY_MOVE(TestPhosphorIpcDeferred)
    TestPhosphorIpcDeferred() = default;
private Q_SLOTS:
    void deferredReply_finishedAfterEventLoop();
    void deferredReply_otherCallsAnsweredMeanwhile();
    void deferredReply_secondAnswerIgnored();
    void deferredReply_failSendsError();
    void deferredReply_connectionClosed_droppedSilently();
    void deferredReply_destroyedUnfinished_failsCall();
    void deferredReply_failedDuringCall_errorAnswers();
    void deferredReply_move();
    void deferReply_withoutConnection();
    void sendEvent_reachesOnlyThatConnection_beforeReply();
    void sendEvent_unknownOrClosedConnection();
    void sendEvent_overLimit_closesConnection();
    void connectionClosed_onceWithId_idsNotReused();
    void connectionClosed_onStopAndDestruction();
    void schema_listsTargetedEvent();
};

void TestPhosphorIpcDeferred::deferredReply_finishedAfterEventLoop()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));

    QLocalSocket client;
    QVERIFY(connectClient(client, fx.sockPath));
    send(client, makeCallReq(5, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("pkg")}));

    // The method returned, and the event loop ran: no reply yet.
    QVERIFY(framesWithin(client).isEmpty());
    QCOMPARE(jobs.lastName, QStringLiteral("pkg"));
    QVERIFY(jobs.lastConnection != 0);
    QCOMPARE(jobs.jobs.size(), std::size_t(1));
    QVERIFY(jobs.jobs.front().isValid());
    QCOMPARE(jobs.jobs.front().connectionId(), jobs.lastConnection);

    QVERIFY(jobs.jobs.front().finish(QJsonValue(42)));
    QVERIFY(!jobs.jobs.front().isValid());
    QCOMPARE(jobs.jobs.front().connectionId(), jobs.lastConnection);

    const QList<QJsonObject> frames = readLines(client, 1);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(typeOf(frames.first()), QStringLiteral("reply"));
    QCOMPARE(frames.first().value(QStringLiteral("id")).toInt(), 5);
    QCOMPARE(frames.first().value(QStringLiteral("result")).toInt(), 42);
}

void TestPhosphorIpcDeferred::deferredReply_otherCallsAnsweredMeanwhile()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));

    QLocalSocket client;
    QVERIFY(connectClient(client, fx.sockPath));
    send(client, makeCallReq(1, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("a")}));
    send(client, makeCallReq(2, QStringLiteral("jobs"), QStringLiteral("echo"), QJsonArray{QStringLiteral("hi")}));
    send(client, makeReq(QStringLiteral("list"), 3));

    // The later requests are answered while the first waits.
    QList<QJsonObject> frames = readLines(client, 2);
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(0).value(QStringLiteral("id")).toInt(), 2);
    QCOMPARE(frames.at(0).value(QStringLiteral("result")).toString(), QStringLiteral("hi"));
    QCOMPARE(frames.at(1).value(QStringLiteral("id")).toInt(), 3);

    QCOMPARE(jobs.jobs.size(), std::size_t(1));
    QVERIFY(jobs.jobs.front().finish(QJsonObject{{QStringLiteral("ok"), true}}));
    frames = readLines(client, 1);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(frames.first().value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool(), true);
}

void TestPhosphorIpcDeferred::deferredReply_secondAnswerIgnored()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));

    QLocalSocket client;
    QVERIFY(connectClient(client, fx.sockPath));
    send(client, makeCallReq(1, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("a")}));
    QVERIFY(framesWithin(client).isEmpty());
    QCOMPARE(jobs.jobs.size(), std::size_t(1));

    IpcPendingReply& reply = jobs.jobs.front();
    QVERIFY(reply.finish(QStringLiteral("first")));
    QVERIFY(!reply.finish(QStringLiteral("second")));
    QVERIFY(!reply.fail(QStringLiteral("third")));
    jobs.jobs.clear(); // nothing to abandon either

    const QList<QJsonObject> frames = readLines(client, 2, 300);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().value(QStringLiteral("result")).toString(), QStringLiteral("first"));
}

void TestPhosphorIpcDeferred::deferredReply_failSendsError()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));

    QLocalSocket client;
    QVERIFY(connectClient(client, fx.sockPath));
    send(client, makeCallReq(1, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("a")}));
    send(client, makeCallReq(2, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("b")}));
    send(client, makeCallReq(3, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("c")}));
    QVERIFY(framesWithin(client).isEmpty());
    QCOMPARE(jobs.jobs.size(), std::size_t(3));

    // Answered in another order than asked.
    QVERIFY(jobs.jobs.at(2).fail(QStringLiteral("NO_SPACE"), QStringLiteral("disk full")));
    QVERIFY(jobs.jobs.at(0).fail(QStringLiteral("download failed")));
    QVERIFY(jobs.jobs.at(1).fail(QString(), QStringLiteral("no code given")));

    const QList<QJsonObject> frames = readLines(client, 3);
    QCOMPARE(frames.size(), 3);
    QCOMPARE(typeOf(frames.at(0)), QStringLiteral("error"));
    QCOMPARE(frames.at(0).value(QStringLiteral("id")).toInt(), 3);
    QCOMPARE(frames.at(0).value(QStringLiteral("code")).toString(), QStringLiteral("NO_SPACE"));
    QCOMPARE(frames.at(0).value(QStringLiteral("message")).toString(), QStringLiteral("disk full"));
    QCOMPARE(frames.at(1).value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(frames.at(1).value(QStringLiteral("code")).toString(), QString::fromUtf8(ErrorCode::CallFailed));
    QCOMPARE(frames.at(1).value(QStringLiteral("message")).toString(), QStringLiteral("download failed"));
    QCOMPARE(frames.at(2).value(QStringLiteral("id")).toInt(), 2);
    QCOMPARE(frames.at(2).value(QStringLiteral("code")).toString(), QString::fromUtf8(ErrorCode::CallFailed));
}

void TestPhosphorIpcDeferred::deferredReply_connectionClosed_droppedSilently()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));
    QSignalSpy closed(&fx.router, &IpcRouter::connectionClosed);

    {
        QLocalSocket client;
        QVERIFY(connectClient(client, fx.sockPath));
        send(client, makeCallReq(1, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("a")}));
        send(client, makeCallReq(2, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("b")}));
        QVERIFY(framesWithin(client).isEmpty());
        client.disconnectFromServer();
    }
    QTRY_COMPARE(closed.count(), 1);
    QCOMPARE(closed.first().first().value<quint64>(), jobs.lastConnection);
    QCOMPARE(jobs.jobs.size(), std::size_t(2));

    // Dropped, with no warning: the client is gone, nothing went wrong.
    QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
    QVERIFY(jobs.jobs.at(0).isValid());
    QVERIFY(!jobs.jobs.at(0).finish(QStringLiteral("late")));
    QVERIFY(!jobs.jobs.at(1).fail(QStringLiteral("late")));
    QVERIFY(!fx.router.sendEvent(jobs.lastConnection, QStringLiteral("jobs"), QStringLiteral("output"),
                                 QJsonArray{QStringLiteral("late")}));
    // Destroying an unanswered reply for a closed connection is silent too.
    jobs.jobs.clear();
}

void TestPhosphorIpcDeferred::deferredReply_destroyedUnfinished_failsCall()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));

    QLocalSocket client;
    QVERIFY(connectClient(client, fx.sockPath));

    // Destroyed after the method returned.
    send(client, makeCallReq(1, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("a")}));
    QVERIFY(framesWithin(client).isEmpty());
    jobs.jobs.clear();
    QList<QJsonObject> frames = readLines(client, 1);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(typeOf(frames.first()), QStringLiteral("error"));
    QCOMPARE(frames.first().value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(frames.first().value(QStringLiteral("code")).toString(), QString::fromUtf8(ErrorCode::CallAbandoned));
    QVERIFY(!frames.first().value(QStringLiteral("message")).toString().isEmpty());

    // Dropped inside the method: abandoned, and the return value never
    // goes out.
    send(client, makeCallReq(2, QStringLiteral("jobs"), QStringLiteral("startAndDrop")));
    frames = readLines(client, 2, 300);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().value(QStringLiteral("id")).toInt(), 2);
    QCOMPARE(frames.first().value(QStringLiteral("code")).toString(), QString::fromUtf8(ErrorCode::CallAbandoned));

    // Overwritten by a move: the call it held is abandoned.
    send(client, makeCallReq(3, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("c")}));
    send(client, makeCallReq(4, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("d")}));
    QVERIFY(framesWithin(client).isEmpty());
    QCOMPARE(jobs.jobs.size(), std::size_t(2));
    jobs.jobs.at(0) = std::move(jobs.jobs.at(1));
    frames = readLines(client, 1);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().value(QStringLiteral("id")).toInt(), 3);
    QCOMPARE(frames.first().value(QStringLiteral("code")).toString(), QString::fromUtf8(ErrorCode::CallAbandoned));
    QVERIFY(jobs.jobs.at(0).finish(QStringLiteral("d done")));
    frames = readLines(client, 1);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().value(QStringLiteral("id")).toInt(), 4);
    QCOMPARE(frames.first().value(QStringLiteral("result")).toString(), QStringLiteral("d done"));
}

void TestPhosphorIpcDeferred::deferredReply_failedDuringCall_errorAnswers()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));

    QLocalSocket client;
    QVERIFY(connectClient(client, fx.sockPath));

    // sendErrorReply after deferReply: the error goes out as the method
    // returns, and the pending reply has nothing left to answer.
    send(client, makeCallReq(1, QStringLiteral("jobs"), QStringLiteral("startAndFail")));
    QList<QJsonObject> frames = readLines(client, 1);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(frames.first().value(QStringLiteral("code")).toString(), QString::fromUtf8(ErrorCode::CallFailed));
    QCOMPARE(frames.first().value(QStringLiteral("message")).toString(), QStringLiteral("no such package"));
    QCOMPARE(jobs.jobs.size(), std::size_t(1));
    QVERIFY(!jobs.jobs.front().finish(1));
    jobs.jobs.clear();
    QVERIFY(framesWithin(client).isEmpty());

    // The pending reply answered first: the later sendErrorReply is dropped.
    send(client, makeCallReq(2, QStringLiteral("jobs"), QStringLiteral("answerThenFail"), QJsonArray{7}));
    frames = readLines(client, 2, 300);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(typeOf(frames.first()), QStringLiteral("reply"));
    QCOMPARE(frames.first().value(QStringLiteral("id")).toInt(), 2);
    QCOMPARE(frames.first().value(QStringLiteral("result")).toInt(), 7);

    // A second deferral in one call holds nothing; the first still answers.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("deferred already")));
    send(client, makeCallReq(3, QStringLiteral("jobs"), QStringLiteral("deferTwice")));
    QVERIFY(framesWithin(client).isEmpty());
    QVERIFY(!jobs.secondDeferralValid);
    QCOMPARE(jobs.jobs.size(), std::size_t(1));
    QVERIFY(jobs.jobs.front().finish(3));
    frames = readLines(client, 1);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().value(QStringLiteral("id")).toInt(), 3);
}

void TestPhosphorIpcDeferred::deferredReply_move()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));

    QLocalSocket client;
    QVERIFY(connectClient(client, fx.sockPath));
    send(client, makeCallReq(9, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("a")}));
    QVERIFY(framesWithin(client).isEmpty());
    QCOMPARE(jobs.jobs.size(), std::size_t(1));

    IpcPendingReply moved(std::move(jobs.jobs.front()));
    QVERIFY(!jobs.jobs.front().isValid()); // NOLINT(bugprone-use-after-move)
    QCOMPARE(jobs.jobs.front().connectionId(), quint64(0));
    QVERIFY(!jobs.jobs.front().finish(1));
    jobs.jobs.clear(); // a moved-from reply abandons nothing
    QVERIFY(framesWithin(client).isEmpty());

    QVERIFY(moved.isValid());
    QCOMPARE(moved.connectionId(), jobs.lastConnection);
    QVERIFY(moved.finish(QStringLiteral("moved")));
    const QList<QJsonObject> frames = readLines(client, 1);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().value(QStringLiteral("id")).toInt(), 9);
    QCOMPARE(frames.first().value(QStringLiteral("result")).toString(), QStringLiteral("moved"));

    // A default-constructed reply holds nothing.
    IpcPendingReply empty;
    QVERIFY(!empty.isValid());
    QCOMPARE(empty.connectionId(), quint64(0));
    QVERIFY(!empty.finish());
    QVERIFY(!empty.fail(QStringLiteral("x")));
}

void TestPhosphorIpcDeferred::deferReply_withoutConnection()
{
    IpcRouter router;
    JobTarget jobs;
    QVERIFY(router.registerTarget(QStringLiteral("jobs"), &jobs));

    // Outside any call.
    QVERIFY(!jobs.deferOutsideCall());
    QCOMPARE(jobs.connectionOutsideCall(), quint64(0));

    // A direct invoke has no connection and no client to answer later.
    IpcRouter::InvokeOutcome outcome = IpcRouter::InvokeOutcome::Ok;
    QString message;
    const QVariant result =
        router.invoke(QStringLiteral("jobs"), QStringLiteral("start"), {QStringLiteral("x")}, &outcome, &message);
    QCOMPARE(outcome, IpcRouter::InvokeOutcome::Deferred);
    QVERIFY(!result.isValid());
    QCOMPARE(jobs.lastConnection, quint64(0));
    QCOMPARE(jobs.jobs.size(), std::size_t(1));
    QVERIFY(!jobs.jobs.front().isValid());
    QVERIFY(!jobs.jobs.front().finish(1));

    // Deferred, then failed: the failure is the outcome.
    router.invoke(QStringLiteral("jobs"), QStringLiteral("startAndFail"), {}, &outcome, &message);
    QCOMPARE(outcome, IpcRouter::InvokeOutcome::CallFailed);
    QCOMPARE(message, QStringLiteral("no such package"));

    // Calls that do not defer are unchanged.
    QCOMPARE(router.invoke(QStringLiteral("jobs"), QStringLiteral("echo"), {QStringLiteral("e")}, &outcome).toString(),
             QStringLiteral("e"));
    QCOMPARE(outcome, IpcRouter::InvokeOutcome::Ok);
    QCOMPARE(router.invoke(QStringLiteral("jobs"), QStringLiteral("whoAmI"), {}, &outcome).toULongLong(), 0ULL);
}

void TestPhosphorIpcDeferred::sendEvent_reachesOnlyThatConnection_beforeReply()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));

    // `other` subscribed to the event, so it would get a broadcast of it.
    QLocalSocket other;
    QVERIFY(connectClient(other, fx.sockPath));
    send(other, makeReq(QStringLiteral("subscribe"), 1, QStringLiteral("jobs"), QStringLiteral("output")));
    QCOMPARE(readLines(other, 1).size(), 1);

    QLocalSocket caller;
    QVERIFY(connectClient(caller, fx.sockPath));
    send(caller, makeCallReq(7, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("pkg")}));
    QVERIFY(framesWithin(caller).isEmpty());
    const quint64 id = jobs.lastConnection;
    QVERIFY(id != 0);

    QVERIFY(fx.router.sendEvent(id, QStringLiteral("jobs"), QStringLiteral("output"),
                                QJsonArray{QStringLiteral("line 1")}));
    QList<QJsonObject> frames = readLines(caller, 1);
    QCOMPARE(frames.size(), 1);
    // Written last thing before the reply, in one go with it.
    QVERIFY(fx.router.sendEvent(id, QStringLiteral("jobs"), QStringLiteral("output"),
                                QJsonArray{QStringLiteral("line 2")}));
    QVERIFY(jobs.jobs.front().finish(0));
    frames += readLines(caller, 2);
    QCOMPARE(frames.size(), 3);

    for (int i = 0; i < 2; ++i) {
        const QJsonObject& event = frames.at(i);
        QCOMPARE(typeOf(event), QStringLiteral("event"));
        QCOMPARE(event.value(QStringLiteral("target")).toString(), QStringLiteral("jobs"));
        QCOMPARE(event.value(QStringLiteral("signal")).toString(), QStringLiteral("output"));
        QVERIFY(!event.contains(QStringLiteral("subscriptionId")));
        QCOMPARE(event.value(QStringLiteral("args")).toArray(), QJsonArray{QStringLiteral("line %1").arg(i + 1)});
    }
    QCOMPARE(typeOf(frames.at(2)), QStringLiteral("reply"));
    QCOMPARE(frames.at(2).value(QStringLiteral("id")).toInt(), 7);

    // The subscriber got none of it.
    QVERIFY(framesWithin(other).isEmpty());
    // A broadcast still reaches it, and not the caller, which did not
    // subscribe.
    fx.router.broadcastEvent(QStringLiteral("jobs"), QStringLiteral("output"), QJsonArray{QStringLiteral("all")});
    frames = readLines(other, 1);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().value(QStringLiteral("subscriptionId")).toInt(), 1);
    QVERIFY(framesWithin(caller).isEmpty());
}

void TestPhosphorIpcDeferred::sendEvent_unknownOrClosedConnection()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));
    QSignalSpy closed(&fx.router, &IpcRouter::connectionClosed);

    const QJsonArray args{QStringLiteral("x")};
    QVERIFY(!fx.router.sendEvent(0, QStringLiteral("jobs"), QStringLiteral("output"), args));
    QVERIFY(!fx.router.sendEvent(~quint64(0), QStringLiteral("jobs"), QStringLiteral("output"), args));

    QLocalSocket client;
    QVERIFY(connectClient(client, fx.sockPath));
    const quint64 id = connectionIdOf(client, 1);
    QVERIFY(id != 0);
    QVERIFY(!fx.router.sendEvent(id, QString(), QStringLiteral("output"), args));
    QVERIFY(!fx.router.sendEvent(id, QStringLiteral("jobs"), QString(), args));
    QVERIFY(framesWithin(client).isEmpty());
    QVERIFY(fx.router.sendEvent(id, QStringLiteral("jobs"), QStringLiteral("output"), args));
    QCOMPARE(readLines(client, 1).size(), 1);

    client.disconnectFromServer();
    QTRY_COMPARE(closed.count(), 1);
    QVERIFY(!fx.router.sendEvent(id, QStringLiteral("jobs"), QStringLiteral("output"), args));
}

void TestPhosphorIpcDeferred::sendEvent_overLimit_closesConnection()
{
    QTest::ignoreMessage(
        QtWarningMsg,
        QRegularExpression(QStringLiteral("connection \\d+ is \\d+ bytes behind; closing the connection")));

    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));
    QSignalSpy closed(&fx.router, &IpcRouter::connectionClosed);

    QLocalSocket client;
    QVERIFY(connectClient(client, fx.sockPath));
    const quint64 id = connectionIdOf(client, 1);
    QVERIFY(id != 0);

    // ~1 MiB per event, and the client never reads: the kernel buffer
    // fills, then the router's write queue, until it passes 16 MiB.
    const QJsonArray big{QString(1024 * 1024, QLatin1Char('x'))};
    constexpr int MaxEvents = 64;
    int sent = 0;
    bool refused = false;
    for (; sent < MaxEvents; ++sent) {
        if (!fx.router.sendEvent(id, QStringLiteral("jobs"), QStringLiteral("output"), big)) {
            refused = true;
            break;
        }
    }
    QVERIFY2(refused, "sendEvent must close a connection more than 16 MiB behind");
    QVERIFY(sent >= 16);
    // Not from inside sendEvent: from the event loop.
    QCOMPARE(closed.count(), 0);
    QTRY_COMPARE(closed.count(), 1);
    QCOMPARE(closed.first().first().value<quint64>(), id);
    QVERIFY(!fx.router.sendEvent(id, QStringLiteral("jobs"), QStringLiteral("output"), QJsonArray()));
}

void TestPhosphorIpcDeferred::connectionClosed_onceWithId_idsNotReused()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));
    QSignalSpy closed(&fx.router, &IpcRouter::connectionClosed);

    auto a = std::make_unique<QLocalSocket>();
    QVERIFY(connectClient(*a, fx.sockPath));
    const quint64 idA = connectionIdOf(*a, 1);
    QLocalSocket b;
    QVERIFY(connectClient(b, fx.sockPath));
    const quint64 idB = connectionIdOf(b, 1);
    QVERIFY(idA != 0);
    QVERIFY(idB != 0);
    QVERIFY(idA != idB);
    // One connection, one id, on every call.
    QCOMPARE(connectionIdOf(*a, 2), idA);

    a->disconnectFromServer();
    a.reset();
    QTRY_COMPARE(closed.count(), 1);
    QCOMPARE(closed.first().first().value<quint64>(), idA);
    QTest::qWait(100);
    QCOMPARE(closed.count(), 1);

    // A new connection never gets a closed one's id.
    QLocalSocket c;
    QVERIFY(connectClient(c, fx.sockPath));
    const quint64 idC = connectionIdOf(c, 1);
    QVERIFY(idC != 0);
    QVERIFY(idC != idA);
    QVERIFY(idC != idB);

    // Nor does a connection to another router in the process.
    RouterFixture other;
    QVERIFY(other.valid());
    JobTarget otherJobs;
    QVERIFY(other.router.registerTarget(QStringLiteral("jobs"), &otherJobs));
    QVERIFY(other.router.start(other.sockPath));
    QLocalSocket d;
    QVERIFY(connectClient(d, other.sockPath));
    const quint64 idD = connectionIdOf(d, 1);
    QVERIFY(idD != 0);
    QVERIFY(idD != idA);
    QVERIFY(idD != idB);
    QVERIFY(idD != idC);
    // The other router's connection is not this router's to send to.
    QVERIFY(!fx.router.sendEvent(idD, QStringLiteral("jobs"), QStringLiteral("output"), QJsonArray()));
}

void TestPhosphorIpcDeferred::connectionClosed_onStopAndDestruction()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));
    QSignalSpy closed(&fx.router, &IpcRouter::connectionClosed);

    QLocalSocket client;
    QVERIFY(connectClient(client, fx.sockPath));
    send(client, makeCallReq(1, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("a")}));
    QVERIFY(framesWithin(client).isEmpty());
    const quint64 id = jobs.lastConnection;

    fx.router.stop();
    QCOMPARE(closed.count(), 0); // from the event loop
    QTRY_COMPARE(closed.count(), 1);
    QCOMPARE(closed.first().first().value<quint64>(), id);
    QVERIFY(!jobs.jobs.front().finish(1));
    QTest::qWait(50);
    QCOMPARE(closed.count(), 1);

    // A router that is destroyed tells about the connections it closes
    // before it is gone, since its turn of the event loop never comes.
    JobTarget doomedJobs;
    QLocalSocket held;
    QList<quint64> announced;
    auto doomed = std::make_unique<RouterFixture>();
    QVERIFY(doomed->valid());
    QVERIFY(doomed->router.registerTarget(QStringLiteral("jobs"), &doomedJobs));
    QVERIFY(doomed->router.start(doomed->sockPath));
    QObject::connect(&doomed->router, &IpcRouter::connectionClosed, &doomed->router, [&announced](quint64 closedId) {
        announced.append(closedId);
    });
    QVERIFY(connectClient(held, doomed->sockPath));
    const quint64 heldId = connectionIdOf(held, 1);
    QVERIFY(heldId != 0);
    doomed.reset();
    QCOMPARE(announced, QList<quint64>{heldId});
    QTRY_COMPARE(held.state(), QLocalSocket::UnconnectedState);
}

void TestPhosphorIpcDeferred::schema_listsTargetedEvent()
{
    IpcRouter router;
    JobTarget jobs;
    QVERIFY(router.registerTarget(QStringLiteral("jobs"), &jobs));
    const QJsonArray signalList = router.schemaFor(QStringLiteral("jobs")).value(QStringLiteral("signals")).toArray();
    bool found = false;
    for (const QJsonValue& entry : signalList) {
        found = found || entry.toObject().value(QStringLiteral("name")).toString() == QLatin1String("output");
    }
    QVERIFY(found);
}

QTEST_GUILESS_MAIN(TestPhosphorIpcDeferred)
#include "test_phosphor_ipc_deferred.moc"
