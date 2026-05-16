// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorDBus/Client.h>
#include <PhosphorDBus/Streaming.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QTest>

using namespace PhosphorDBus;

namespace {
/// A plain struct with no QDBusArgument streaming operators — the negative
/// case for the HasDBusStreaming concept.
struct NotStreamable
{
    int value = 0;
};
} // namespace

// HasDBusStreaming is a compile-time concept; exercise both polarities here.
// `int` has built-in QDBusArgument operators; NotStreamable has none.
static_assert(HasDBusStreaming<int>::value, "int is QDBusArgument-streamable");
static_assert(!HasDBusStreaming<NotStreamable>::value, "NotStreamable has no QDBusArgument operators");

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

    void testFireAndForgetNullParentDoesNotCrash()
    {
        Client client(QDBusConnection::sessionBus(), QStringLiteral("org.example.Service"),
                      QStringLiteral("/example/Object"));
        // A null parent is a contract violation; the call must degrade to a
        // logged warning and return, never dereference the null or allocate
        // an unparented watcher.
        client.fireAndForget(nullptr, QStringLiteral("org.example.Iface"), QStringLiteral("doThing"), {});
        QVERIFY(true);
    }
};

QTEST_GUILESS_MAIN(TestPhosphorDBusClient)
#include "test_phosphordbus_client.moc"
