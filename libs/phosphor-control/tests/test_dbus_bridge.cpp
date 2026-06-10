// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QTest>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QThread>
#include <QtDBus/QDBusMessage>

#include "PhosphorControl/DBusBridge.h"

using PhosphorControl::DBusBridge;
using PhosphorControl::DBusEndpoint;

// File-scope mutex guarding the static `g_warnings` pointer used by
// asyncCallOnEmptyEndpointIsNoCrash's installed QtMessageHandler. The
// handler may be invoked from any thread Qt happens to log from
// (including worker threads in other tests run in the same process if
// they ever land in the same TU), so the append + the test-side read
// must be serialised.
static QMutex g_warningsMutex;

class TestDBusBridge : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void endpointRoundTrips()
    {
        DBusEndpoint ep;
        ep.service = QStringLiteral("org.example.svc");
        ep.objectPath = QStringLiteral("/Path");
        ep.interfaceName = QStringLiteral("org.example.Iface");
        ep.syncTimeoutMs = 1234;

        DBusBridge bridge(ep);
        const auto out = bridge.endpoint();
        QCOMPARE(out.service, ep.service);
        QCOMPARE(out.objectPath, ep.objectPath);
        QCOMPARE(out.interfaceName, ep.interfaceName);
        QCOMPARE(out.syncTimeoutMs, 1234);
    }

    void clampsNonPositiveTimeout()
    {
        // Qt 6 patch releases disagree about whether <=0 means block-
        // forever or instant-fail. The bridge clamps to a positive
        // default so syncTimeoutMs is always usable.
        DBusEndpoint ep;
        ep.service = QStringLiteral("org.example.svc");
        ep.objectPath = QStringLiteral("/Path");
        ep.interfaceName = QStringLiteral("org.example.Iface");
        ep.syncTimeoutMs = 0;

        DBusBridge bridge(ep);
        QVERIFY(bridge.endpoint().syncTimeoutMs > 0);

        ep.syncTimeoutMs = -50;
        DBusBridge bridge2(ep);
        QVERIFY(bridge2.endpoint().syncTimeoutMs > 0);
    }

    void rejectsCallOnEmptyService()
    {
        // System-boundary validation: empty service / objectPath /
        // interface / method must return an Error-type QDBusMessage
        // rather than producing a malformed bus call (which would
        // silently fail and swallow the programmer error).
        DBusEndpoint ep;
        ep.service = QString();
        ep.objectPath = QStringLiteral("/Path");
        ep.interfaceName = QStringLiteral("org.example.Iface");
        // The bridge ctor warns about the empty-service endpoint up
        // front (line ~65 in dbusbridge.cpp). Assert + suppress so the
        // warning is part of the test contract instead of CI log noise.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("empty service or objectPath")));
        DBusBridge bridge(ep);

        // call() itself also warns on the rejection path.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("empty service")));
        const auto reply = bridge.call(QStringLiteral("anyMethod"));
        QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
    }

    void rejectsCallOnEmptyMethod()
    {
        DBusEndpoint ep;
        ep.service = QStringLiteral("org.example.svc");
        ep.objectPath = QStringLiteral("/Path");
        ep.interfaceName = QStringLiteral("org.example.Iface");
        DBusBridge bridge(ep);

        const auto reply = bridge.call(QString());
        QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
    }

    void rejectsCallOnEmptyInterfaceArg()
    {
        DBusEndpoint ep;
        ep.service = QStringLiteral("org.example.svc");
        ep.objectPath = QStringLiteral("/Path");
        ep.interfaceName = QStringLiteral("org.example.Default");
        DBusBridge bridge(ep);

        // Override the interface with empty — same rejection path.
        const auto reply = bridge.callOn(QString(), QStringLiteral("m"));
        QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
    }

    void rejectsCallOnEmptyObjectPath()
    {
        // Fourth branch of validateEndpoint — pin the rejection on an
        // endpoint with empty objectPath. A regression that dropped this
        // check would let a malformed bus call past validation.
        DBusEndpoint ep;
        ep.service = QStringLiteral("org.example.svc");
        ep.objectPath = QString();
        ep.interfaceName = QStringLiteral("org.example.Iface");
        DBusBridge bridge(ep);

        const auto reply = bridge.call(QStringLiteral("anyMethod"));
        QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
    }

    void asyncCallOnEmptyEndpointIsNoCrash()
    {
        // asyncCall / asyncCallOn return void so we can't QCOMPARE a reply.
        // The contract is: validation rejects the call before any
        // QDBusPendingCall is enqueued. Capture qWarning output via a
        // scoped message handler so we can pin both the no-crash
        // outcome AND the observable rejection-path side-effect.
        DBusEndpoint ep;
        ep.service = QString();
        ep.objectPath = QStringLiteral("/Path");
        ep.interfaceName = QStringLiteral("org.example.Iface");

        QStringList warnings;
        // Static-instance hand-off: QtMessageHandler is a free
        // function pointer, no captures. Stash the list in a static
        // pointer for the handler's lifetime, restore the previous
        // handler at scope end. All access to `g_warnings` and the
        // QStringList it points at is serialised through
        // g_warningsMutex so the handler is safe to invoke from any
        // thread Qt happens to log from (DBus watchers, etc.).
        static QStringList* g_warnings = nullptr;
        {
            QMutexLocker locker(&g_warningsMutex);
            g_warnings = &warnings;
        }
        QtMessageHandler previousHandler =
            qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& msg) {
                if (type != QtWarningMsg)
                    return;
                QMutexLocker locker(&g_warningsMutex);
                if (g_warnings)
                    g_warnings->append(msg);
            });

        DBusBridge bridge(ep);
        bridge.asyncCall(QStringLiteral("anyMethod"));
        bridge.asyncCallOn(QString(), QStringLiteral("m"));
        bridge.asyncCall(QString());

        qInstallMessageHandler(previousHandler);
        QStringList capturedWarnings;
        {
            QMutexLocker locker(&g_warningsMutex);
            g_warnings = nullptr;
            capturedWarnings = warnings;
        }

        // Bridge validation must surface SOMETHING — empty endpoint
        // / empty method should produce at least one warning. The
        // exact message text is implementation detail; what matters
        // is that the rejection path actually ran (not silently
        // dispatched a malformed bus call).
        QVERIFY2(!capturedWarnings.isEmpty(),
                 "DBusBridge::asyncCall with empty endpoint/method must emit a qWarning — "
                 "silent dispatch would let malformed bus calls leak through.");
    }

    void rejectsCallOnFromForeignThread()
    {
        // QDBusConnection::sessionBus() returns a per-thread connection;
        // a sync call from a non-owning thread silently fails with
        // Disconnected. The bridge must refuse explicitly so the
        // programmer error surfaces in the log. Symmetric with the
        // asyncCallOn cross-thread guard.
        DBusEndpoint ep;
        ep.service = QStringLiteral("org.example.svc");
        ep.objectPath = QStringLiteral("/Path");
        ep.interfaceName = QStringLiteral("org.example.Iface");
        DBusBridge bridge(ep);

        QDBusMessage reply;
        QMutex replyMutex;
        QThread worker;
        // Heap-allocate the context QObject so its destruction is
        // deferred via deleteLater() running on the WORKER thread
        // (the thread it lives on). A stack `QObject context` would
        // be destroyed on the main thread after worker.wait() returns
        // — destruction off-affinity is UB and trips ASan/TSan.
        QObject* context = new QObject;
        context->moveToThread(&worker);
        QObject::connect(&worker, &QThread::started, context, [&, context]() {
            QDBusMessage localReply = bridge.callOn(QStringLiteral("org.example.Other"), QStringLiteral("m"));
            {
                QMutexLocker locker(&replyMutex);
                reply = localReply;
            }
            context->deleteLater();
            worker.quit();
        });
        worker.start();
        // `worker.wait()` synchronises with the worker's exit. If it
        // returns false (timeout), the worker is still running and the
        // reply read below would race the lambda's write — guard with
        // QVERIFY2 + return so the test fails fast instead of racing.
        if (!worker.wait(2000)) {
            worker.requestInterruption();
            worker.wait(500);
            QFAIL("worker thread did not finish within 2 s — cross-thread bridge call wedged");
        }
        // worker.wait() returned true → the worker thread has exited
        // and its lambda's write to `reply` is happens-before-visible
        // to this thread. The mutex is belt-and-braces for static
        // analysers that don't model QThread::wait()'s synchronisation.
        QMutexLocker locker(&replyMutex);
        QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
    }
};

QTEST_MAIN(TestDBusBridge)
#include "test_dbus_bridge.moc"
