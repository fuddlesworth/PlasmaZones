// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Wire test for phosphor-service-notifications (milestone 8). The C++ smoke
// test calls the server's slots directly, which cannot exercise the image-data
// hint decode: an in-process QDBusArgument is in marshalling mode and cannot be
// read back. This test sends real D-Bus method calls across a peer-to-peer
// connection, so arguments go through genuine marshalling / demarshalling and
// the server sees a proper demarshalling QDBusArgument, exactly as it would from
// a real notifying app. It also validates that the generated adaptor forwards
// the spec methods correctly over the wire.
//
// Calls are issued asynchronously and the event loop is pumped while they are in
// flight: client and server share this thread, so a blocking call() would
// deadlock (the server could never process the request). The peer-to-peer
// transport needs no bus daemon and never touches the real
// org.freedesktop.Notifications name.

#include <PhosphorServiceNotifications/Notification.h>
#include <PhosphorServiceNotifications/NotificationServer.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusServer>
#include <QDeadlineTimer>
#include <QImage>
#include <QStringList>
#include <QTest>
#include <QVariantMap>

#include <memory>

using namespace PhosphorServiceNotifications;

namespace {
// Mirror of the spec image-data struct (iiibiiay). Registered as a D-Bus
// metatype so the CLIENT can marshal it into the a{sv} hints; the server side
// (the library) deliberately does NOT register it, so it arrives there as a raw
// QDBusArgument that the lib's decoder reads.
struct ImageData
{
    int width = 0;
    int height = 0;
    int rowstride = 0;
    bool hasAlpha = false;
    int bitsPerSample = 0;
    int channels = 0;
    QByteArray data;
};

QDBusArgument& operator<<(QDBusArgument& arg, const ImageData& d)
{
    arg.beginStructure();
    arg << d.width << d.height << d.rowstride << d.hasAlpha << d.bitsPerSample << d.channels << d.data;
    arg.endStructure();
    return arg;
}
const QDBusArgument& operator>>(const QDBusArgument& arg, ImageData& d)
{
    arg.beginStructure();
    arg >> d.width >> d.height >> d.rowstride >> d.hasAlpha >> d.bitsPerSample >> d.channels >> d.data;
    arg.endStructure();
    return arg;
}
} // namespace

Q_DECLARE_METATYPE(ImageData)

class NotificationsWireTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void getServerInformationOverWire();
    void notifyDecodesImageDataStructOverWire();
    void notifyRejectsOversizedImageData();
    void closeNotificationOverWire();

private:
    QDBusMessage callSync(const QDBusMessage& message);
    QDBusMessage notifyMessage() const;
    Notification* findById(uint id) const;

    std::unique_ptr<QDBusServer> m_dbusServer;
    QDBusConnection m_serverSide{QStringLiteral("invalid")};
    QDBusConnection m_clientSide{QStringLiteral("invalid")};
    std::unique_ptr<NotificationServer> m_server;
};

