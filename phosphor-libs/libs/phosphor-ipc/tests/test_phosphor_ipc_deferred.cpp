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

#include <QByteArray>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1Char>
#include <QList>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QTimer>
#include <QtCore/qtclasshelpermacros.h>

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

using namespace PhosphorIpc;
using PhosphorIpcTests::connectClient;
using PhosphorIpcTests::makeCallReq;
using PhosphorIpcTests::makeReq;
using PhosphorIpcTests::readLines;
using PhosphorIpcTests::RouterFixture;
using PhosphorIpcTests::send;

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
    // Runs the test's hook inside a wire call, then reports what the call
    // context says once the hook returned, and defers when asked to.
    Q_INVOKABLE int runHook()
    {
        if (hook) {
            hook();
        }
        connectionAfterHook = connectionId();
        if (deferAfterHook) {
            jobs.push_back(deferReply());
        }
        return 1;
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
    std::function<void()> hook;
    bool deferAfterHook = false;
    quint64 connectionAfterHook = 0;

Q_SIGNALS:
    // The event the jobs send to their caller. Declared so the schema
    // lists it and so a client can also subscribe to it.
    void output(const QString& line); // NOLINT(modernize-use-trailing-return-type)
};

// Frames that arrive within a short wait. For checks that nothing comes:
// make sure first that the router has handled the request, or the check
// passes before anything could come.
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

// Runs a nested event loop for `ms`, as a target method that waits on
// something would.
void spinNestedLoop(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// The router's own end of its only client connection.
QPointer<QLocalSocket> serverSocketOf(IpcRouter& router)
{
    const QList<QLocalSocket*> sockets = router.findChildren<QLocalSocket*>();
    return sockets.size() == 1 ? sockets.first() : nullptr;
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
    void connectionClosed_slotDropsPendingReplies();
    void connectionClosed_onMalformedClose();
    void connectionClosed_slotDestroysRouter();
    void deferredReply_answersOwnConnection();
    void deferredReply_capPerConnection();
    void deferredReply_undefinedResultIsNull();
    void nestedLoop_clientGoes_socketOutlivesDispatch();
    void nestedLoop_otherSlotsSeeNoCall();
    void nestedLoop_laterRequestsWait();
    void nestedLoop_oversizeLineStillCloses();
    void stopInsideCall();
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
    QTRY_COMPARE(jobs.jobs.size(), std::size_t(1));
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
    QTRY_COMPARE(jobs.jobs.size(), std::size_t(1));
    QVERIFY(framesWithin(client).isEmpty());

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
    QTRY_COMPARE(jobs.jobs.size(), std::size_t(3));
    QVERIFY(framesWithin(client).isEmpty());

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
        send(client, makeCallReq(3, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("c")}));
        QTRY_COMPARE(jobs.jobs.size(), std::size_t(3));
        QVERIFY(framesWithin(client).isEmpty());
        client.disconnectFromServer();
    }
    QTRY_COMPARE(closed.count(), 1);
    QCOMPARE(closed.first().first().value<quint64>(), jobs.lastConnection);

    // Dropped, with no warning: the client is gone, nothing went wrong.
    QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
    QVERIFY(jobs.jobs.at(0).isValid());
    QVERIFY(!jobs.jobs.at(0).finish(QStringLiteral("late")));
    QVERIFY(!jobs.jobs.at(1).fail(QStringLiteral("late")));
    QVERIFY(!fx.router.sendEvent(jobs.lastConnection, QStringLiteral("jobs"), QStringLiteral("output"),
                                 QJsonArray{QStringLiteral("late")}));
    // Destroying the reply that was never answered is silent too: it
    // abandons a call whose connection is gone.
    QVERIFY(jobs.jobs.at(2).isValid());
    jobs.jobs.clear();
    QCOMPARE(closed.count(), 1);
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
    QTRY_COMPARE(jobs.jobs.size(), std::size_t(1));
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
    QTRY_COMPARE(jobs.jobs.size(), std::size_t(2));
    QVERIFY(framesWithin(client).isEmpty());
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
    QTRY_COMPARE(jobs.jobs.size(), std::size_t(1));
    QVERIFY(framesWithin(client).isEmpty());
    QVERIFY(!jobs.secondDeferralValid);
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
    QTRY_COMPARE(jobs.jobs.size(), std::size_t(1));
    QVERIFY(framesWithin(client).isEmpty());

    // Assigned to itself, a reply keeps its call: nothing is abandoned.
    // Through a second name, as a self-assignment usually comes about.
    IpcPendingReply& self = jobs.jobs.front();
    IpcPendingReply& alias = jobs.jobs.front();
    self = std::move(alias);
    QVERIFY(self.isValid());
    QVERIFY(framesWithin(client).isEmpty());

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
    jobs.lastConnection = ~quint64(0);
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
    QTRY_COMPARE(jobs.jobs.size(), std::size_t(1));
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

    // ~1 MiB per event, and the client never reads. Nothing is flushed
    // without a turn of the event loop, so the router's write queue grows
    // with each event until it passes 16 MiB.
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
    QTRY_COMPARE(jobs.jobs.size(), std::size_t(1));
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
    send(held, makeCallReq(2, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("x")}));
    send(held, makeCallReq(3, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("y")}));
    QTRY_COMPARE(doomedJobs.jobs.size(), std::size_t(2));
    doomed.reset();
    QCOMPARE(announced, QList<quint64>{heldId});
    QTRY_COMPARE(held.state(), QLocalSocket::UnconnectedState);

    // A pending reply that outlives its router drops its answer, and one
    // destroyed unanswered abandons nothing, both without a warning.
    QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
    QVERIFY(!doomedJobs.jobs.front().finish(1));
    QVERIFY(!doomedJobs.jobs.front().isValid());
    QVERIFY(doomedJobs.jobs.back().isValid()); // still holds its call
    doomedJobs.jobs.clear();
}

