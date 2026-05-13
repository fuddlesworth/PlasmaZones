// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/MprisPlayer.h>

#include "mpris_mediaplayer2_interface.h"
#include "mpris_player_interface.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QTimer>
#include <QVariantMap>

Q_LOGGING_CATEGORY(lcMpris, "phosphorservices.mpris")

static constexpr auto kMprisPath = "/org/mpris/MediaPlayer2";
static constexpr int kPositionPollMs = 1000;

namespace PhosphorServices {

class MprisPlayer::Private
{
public:
    MprisPlayer* owner = nullptr;
    QString service;
    std::unique_ptr<OrgMprisMediaPlayer2Interface> rootProxy;
    std::unique_ptr<OrgMprisMediaPlayer2PlayerInterface> playerProxy;
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
        auto setStr = [](QString& field, const QString& val, auto signal, auto* o) {
            if (field == val)
                return;
            field = val;
            Q_EMIT(o->*signal)();
        };
        setStr(identity, rootProxy->identity(), &MprisPlayer::identityChanged, owner);
        setStr(desktopEntry, rootProxy->desktopEntry(), &MprisPlayer::desktopEntryChanged, owner);
    }

    void refreshPlayer()
    {
        const QString statusStr = playerProxy->playbackStatus();
        PlaybackState newState = Stopped;
        if (statusStr == QLatin1String("Playing"))
            newState = Playing;
        else if (statusStr == QLatin1String("Paused"))
            newState = Paused;
        if (playbackState != newState) {
            playbackState = newState;
            Q_EMIT owner->playbackStateChanged();
        }

        refreshMetadata(playerProxy->metadata());

        auto setReal = [](qreal& field, qreal val, auto signal, auto* o) {
            if (qFuzzyCompare(field, val))
                return;
            field = val;
            Q_EMIT(o->*signal)();
        };
        setReal(volume, playerProxy->volume(), &MprisPlayer::volumeChanged, owner);
        setReal(rate, playerProxy->rate(), &MprisPlayer::rateChanged, owner);

        const QString loopStr = playerProxy->loopStatus();
        LoopState newLoop = LoopNone;
        if (loopStr == QLatin1String("Track"))
            newLoop = LoopTrack;
        else if (loopStr == QLatin1String("Playlist"))
            newLoop = LoopPlaylist;
        if (loopState != newLoop) {
            loopState = newLoop;
            Q_EMIT owner->loopStateChanged();
        }

        bool newShuffle = playerProxy->shuffle();
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
        setBool(canPlay, playerProxy->canPlay(), &MprisPlayer::canPlayChanged, owner);
        setBool(canPause, playerProxy->canPause(), &MprisPlayer::canPauseChanged, owner);
        setBool(canSeek, playerProxy->canSeek(), &MprisPlayer::canSeekChanged, owner);
        setBool(canGoNext, playerProxy->canGoNext(), &MprisPlayer::canGoNextChanged, owner);
        setBool(canGoPrevious, playerProxy->canGoPrevious(), &MprisPlayer::canGoPreviousChanged, owner);
        setBool(canControl, playerProxy->canControl(), &MprisPlayer::canControlChanged, owner);

        positionUs = playerProxy->position();
        Q_EMIT owner->positionChanged();

        updatePositionTimer();
    }

    void refreshMetadata(const QVariantMap& meta)
    {
        bool changed = false;
        auto setMeta = [&changed](QString& field, const QVariant& val) {
            QString s = val.toString();
            if (field == s)
                return;
            field = s;
            changed = true;
        };
        setMeta(trackTitle, meta.value(QStringLiteral("xesam:title")));
        setMeta(trackAlbum, meta.value(QStringLiteral("xesam:album")));
        setMeta(trackArtUrl, meta.value(QStringLiteral("mpris:artUrl")));

        const QVariant artistVar = meta.value(QStringLiteral("xesam:artist"));
        QString artistStr;
        if (artistVar.typeId() == QMetaType::QStringList)
            artistStr = artistVar.toStringList().join(QStringLiteral(", "));
        else
            artistStr = artistVar.toString();
        if (trackArtist != artistStr) {
            trackArtist = artistStr;
            changed = true;
        }

        qint64 newLength = meta.value(QStringLiteral("mpris:length"), 0).toLongLong();
        if (lengthUs != newLength) {
            lengthUs = newLength;
            changed = true;
        }

        if (changed)
            Q_EMIT owner->metadataChanged();
    }

    void onPropertiesChanged(const QString& iface, const QVariantMap& changed, const QStringList& /*invalidated*/)
    {
        if (iface == QLatin1String("org.mpris.MediaPlayer2")) {
            refreshRoot();
        } else if (iface == QLatin1String("org.mpris.MediaPlayer2.Player")) {
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
                const QVariant v = changed.value(QStringLiteral("Metadata"));
                refreshMetadata(qdbus_cast<QVariantMap>(v));
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

    d->rootProxy = std::make_unique<OrgMprisMediaPlayer2Interface>(serviceName, QLatin1String(kMprisPath),
                                                                   QDBusConnection::sessionBus(), this);
    d->playerProxy = std::make_unique<OrgMprisMediaPlayer2PlayerInterface>(serviceName, QLatin1String(kMprisPath),
                                                                           QDBusConnection::sessionBus(), this);

    d->positionTimer.setSingleShot(false);
    connect(&d->positionTimer, &QTimer::timeout, this, [this]() {
        d->tickPosition();
    });

    QDBusConnection::sessionBus().connect(
        serviceName, QLatin1String(kMprisPath), QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"), this, SLOT(_q_onPropertiesChanged(QString, QVariantMap, QStringList)));

    connect(d->playerProxy.get(), &OrgMprisMediaPlayer2PlayerInterface::Seeked, this, [this](qint64 pos) {
        d->onSeeked(pos);
    });

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
    d->playerProxy->setVolume(v);
}
void MprisPlayer::setShuffle(bool s)
{
    d->playerProxy->setShuffle(s);
}

void MprisPlayer::setLoopState(LoopState state)
{
    QString str = QStringLiteral("None");
    if (state == LoopTrack)
        str = QStringLiteral("Track");
    else if (state == LoopPlaylist)
        str = QStringLiteral("Playlist");
    d->playerProxy->setLoopStatus(str);
}

void MprisPlayer::play()
{
    d->playerProxy->Play();
}
void MprisPlayer::pause()
{
    d->playerProxy->Pause();
}
void MprisPlayer::stop()
{
    d->playerProxy->Stop();
}
void MprisPlayer::togglePlaying()
{
    d->playerProxy->PlayPause();
}
void MprisPlayer::next()
{
    d->playerProxy->Next();
}
void MprisPlayer::previous()
{
    d->playerProxy->Previous();
}
void MprisPlayer::seek(qreal offsetSeconds)
{
    d->playerProxy->Seek(static_cast<qint64>(offsetSeconds * 1e6));
}
void MprisPlayer::raise()
{
    d->rootProxy->Raise();
}
void MprisPlayer::quit()
{
    d->rootProxy->Quit();
}

void MprisPlayer::_q_onPropertiesChanged(const QString& iface, const QVariantMap& changed,
                                         const QStringList& invalidated)
{
    d->onPropertiesChanged(iface, changed, invalidated);
}

} // namespace PhosphorServices