void NotificationsWireTest::initTestCase()
{
    qDBusRegisterMetaType<ImageData>();

    m_dbusServer = std::make_unique<QDBusServer>(QStringLiteral("unix:tmpdir=/tmp"));
    m_dbusServer->setAnonymousAuthenticationAllowed(true);
    connect(m_dbusServer.get(), &QDBusServer::newConnection, this, [this](const QDBusConnection& connection) {
        m_serverSide = connection;
    });

    m_clientSide = QDBusConnection::connectToPeer(m_dbusServer->address(), QStringLiteral("pz-notif-wire"));
    QDeadlineTimer deadline(3000);
    while ((!m_clientSide.isConnected() || !m_serverSide.isConnected()) && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    QVERIFY2(m_clientSide.isConnected() && m_serverSide.isConnected(), "peer-to-peer connection did not establish");

    // One server shared by the whole suite, registered on the peer's server end
    // so the client's method calls route to it through real marshalling. Tests
    // must stay count-agnostic (locate notifications by the returned id, not by
    // notifications().size()) since state accumulates across cases.
    m_server = std::make_unique<NotificationServer>(m_serverSide, NotificationServer::serviceName());
}

QDBusMessage NotificationsWireTest::callSync(const QDBusMessage& message)
{
    // Async + pump: a blocking call() would deadlock since the server dispatches
    // on this same thread.
    QDBusPendingCall pending = m_clientSide.asyncCall(message);
    QDeadlineTimer deadline(5000);
    while (!pending.isFinished() && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return pending.reply();
}

QDBusMessage NotificationsWireTest::notifyMessage() const
{
    // Empty service: on a peer connection there are no bus names, so the call
    // routes to the peer and dispatches by object path. serviceName() doubles
    // as the INTERFACE argument here — valid only because the notifications
    // spec uses one string ("org.freedesktop.Notifications") for both; a
    // spec-rev that splits them would need a dedicated interfaceName().
    return QDBusMessage::createMethodCall(QString(), NotificationServer::objectPath(),
                                          NotificationServer::serviceName(), QStringLiteral("Notify"));
}

Notification* NotificationsWireTest::findById(uint id) const
{
    const auto live = m_server->notifications();
    for (auto* n : live) {
        if (n->id() == id)
            return n;
    }
    return nullptr;
}

void NotificationsWireTest::getServerInformationOverWire()
{
    QDBusMessage call =
        QDBusMessage::createMethodCall(QString(), NotificationServer::objectPath(), NotificationServer::serviceName(),
                                       QStringLiteral("GetServerInformation"));
    const QDBusMessage reply = callSync(call);
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage); // adaptor forwarded the call
    const QVariantList args = reply.arguments();
    QCOMPARE(args.size(), 4);
    QCOMPARE(args.at(0).toString(), QStringLiteral("Phosphor"));
    QCOMPARE(args.at(3).toString(), QStringLiteral("1.3"));
}

void NotificationsWireTest::notifyDecodesImageDataStructOverWire()
{
    // A 2x2 RGBA image: 4 channels, 8 bits, rowstride = 2*4 = 8, 16 bytes.
    ImageData image;
    image.width = 2;
    image.height = 2;
    image.channels = 4;
    image.bitsPerSample = 8;
    image.rowstride = image.width * image.channels;
    image.hasAlpha = true;
    image.data = QByteArray(image.rowstride * image.height, '\x7f');

    QVariantMap hints;
    hints.insert(QStringLiteral("image-data"), QVariant::fromValue(image));

    QDBusMessage call = notifyMessage();
    call << QStringLiteral("app") << 0u << QString() << QStringLiteral("pic") << QString() << QStringList() << hints
         << 0;

    const QDBusMessage reply = callSync(call);
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    const uint id = reply.arguments().value(0).toUInt();
    QVERIFY(id != 0);

    // The (iiibiiay) struct arrived as a demarshalling QDBusArgument and was
    // decoded to a QImage of the declared size.
    Notification* n = findById(id);
    QVERIFY(n != nullptr);
    QVERIFY(n->hasImage());
    QCOMPARE(n->image().size(), QSize(2, 2));
}

void NotificationsWireTest::notifyRejectsOversizedImageData()
{
    // A width beyond the decoder's dimension cap, declared with deliberately
    // short data, must be rejected (no over-read, no giant allocation) rather
    // than decoded. The notification still lands; it simply has no image. This
    // pins the boundary check against attacker-controlled (width/rowstride/data)
    // image structs.
    ImageData image;
    image.width = 100000; // beyond the decoder's kMaxImageDimension
    image.height = 1;
    image.channels = 4;
    image.bitsPerSample = 8;
    image.rowstride = image.width * image.channels;
    image.hasAlpha = true;
    image.data = QByteArray(16, '\x7f'); // far too small for the declared size

    QVariantMap hints;
    hints.insert(QStringLiteral("image-data"), QVariant::fromValue(image));

    QDBusMessage call = notifyMessage();
    call << QStringLiteral("app") << 0u << QString() << QStringLiteral("bad") << QString() << QStringList() << hints
         << 0;

    const QDBusMessage reply = callSync(call);
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    const uint id = reply.arguments().value(0).toUInt();
    QVERIFY(id != 0);

    Notification* n = findById(id);
    QVERIFY(n != nullptr);
    QVERIFY(!n->hasImage()); // rejected by the bounds check, not over-read
}

void NotificationsWireTest::closeNotificationOverWire()
{
    QDBusMessage notify = notifyMessage();
    notify << QStringLiteral("app") << 0u << QString() << QStringLiteral("x") << QString() << QStringList()
           << QVariantMap() << 0;
    const QDBusMessage notifyReply = callSync(notify);
    QCOMPARE(notifyReply.type(), QDBusMessage::ReplyMessage);
    const uint id = notifyReply.arguments().value(0).toUInt();
    QVERIFY(findById(id) != nullptr);

    QDBusMessage close =
        QDBusMessage::createMethodCall(QString(), NotificationServer::objectPath(), NotificationServer::serviceName(),
                                       QStringLiteral("CloseNotification"));
    close << id;
    const QDBusMessage closeReply = callSync(close);
    QCOMPARE(closeReply.type(), QDBusMessage::ReplyMessage);
    QVERIFY(findById(id) == nullptr); // removed from the live set over the wire
}

QTEST_GUILESS_MAIN(NotificationsWireTest)
#include "test_wire.moc"
