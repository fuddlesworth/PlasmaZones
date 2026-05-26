// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QTest>
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
        ep.interface = QStringLiteral("org.example.Iface");
        ep.syncTimeoutMs = 1234;

        DBusBridge bridge(ep);
        const auto out = bridge.endpoint();
        QCOMPARE(out.service, ep.service);
        QCOMPARE(out.objectPath, ep.objectPath);
        QCOMPARE(out.interface, ep.interface);
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
        ep.interface = QStringLiteral("org.example.Iface");
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
        ep.interface = QStringLiteral("org.example.Iface");
        DBusBridge bridge(ep);

        const auto reply = bridge.call(QStringLiteral("anyMethod"));
        QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
    }

    void rejectsCallOnEmptyMethod()
    {
        DBusEndpoint ep;
        ep.service = QStringLiteral("org.example.svc");
        ep.objectPath = QStringLiteral("/Path");
        ep.interface = QStringLiteral("org.example.Iface");
        DBusBridge bridge(ep);

        const auto reply = bridge.call(QString());
        QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
    }

    void rejectsCallOnEmptyInterfaceArg()
    {
        DBusEndpoint ep;
        ep.service = QStringLiteral("org.example.svc");
        ep.objectPath = QStringLiteral("/Path");
        ep.interface = QStringLiteral("org.example.Default");
        DBusBridge bridge(ep);

        // Override the interface with empty — same rejection path.
        const auto reply = bridge.callOn(QString(), QStringLiteral("m"));
        QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
    }
};

QTEST_MAIN(TestDBusBridge)
#include "test_dbus_bridge.moc"
