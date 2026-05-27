// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QTest>
#include <QThread>
#include <QtDBus/QDBusMessage>

#include "PhosphorSettingsUi/DBusBridge.h"

using PhosphorSettingsUi::DBusBridge;
using PhosphorSettingsUi::DBusEndpoint;

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
        DBusBridge bridge(ep);

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
        // QDBusPendingCall is enqueued. Pin no-crash + no-watcher-leak
        // behaviour — a regression that dropped validation in the async
        // path would dispatch a malformed bus call, but here neither the
        // ctor's empty-endpoint warning nor the bridge's normal flow
        // can be observed deterministically across threads. The crash-
        // free outcome of the call IS the verifiable behaviour.
        DBusEndpoint ep;
        ep.service = QString();
        ep.objectPath = QStringLiteral("/Path");
        ep.interfaceName = QStringLiteral("org.example.Iface");
        DBusBridge bridge(ep);
        bridge.asyncCall(QStringLiteral("anyMethod"));
        bridge.asyncCallOn(QString(), QStringLiteral("m"));
        bridge.asyncCall(QString());
        QVERIFY(true); // no abort, no segfault
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
        QThread worker;
        QObject context;
        context.moveToThread(&worker);
        QObject::connect(&worker, &QThread::started, &context, [&]() {
            reply = bridge.callOn(QStringLiteral("org.example.Other"), QStringLiteral("m"));
            worker.quit();
        });
        worker.start();
        QVERIFY(worker.wait(2000));
        QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
    }
};

QTEST_MAIN(TestDBusBridge)
#include "test_dbus_bridge.moc"