void TestPhosphorIpcDeferred::connectionClosed_slotDropsPendingReplies()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));

    // What a target does with a client's work when the client goes: drop
    // its pending replies right in the slot, with no warning.
    int slotRuns = 0;
    QObject::connect(&fx.router, &IpcRouter::connectionClosed, &jobs, [&jobs, &slotRuns](quint64 closedId) {
        ++slotRuns;
        std::erase_if(jobs.jobs, [closedId](const IpcPendingReply& reply) {
            return reply.connectionId() == closedId;
        });
    });

    QLocalSocket client;
    QVERIFY(connectClient(client, fx.sockPath));
    send(client, makeCallReq(1, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("a")}));
    send(client, makeCallReq(2, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("b")}));
    QTRY_COMPARE(jobs.jobs.size(), std::size_t(2));

    QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
    client.disconnectFromServer();
    QTRY_COMPARE(slotRuns, 1);
    QVERIFY(jobs.jobs.empty());
}

void TestPhosphorIpcDeferred::connectionClosed_onMalformedClose()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("consecutive malformed frames; closing")));

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

    // One more garbage line than the router puts up with in a row.
    QByteArray garbage;
    for (int i = 0; i < 17; ++i) {
        garbage += "not json\n";
    }
    client.write(garbage);
    client.flush();
    QTRY_COMPARE(closed.count(), 1);
    QCOMPARE(closed.first().first().value<quint64>(), id);
    QTest::qWait(50);
    QCOMPARE(closed.count(), 1);
}

