// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/MprisPlayer.h>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QLoggingCategory>
#include <QTimer>
#include <QVariantMap>

Q_LOGGING_CATEGORY(lcMpris, "phosphorservices.mpris")

namespace {
constexpr auto kMprisPath = "/org/mpris/MediaPlayer2";
constexpr auto kPlayerIface = "org.mpris.MediaPlayer2.Player";
constexpr auto kRootIface = "org.mpris.MediaPlayer2";
constexpr auto kPropsIface = "org.freedesktop.DBus.Properties";
constexpr int kPositionPollMs = 1000;
} // namespace

namespace PhosphorServices {

static QVariant dbusProperty(QDBusConnection& bus, const QString& service, const char* iface, const char* prop)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(service, QLatin1String(kMprisPath), QLatin1String(kPropsIface),
                                                      QStringLiteral("Get"));
    msg << QLatin1String(iface) << QLatin1String(prop);
    QDBusMessage reply = bus.call(msg, QDBus::Block, 200);
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().value<QDBusVariant>().variant();
    return {};
}

static void dbusSetProperty(QDBusConnection& bus, const QString& service, const char* iface, const char* prop,
                            const QVariant& value)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(service, QLatin1String(kMprisPath), QLatin1String(kPropsIface),
                                                      QStringLiteral("Set"));
    msg << QLatin1String(iface) << QLatin1String(prop) << QVariant::fromValue(QDBusVariant(value));
    bus.asyncCall(msg);
}

static void dbusCall(QDBusConnection& bus, const QString& service, const char* iface, const char* method)
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(service, QLatin1String(kMprisPath), QLatin1String(iface), QLatin1String(method));
    bus.asyncCall(msg);
}

class MprisPlayer::Private
{
public:
    MprisPlayer* owner = nullptr;
    QString service;
    QDBusConnection bus = QDBusConnection::sessionBus();
    QTimer positionTimer;

    QString identity;
    QString desktopEntry;
    PlaybackState playbackState = Stopped;
    QString trackTitle;
    QString trackArtist;
    QString trackAlbum;
    QString trackArtUrl;
    qint64 positionUs = 0;
    qint64 lengthUs = 0;
    qreal volume = 0.0;
    qreal rate = 1.0;
    LoopState loopState = LoopNone;
    bool shuffle = false;
    bool canPlay = false;
    bool canPause = false;
    bool canSeek = false;
    bool canGoNext = false;
    bool canGoPrevious = false;
    bool canControl = false;

    void refreshRoot()
    {
        auto setStr = [](QString& field, const QVariant& val, auto signal, auto* o) {
            QString s = val.toString();
            if (field == s)
                return;
            field = s;
            Q_EMIT(o->*signal)();
        };
        setStr(identity, dbusProperty(bus, service, kRootIface, "Identity"), &MprisPlayer::identityChanged, owner);
        setStr(desktopEntry, dbusProperty(bus, service, kRootIface, "DesktopEntry"), &MprisPlayer::desktopEntryChanged,
               owner);
    }

