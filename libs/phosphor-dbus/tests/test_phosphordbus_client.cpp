// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorDBus/Client.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QTest>

using namespace PhosphorDBus;

class TestPhosphorDBusClient : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testAccessorsRetainConstruction()
    {
        Client client(QDBusConnection::sessionBus(), QStringLiteral("org.example.Service"),
                      QStringLiteral("/example/Object"));
        QCOMPARE(client.service(), QStringLiteral("org.example.Service"));
        QCOMPARE(client.objectPath(), QStringLiteral("/example/Object"));
    }

    void testCreateCallTargetsConfiguredEndpoint()
    {
        Client client(QDBusConnection::sessionBus(), QStringLiteral("org.example.Service"),
                      QStringLiteral("/example/Object"));
        const QDBusMessage msg = client.createCall(QStringLiteral("org.example.Iface"), QStringLiteral("doThing"),
                                                   {42, QStringLiteral("x")});

        QCOMPARE(msg.type(), QDBusMessage::MethodCallMessage);
        QCOMPARE(msg.service(), QStringLiteral("org.example.Service"));
        QCOMPARE(msg.path(), QStringLiteral("/example/Object"));
        QCOMPARE(msg.interface(), QStringLiteral("org.example.Iface"));
        QCOMPARE(msg.member(), QStringLiteral("doThing"));
        QCOMPARE(msg.arguments().size(), 2);
        QCOMPARE(msg.arguments().at(0).toInt(), 42);
        QCOMPARE(msg.arguments().at(1).toString(), QStringLiteral("x"));
    }

    void testClientIsCopyable()
    {
        Client original(QDBusConnection::sessionBus(), QStringLiteral("org.example.A"), QStringLiteral("/a"));
        Client copy = original;
        QCOMPARE(copy.service(), QStringLiteral("org.example.A"));
        QCOMPARE(copy.objectPath(), QStringLiteral("/a"));
    }
};

QTEST_GUILESS_MAIN(TestPhosphorDBusClient)
#include "test_phosphordbus_client.moc"
