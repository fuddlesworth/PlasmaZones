// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Wire test: a real MPRIS service on the bus, read by a real MprisHost.
//
// The suite had only a smoke test, so nothing ever checked that the
// metadata a player publishes actually reaches MprisPlayer. That gap
// showed up the first time a player was put in front of the shell in the
// nested harness: the host logged "Player added" and the bar's media chip
// stayed hidden, because the chip is gated on there being a track title
// and the title was empty.
//
// The fake publishes exactly what the spec says and nothing more, through
// org.freedesktop.DBus.Properties.GetAll, which is how MprisPlayer fetches
// its initial state.

#include <PhosphorServiceMpris/MprisHost.h>
#include <PhosphorServiceMpris/MprisPlayer.h>

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDeadlineTimer>
#include <QSignalSpy>
#include <QTest>
#include <QVariantMap>

#include <memory>

namespace {
constexpr auto kService = "org.mpris.MediaPlayer2.phosphorwiretest";
constexpr auto kPath = "/org/mpris/MediaPlayer2";
constexpr auto kPlayerIface = "org.mpris.MediaPlayer2.Player";
constexpr auto kRootIface = "org.mpris.MediaPlayer2";

} // namespace

// Serves org.freedesktop.DBus.Properties for the object below. MprisPlayer
// pulls its whole initial state through GetAll, so that is the one method
// this has to answer correctly.
//
// Outside the anonymous namespace on purpose: both of these are members of
// MprisWireTest, which has external linkage, and a member whose type has
// internal linkage is -Wsubobject-linkage.
class PropertiesAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.Properties")

public:
    explicit PropertiesAdaptor(QObject* parent)
        : QDBusAbstractAdaptor(parent)
    {
    }

public Q_SLOTS:
    QVariantMap GetAll(const QString& iface)
    {
        QVariantMap props;
        if (iface == QLatin1String(kPlayerIface)) {
            QVariantMap metadata;
            metadata.insert(QStringLiteral("mpris:trackid"),
                            QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/mpris/track/1"))));
            metadata.insert(QStringLiteral("mpris:length"), QVariant::fromValue(qlonglong(215000000)));
            metadata.insert(QStringLiteral("xesam:title"), QStringLiteral("Dayvan Cowboy"));
            metadata.insert(QStringLiteral("xesam:artist"), QStringList{QStringLiteral("Boards of Canada")});
            metadata.insert(QStringLiteral("xesam:album"), QStringLiteral("The Campfire Headphase"));

            props.insert(QStringLiteral("PlaybackStatus"), QStringLiteral("Playing"));
            props.insert(QStringLiteral("CanControl"), true);
            props.insert(QStringLiteral("CanPlay"), true);
            props.insert(QStringLiteral("CanPause"), true);
            props.insert(QStringLiteral("CanGoNext"), true);
            props.insert(QStringLiteral("CanGoPrevious"), true);
            props.insert(QStringLiteral("Metadata"), metadata);
            return props;
        }
        if (iface == QLatin1String(kRootIface)) {
            props.insert(QStringLiteral("Identity"), QStringLiteral("Wire Test Player"));
            props.insert(QStringLiteral("CanRaise"), true);
        }
        return props;
    }

    QDBusVariant Get(const QString& iface, const QString& name)
    {
        return QDBusVariant(GetAll(iface).value(name));
    }

    void Set(const QString&, const QString&, const QDBusVariant&)
    {
    }
};

class FakePlayerObject : public QObject
{
    Q_OBJECT
};

class MprisWireTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void hostSeesThePlayer();
    void metadataReachesThePlayer();
    void capabilitiesReachThePlayer();

private:
    /// Spin the event loop until `predicate` holds or the deadline passes.
    /// Every property here arrives through an async D-Bus reply, so a bare
    /// assertion after construction races the reply.
    template<typename Predicate>
    static bool waitFor(Predicate predicate, int timeoutMs = 5000)
    {
        QDeadlineTimer deadline(timeoutMs);
        while (!predicate() && !deadline.hasExpired()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        return predicate();
    }

    std::unique_ptr<FakePlayerObject> m_object;
    std::unique_ptr<PhosphorServiceMpris::MprisHost> m_host;
};

void MprisWireTest::initTestCase()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QVERIFY2(bus.isConnected(), "no session bus; the test harness should provide a private one");

    m_object = std::make_unique<FakePlayerObject>();
    new PropertiesAdaptor(m_object.get());
    QVERIFY2(bus.registerObject(QLatin1String(kPath), m_object.get()), "could not export the fake player object");
    QVERIFY2(bus.registerService(QLatin1String(kService)), "could not take the fake player's bus name");

    // Constructed AFTER the fake is on the bus, which is the case a user
    // hits most: a player already running when the shell starts.
    m_host = std::make_unique<PhosphorServiceMpris::MprisHost>();
}

void MprisWireTest::hostSeesThePlayer()
{
    QVERIFY2(waitFor([this] {
                 return m_host->playerCount() > 0;
             }),
             "MprisHost never saw the player on the bus");
}

void MprisWireTest::metadataReachesThePlayer()
{
    QVERIFY(waitFor([this] {
        return m_host->playerCount() > 0;
    }));
    PhosphorServiceMpris::MprisPlayer* player = m_host->playerAt(0);
    QVERIFY(player);

    // THE case this file exists for. The host seeing the name is not the
    // same as the track reaching the player, and the bar's media chip is
    // gated on the title, so an empty one hides the chip entirely.
    QVERIFY2(waitFor([player] {
                 return !player->trackTitle().isEmpty();
             }),
             "the player's metadata never arrived: trackTitle is still empty");
    QCOMPARE(player->trackTitle(), QStringLiteral("Dayvan Cowboy"));
    QCOMPARE(player->trackArtist(), QStringLiteral("Boards of Canada"));
    QCOMPARE(player->trackAlbum(), QStringLiteral("The Campfire Headphase"));
}

void MprisWireTest::capabilitiesReachThePlayer()
{
    QVERIFY(waitFor([this] {
        return m_host->playerCount() > 0;
    }));
    PhosphorServiceMpris::MprisPlayer* player = m_host->playerAt(0);
    QVERIFY(player);

    // The media panel's transport buttons are each gated on one of these,
    // so a player whose capabilities never arrive renders every control
    // disabled while it plays.
    QVERIFY2(waitFor([player] {
                 return player->canControl();
             }),
             "canControl never arrived");
    QVERIFY(player->isPlaying());
    QVERIFY(player->canGoNext());
    QVERIFY(player->canGoPrevious());
}

QTEST_MAIN(MprisWireTest)

#include "test_wire.moc"