    void refreshPlayer()
    {
        const QString statusStr = dbusProperty(bus, service, kPlayerIface, "PlaybackStatus").toString();
        PlaybackState newState = Stopped;
        if (statusStr == QLatin1String("Playing"))
            newState = Playing;
        else if (statusStr == QLatin1String("Paused"))
            newState = Paused;
        if (playbackState != newState) {
            playbackState = newState;
            Q_EMIT owner->playbackStateChanged();
        }

        {
            QVariant metaVar = dbusProperty(bus, service, kPlayerIface, "Metadata");
            QVariantMap meta;
            if (metaVar.canConvert<QDBusArgument>())
                meta = qdbus_cast<QVariantMap>(metaVar.value<QDBusArgument>());
            else
                meta = metaVar.toMap();
            qCDebug(lcMpris) << "Metadata for" << service << "keys:" << meta.keys()
                             << "artUrl:" << meta.value(QStringLiteral("mpris:artUrl"));
            refreshMetadata(meta);
        }

        auto setReal = [](qreal& field, qreal val, auto signal, auto* o) {
            if (qFuzzyCompare(field, val))
                return;
            field = val;
            Q_EMIT(o->*signal)();
        };
        setReal(volume, dbusProperty(bus, service, kPlayerIface, "Volume").toDouble(), &MprisPlayer::volumeChanged,
                owner);
        setReal(rate, dbusProperty(bus, service, kPlayerIface, "Rate").toDouble(), &MprisPlayer::rateChanged, owner);

        const QString loopStr = dbusProperty(bus, service, kPlayerIface, "LoopStatus").toString();
        LoopState newLoop = LoopNone;
        if (loopStr == QLatin1String("Track"))
            newLoop = LoopTrack;
        else if (loopStr == QLatin1String("Playlist"))
            newLoop = LoopPlaylist;
        if (loopState != newLoop) {
            loopState = newLoop;
            Q_EMIT owner->loopStateChanged();
        }

        bool newShuffle = dbusProperty(bus, service, kPlayerIface, "Shuffle").toBool();
        if (shuffle != newShuffle) {
            shuffle = newShuffle;
            Q_EMIT owner->shuffleChanged();
        }

        auto setBool = [](bool& field, bool val, auto signal, auto* o) {
            if (field == val)
                return;
            field = val;
            Q_EMIT(o->*signal)();
        };
        setBool(canPlay, dbusProperty(bus, service, kPlayerIface, "CanPlay").toBool(), &MprisPlayer::canPlayChanged,
                owner);
        setBool(canPause, dbusProperty(bus, service, kPlayerIface, "CanPause").toBool(), &MprisPlayer::canPauseChanged,
                owner);
        setBool(canSeek, dbusProperty(bus, service, kPlayerIface, "CanSeek").toBool(), &MprisPlayer::canSeekChanged,
                owner);
        setBool(canGoNext, dbusProperty(bus, service, kPlayerIface, "CanGoNext").toBool(),
                &MprisPlayer::canGoNextChanged, owner);
        setBool(canGoPrevious, dbusProperty(bus, service, kPlayerIface, "CanGoPrevious").toBool(),
                &MprisPlayer::canGoPreviousChanged, owner);
        setBool(canControl, dbusProperty(bus, service, kPlayerIface, "CanControl").toBool(),
                &MprisPlayer::canControlChanged, owner);

        positionUs = dbusProperty(bus, service, kPlayerIface, "Position").toLongLong();
        Q_EMIT owner->positionChanged();

        updatePositionTimer();
    }

    void refreshMetadata(const QVariantMap& meta)
    {
        bool changed = false;
        auto metaString = [](const QVariant& val) -> QString {
            if (val.canConvert<QDBusVariant>())
                return val.value<QDBusVariant>().variant().toString();
            return val.toString();
        };
        // Only update a field if the key is present in the map.
        // PropertiesChanged delivers PARTIAL metadata — missing keys
        // mean "unchanged", not "cleared". Without this guard, a
        // partial update (e.g. just mpris:length) wipes trackArtUrl.
        auto setIfPresent = [&changed, &metaString](const QVariantMap& m, const QString& key, QString& field) {
            if (!m.contains(key))
                return;
            QString s = metaString(m.value(key));
            if (field == s)
                return;
            field = s;
            changed = true;
        };
        setIfPresent(meta, QStringLiteral("xesam:title"), trackTitle);
        setIfPresent(meta, QStringLiteral("xesam:album"), trackAlbum);
        setIfPresent(meta, QStringLiteral("mpris:artUrl"), trackArtUrl);

        if (meta.contains(QStringLiteral("xesam:artist"))) {
            const QVariant artistVar = meta.value(QStringLiteral("xesam:artist"));
            QString artistStr;
            if (artistVar.canConvert<QDBusVariant>()) {
                QVariant inner = artistVar.value<QDBusVariant>().variant();
                if (inner.typeId() == QMetaType::QStringList)
                    artistStr = inner.toStringList().join(QStringLiteral(", "));
                else
                    artistStr = inner.toString();
            } else if (artistVar.typeId() == QMetaType::QStringList) {
                artistStr = artistVar.toStringList().join(QStringLiteral(", "));
            } else {
                artistStr = artistVar.toString();
            }
            if (trackArtist != artistStr) {
                trackArtist = artistStr;
                changed = true;
            }
        }

        if (meta.contains(QStringLiteral("mpris:length"))) {
            auto metaLongLong = [](const QVariant& val) -> qint64 {
                if (val.canConvert<QDBusVariant>())
                    return val.value<QDBusVariant>().variant().toLongLong();
                return val.toLongLong();
            };
            qint64 newLength = metaLongLong(meta.value(QStringLiteral("mpris:length")));
            if (lengthUs != newLength) {
                lengthUs = newLength;
                changed = true;
            }
        }

        if (changed)
            Q_EMIT owner->metadataChanged();
    }