void TestPhosphorIpcDeferred::connectionClosed_slotDestroysRouter()
{
    // Against the lifetime rule, but the router does not read its own
    // freed memory when a slot does it.
    auto fx = std::make_unique<RouterFixture>();
    QVERIFY(fx->valid());
    JobTarget jobs;
    QVERIFY(fx->router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx->router.start(fx->sockPath));
    int slotRuns = 0;
    QObject::connect(&fx->router, &IpcRouter::connectionClosed, &jobs, [&fx, &slotRuns](quint64) {
        ++slotRuns;
        fx.reset();
    });

    QLocalSocket a;
    QLocalSocket b;
    QVERIFY(connectClient(a, fx->sockPath));
    QVERIFY(connectClient(b, fx->sockPath));
    QVERIFY(connectionIdOf(a, 1) != 0);
    QVERIFY(connectionIdOf(b, 1) != 0);
    // Both closes are booked before the one flush that announces them. The
    // slot destroys the router at the first; the router's destructor
    // announces the second, and the flush that was running stops.
    fx->router.stop();
    QTRY_VERIFY(!fx);
    QTest::qWait(50);
    QCOMPARE(slotRuns, 2);
}

void TestPhosphorIpcDeferred::deferredReply_answersOwnConnection()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));

    QLocalSocket a;
    QLocalSocket b;
    QVERIFY(connectClient(a, fx.sockPath));
    QVERIFY(connectClient(b, fx.sockPath));
    send(a, makeCallReq(1, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("a")}));
    QTRY_COMPARE(jobs.jobs.size(), std::size_t(1));
    send(b, makeCallReq(1, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("b")}));
    QTRY_COMPARE(jobs.jobs.size(), std::size_t(2));
    QVERIFY(jobs.jobs.at(0).connectionId() != jobs.jobs.at(1).connectionId());

    // Answered in the other order: each answer reaches its own caller.
    QVERIFY(jobs.jobs.at(1).finish(QStringLiteral("for b")));
    QVERIFY(jobs.jobs.at(0).finish(QStringLiteral("for a")));
    const QList<QJsonObject> toA = readLines(a, 1);
    const QList<QJsonObject> toB = readLines(b, 1);
    QCOMPARE(toA.size(), 1);
    QCOMPARE(toA.first().value(QStringLiteral("result")).toString(), QStringLiteral("for a"));
    QCOMPARE(toB.size(), 1);
    QCOMPARE(toB.first().value(QStringLiteral("result")).toString(), QStringLiteral("for b"));
    QVERIFY(framesWithin(a).isEmpty());
    QVERIFY(framesWithin(b).isEmpty());
}

void TestPhosphorIpcDeferred::deferredReply_capPerConnection()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));

    constexpr int Cap = IpcRouter::MaxPendingCallsPerConnection;
    QLocalSocket client;
    QVERIFY(connectClient(client, fx.sockPath));
    for (int i = 1; i <= Cap; ++i) {
        send(client, makeCallReq(i, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("j")}));
    }
    QTRY_COMPARE(jobs.jobs.size(), std::size_t(Cap));
    QVERIFY(framesWithin(client).isEmpty());

    // One past the cap fails, and holds nothing to answer later.
    send(client,
         makeCallReq(Cap + 1, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("j")}));
    QList<QJsonObject> frames = readLines(client, 1);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().value(QStringLiteral("id")).toInt(), Cap + 1);
    QCOMPARE(frames.first().value(QStringLiteral("code")).toString(), QString::fromUtf8(ErrorCode::CallFailed));
    QCOMPARE(jobs.jobs.size(), std::size_t(Cap + 1));
    QVERIFY(!jobs.jobs.back().isValid());

    // Another connection has a cap of its own.
    QLocalSocket other;
    QVERIFY(connectClient(other, fx.sockPath));
    send(other, makeCallReq(1, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("o")}));
    QTRY_COMPARE(jobs.jobs.size(), std::size_t(Cap + 2));
    QVERIFY(jobs.jobs.back().isValid());

    // An answered call makes room.
    QVERIFY(jobs.jobs.front().finish(0));
    QCOMPARE(readLines(client, 1).size(), 1);
    send(client,
         makeCallReq(Cap + 2, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("j")}));
    QTRY_COMPARE(jobs.jobs.size(), std::size_t(Cap + 3));
    QVERIFY(jobs.jobs.back().isValid());
    QVERIFY(framesWithin(client).isEmpty());
}

void TestPhosphorIpcDeferred::deferredReply_undefinedResultIsNull()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));

    QLocalSocket client;
    QVERIFY(connectClient(client, fx.sockPath));
    send(client, makeCallReq(1, QStringLiteral("jobs"), QStringLiteral("start"), QJsonArray{QStringLiteral("a")}));
    QTRY_COMPARE(jobs.jobs.size(), std::size_t(1));
    QVERIFY(jobs.jobs.front().finish(QJsonValue(QJsonValue::Undefined)));
    const QList<QJsonObject> frames = readLines(client, 1);
    QCOMPARE(frames.size(), 1);
    QVERIFY(frames.first().contains(QStringLiteral("result")));
    QVERIFY(frames.first().value(QStringLiteral("result")).isNull());
}

void TestPhosphorIpcDeferred::nestedLoop_clientGoes_socketOutlivesDispatch()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));
    QSignalSpy closed(&fx.router, &IpcRouter::connectionClosed);

    QLocalSocket client;
    QVERIFY(connectClient(client, fx.sockPath));
    QVERIFY(connectionIdOf(client, 1) != 0);
    QPointer<QLocalSocket> serverSide = serverSocketOf(fx.router);
    QVERIFY(serverSide);

    // The client goes while the method waits in a nested event loop. The
    // router's end of the connection must live until the dispatch that
    // runs the method is off the stack.
    bool aliveAfterLoop = false;
    jobs.hook = [&] {
        client.abort();
        spinNestedLoop(100);
        aliveAfterLoop = !serverSide.isNull();
    };
    send(client, makeCallReq(2, QStringLiteral("jobs"), QStringLiteral("runHook")));
    QTRY_VERIFY(closed.count() == 1);
    QVERIFY(aliveAfterLoop);
    QTRY_VERIFY(serverSide.isNull());
}

void TestPhosphorIpcDeferred::nestedLoop_otherSlotsSeeNoCall()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));

    QLocalSocket client;
    QVERIFY(connectClient(client, fx.sockPath));
    const quint64 id = connectionIdOf(client, 1);
    QVERIFY(id != 0);

    // A timer slot on the target, run by a nested loop inside the call,
    // is not the call. A deferral there does nothing, and says so.
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("deferReply: called from a nested event loop")));
    quint64 seenBySlot = ~quint64(0);
    bool slotDeferred = true;
    jobs.hook = [&] {
        QEventLoop loop;
        QTimer::singleShot(0, &jobs, [&] {
            seenBySlot = jobs.connectionOutsideCall();
            slotDeferred = jobs.deferOutsideCall();
            loop.quit();
        });
        loop.exec();
    };
    send(client, makeCallReq(2, QStringLiteral("jobs"), QStringLiteral("runHook")));
    const QList<QJsonObject> frames = readLines(client, 1);
    QCOMPARE(frames.size(), 1);
    QCOMPARE(frames.first().value(QStringLiteral("result")).toInt(), 1);
    QCOMPARE(seenBySlot, quint64(0));
    QVERIFY(!slotDeferred);
    // The method itself still sees its call after the loop.
    QCOMPARE(jobs.connectionAfterHook, id);
}