    void onPropertiesChanged(const QString& iface, const QVariantMap& changed, const QStringList& /*invalidated*/)
    {
        if (iface == QLatin1String(kRootIface)) {
            refreshRoot();
        } else if (iface == QLatin1String(kPlayerIface)) {
            if (changed.contains(QStringLiteral("PlaybackStatus"))) {
                const QString s = changed.value(QStringLiteral("PlaybackStatus")).toString();
                PlaybackState ns = Stopped;
                if (s == QLatin1String("Playing"))
                    ns = Playing;
                else if (s == QLatin1String("Paused"))
                    ns = Paused;
                if (playbackState != ns) {
                    playbackState = ns;
                    Q_EMIT owner->playbackStateChanged();
                    updatePositionTimer();
                }
            }
            if (changed.contains(QStringLiteral("Metadata"))) {
                QVariant metaVar = changed.value(QStringLiteral("Metadata"));
                QVariantMap meta;
                if (metaVar.canConvert<QDBusArgument>())
                    meta = qdbus_cast<QVariantMap>(metaVar.value<QDBusArgument>());
                else
                    meta = metaVar.toMap();
                refreshMetadata(meta);
            }
            if (changed.contains(QStringLiteral("Volume"))) {
                qreal v = changed.value(QStringLiteral("Volume")).toDouble();
                if (!qFuzzyCompare(volume, v)) {
                    volume = v;
                    Q_EMIT owner->volumeChanged();
                }
            }
            if (changed.contains(QStringLiteral("Shuffle"))) {
                bool v = changed.value(QStringLiteral("Shuffle")).toBool();
                if (shuffle != v) {
                    shuffle = v;
                    Q_EMIT owner->shuffleChanged();
                }
            }
            if (changed.contains(QStringLiteral("LoopStatus"))) {
                const QString ls = changed.value(QStringLiteral("LoopStatus")).toString();
                LoopState nl = LoopNone;
                if (ls == QLatin1String("Track"))
                    nl = LoopTrack;
                else if (ls == QLatin1String("Playlist"))
                    nl = LoopPlaylist;
                if (loopState != nl) {
                    loopState = nl;
                    Q_EMIT owner->loopStateChanged();
                }
            }
            if (changed.contains(QStringLiteral("CanPlay"))) {
                canPlay = changed.value(QStringLiteral("CanPlay")).toBool();
                Q_EMIT owner->canPlayChanged();
            }
            if (changed.contains(QStringLiteral("CanPause"))) {
                canPause = changed.value(QStringLiteral("CanPause")).toBool();
                Q_EMIT owner->canPauseChanged();
            }
            if (changed.contains(QStringLiteral("CanSeek"))) {
                canSeek = changed.value(QStringLiteral("CanSeek")).toBool();
                Q_EMIT owner->canSeekChanged();
            }
            if (changed.contains(QStringLiteral("CanGoNext"))) {
                canGoNext = changed.value(QStringLiteral("CanGoNext")).toBool();
                Q_EMIT owner->canGoNextChanged();
            }
            if (changed.contains(QStringLiteral("CanGoPrevious"))) {
                canGoPrevious = changed.value(QStringLiteral("CanGoPrevious")).toBool();
                Q_EMIT owner->canGoPreviousChanged();
            }
        }
    }

    void onSeeked(qint64 posUs)
    {
        positionUs = posUs;
        Q_EMIT owner->positionChanged();
    }

    void tickPosition()
    {
        if (playbackState == Playing) {
            positionUs += static_cast<qint64>(rate * kPositionPollMs * 1000);
            Q_EMIT owner->positionChanged();
        }
    }

    void updatePositionTimer()
    {
        if (playbackState == Playing)
            positionTimer.start(kPositionPollMs);
        else
            positionTimer.stop();
    }
};