void TestPhosphorIpcDeferred::nestedLoop_laterRequestsWait()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));

    // More requests than the router handles in one go (Batch is the
    // router's detail::MaxFramesPerReadyRead), so the rest run from its
    // queued follow-up, which Qt's own readyRead recursion guard does not
    // cover; the last of them waits in a nested loop while another request
    // comes. That request is answered after it.
    constexpr int Batch = 64;
    QLocalSocket client;
    QVERIFY(connectClient(client, fx.sockPath));
    jobs.hook = [&] {
        send(
            client,
            makeCallReq(Batch + 2, QStringLiteral("jobs"), QStringLiteral("echo"), QJsonArray{QStringLiteral("late")}));
        spinNestedLoop(100);
    };
    QByteArray burst;
    for (int i = 1; i <= Batch; ++i) {
        burst +=
            writeLine(makeCallReq(i, QStringLiteral("jobs"), QStringLiteral("echo"), QJsonArray{QStringLiteral("e")}));
    }
    burst += writeLine(makeCallReq(Batch + 1, QStringLiteral("jobs"), QStringLiteral("runHook")));
    client.write(burst);
    client.flush();

    const QList<QJsonObject> frames = readLines(client, Batch + 2, 5000);
    QCOMPARE(frames.size(), Batch + 2);
    for (int i = 0; i < frames.size(); ++i) {
        QCOMPARE(frames.at(i).value(QStringLiteral("id")).toInt(), i + 1);
    }
}

void TestPhosphorIpcDeferred::nestedLoop_oversizeLineStillCloses()
{
    RouterFixture fx;
    QVERIFY(fx.valid());
    JobTarget jobs;
    QVERIFY(fx.router.registerTarget(QStringLiteral("jobs"), &jobs));
    QVERIFY(fx.router.start(fx.sockPath));
    QSignalSpy closed(&fx.router, &IpcRouter::connectionClosed);

    QLocalSocket client;
    QVERIFY(connectClient(client, fx.sockPath));
    QVERIFY(connectionIdOf(client, 1) != 0);

    // While the method waits in a nested loop, the client sends more than
    // a line may hold, with no newline. The router fills its read buffer
    // there without a pass of its own, and has to notice once the method
    // returns, or the connection stalls for good.
    jobs.hook = [&] {
        const QByteArray flood(1024 * 1024 + 1, 'A');
        client.write(flood);
        for (int i = 0; i < 50 && client.bytesToWrite() > 0; ++i) {
            spinNestedLoop(20);
        }
        spinNestedLoop(100);
    };
    send(client, makeCallReq(2, QStringLiteral("jobs"), QStringLiteral("runHook")));
    const QList<QJsonObject> frames = readLines(client, 2, 5000);
    QCOMPARE(frames.size(), 2);
    QCOMPARE(frames.at(0).value(QStringLiteral("id")).toInt(), 2);
    QCOMPARE(frames.at(1).value(QStringLiteral("code")).toString(), QString::fromUtf8(ErrorCode::MalformedRequest));
    QTRY_COMPARE(closed.count(), 1);
}

void TestPhosphorIpcDeferred::stopInsideCall()
{
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
    QPointer<QLocalSocket> serverSide = serverSocketOf(fx.router);
    QVERIFY(serverSide);

    // The method stops the router, then defers: the call's connection is
    // gone, and its socket lives until the dispatch unwinds.
    bool aliveAfterStop = false;
    jobs.hook = [&] {
        fx.router.stop();
        aliveAfterStop = !serverSide.isNull();
    };
    jobs.deferAfterHook = true;
    send(client, makeCallReq(2, QStringLiteral("jobs"), QStringLiteral("runHook")));
    QTRY_COMPARE(closed.count(), 1);
    QCOMPARE(closed.first().first().value<quint64>(), id);
    QVERIFY(aliveAfterStop);
    QTRY_VERIFY(serverSide.isNull());
    QTRY_COMPARE(client.state(), QLocalSocket::UnconnectedState);
    // The deferral holds the call, with nowhere to answer it.
    QCOMPARE(jobs.jobs.size(), std::size_t(1));
    QVERIFY(jobs.jobs.front().isValid());
    QVERIFY(!jobs.jobs.front().finish(1));
    QVERIFY(readLines(client, 1, 100).isEmpty());
}

QTEST_GUILESS_MAIN(TestPhosphorIpcDeferred)
#include "test_phosphor_ipc_deferred.moc"