MprisPlayer::MprisPlayer(const QString& serviceName, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->owner = this;
    d->service = serviceName;

    d->positionTimer.setSingleShot(false);
    connect(&d->positionTimer, &QTimer::timeout, this, [this]() {
        d->tickPosition();
    });

    QDBusConnection::sessionBus().connect(
        serviceName, QLatin1String(kMprisPath), QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"), this, SLOT(_q_onPropertiesChanged(QString, QVariantMap, QStringList)));

    QDBusConnection::sessionBus().connect(serviceName, QLatin1String(kMprisPath), QLatin1String(kPlayerIface),
                                          QStringLiteral("Seeked"), this, SLOT(_q_onSeeked(qlonglong)));

    d->refreshRoot();
    d->refreshPlayer();
}

MprisPlayer::~MprisPlayer() = default;

QString MprisPlayer::identity() const
{
    return d->identity;
}
QString MprisPlayer::desktopEntry() const
{
    return d->desktopEntry;
}
QString MprisPlayer::serviceName() const
{
    return d->service;
}
MprisPlayer::PlaybackState MprisPlayer::playbackState() const
{
    return d->playbackState;
}
bool MprisPlayer::isPlaying() const
{
    return d->playbackState == Playing;
}
QString MprisPlayer::trackTitle() const
{
    return d->trackTitle;
}
QString MprisPlayer::trackArtist() const
{
    return d->trackArtist;
}
QString MprisPlayer::trackAlbum() const
{
    return d->trackAlbum;
}
QString MprisPlayer::trackArtUrl() const
{
    return d->trackArtUrl;
}
qreal MprisPlayer::position() const
{
    return static_cast<qreal>(d->positionUs) / 1e6;
}
qreal MprisPlayer::length() const
{
    return static_cast<qreal>(d->lengthUs) / 1e6;
}
qreal MprisPlayer::volume() const
{
    return d->volume;
}
qreal MprisPlayer::rate() const
{
    return d->rate;
}
MprisPlayer::LoopState MprisPlayer::loopState() const
{
    return d->loopState;
}
bool MprisPlayer::shuffle() const
{
    return d->shuffle;
}
bool MprisPlayer::canPlay() const
{
    return d->canPlay;
}
bool MprisPlayer::canPause() const
{
    return d->canPause;
}
bool MprisPlayer::canSeek() const
{
    return d->canSeek;
}
bool MprisPlayer::canGoNext() const
{
    return d->canGoNext;
}
bool MprisPlayer::canGoPrevious() const
{
    return d->canGoPrevious;
}
bool MprisPlayer::canControl() const
{
    return d->canControl;
}

void MprisPlayer::setVolume(qreal v)
{
    dbusSetProperty(d->bus, d->service, kPlayerIface, "Volume", v);
}
void MprisPlayer::setShuffle(bool s)
{
    dbusSetProperty(d->bus, d->service, kPlayerIface, "Shuffle", s);
}

void MprisPlayer::setLoopState(LoopState state)
{
    QString str = QStringLiteral("None");
    if (state == LoopTrack)
        str = QStringLiteral("Track");
    else if (state == LoopPlaylist)
        str = QStringLiteral("Playlist");
    dbusSetProperty(d->bus, d->service, kPlayerIface, "LoopStatus", str);
}

void MprisPlayer::play()
{
    dbusCall(d->bus, d->service, kPlayerIface, "Play");
}
void MprisPlayer::pause()
{
    dbusCall(d->bus, d->service, kPlayerIface, "Pause");
}
void MprisPlayer::stop()
{
    dbusCall(d->bus, d->service, kPlayerIface, "Stop");
}
void MprisPlayer::togglePlaying()
{
    dbusCall(d->bus, d->service, kPlayerIface, "PlayPause");
}
void MprisPlayer::next()
{
    dbusCall(d->bus, d->service, kPlayerIface, "Next");
}
void MprisPlayer::previous()
{
    dbusCall(d->bus, d->service, kPlayerIface, "Previous");
}

void MprisPlayer::seek(qreal offsetSeconds)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(d->service, QLatin1String(kMprisPath),
                                                      QLatin1String(kPlayerIface), QStringLiteral("Seek"));
    msg << static_cast<qint64>(offsetSeconds * 1e6);
    d->bus.asyncCall(msg);
}

void MprisPlayer::raise()
{
    dbusCall(d->bus, d->service, kRootIface, "Raise");
}
void MprisPlayer::quit()
{
    dbusCall(d->bus, d->service, kRootIface, "Quit");
}

void MprisPlayer::_q_onPropertiesChanged(const QString& iface, const QVariantMap& changed,
                                         const QStringList& invalidated)
{
    d->onPropertiesChanged(iface, changed, invalidated);
}

void MprisPlayer::_q_onSeeked(qlonglong position)
{
    d->onSeeked(position);
}

} // namespace PhosphorServices
