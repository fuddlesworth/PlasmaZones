// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/MprisPlayer.h>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QTimer>
#include <QVariantMap>

#include <cmath>

Q_LOGGING_CATEGORY(lcMpris, "phosphorservices.mpris")

namespace {
constexpr auto kMprisPath = "/org/mpris/MediaPlayer2";
constexpr auto kPlayerIface = "org.mpris.MediaPlayer2.Player";
constexpr auto kRootIface = "org.mpris.MediaPlayer2";
constexpr auto kPropsIface = "org.freedesktop.DBus.Properties";
constexpr int kPositionPollMs = 1000;
// Resync the interpolated playback position against the player's real
// Position property once every N ticks to correct accumulated drift.
constexpr int kPositionResyncTicks = 30;
} // namespace

namespace PhosphorServices {

// ─── Change-guarded field setters ────────────────────────────────────────
// Free helpers so applyRoot()/applyPlayer() stay flat. Each emits its
// NOTIFY signal only when the value actually changes.
static void setStrField(QString& field, const QString& value, void (MprisPlayer::*sig)(), MprisPlayer* o)
{
    if (field == value)
        return;
    field = value;
    Q_EMIT(o->*sig)();
}

static void setRealField(qreal& field, qreal value, void (MprisPlayer::*sig)(), MprisPlayer* o)
{
    if (qFuzzyCompare(field + 1.0, value + 1.0))
        return;
    field = value;
    Q_EMIT(o->*sig)();
}

static void setBoolField(bool& field, bool value, void (MprisPlayer::*sig)(), MprisPlayer* o)
{
    if (field == value)
        return;
    field = value;
    Q_EMIT(o->*sig)();
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

static QVariantMap demarshallMetadata(const QVariant& var)
{
    // Case 1: already a QVariantMap (some Qt versions demarshal a{sv} directly)
    if (var.typeId() == QMetaType::QVariantMap)
        return var.toMap();
    // Case 2: QDBusArgument wrapping a{sv}
    if (var.canConvert<QDBusArgument>())
        return qdbus_cast<QVariantMap>(var.value<QDBusArgument>());
    // Case 3: QDBusVariant wrapping a QDBusArgument (double-wrapped)
    if (var.canConvert<QDBusVariant>()) {
        QVariant inner = var.value<QDBusVariant>().variant();
        if (inner.canConvert<QDBusArgument>())
            return qdbus_cast<QVariantMap>(inner.value<QDBusArgument>());
        if (inner.typeId() == QMetaType::QVariantMap)
            return inner.toMap();
    }
    // Case 4: try qdbus_cast directly on the variant
    return qdbus_cast<QVariantMap>(var);
}

class MprisPlayer::Private
{
public:
    MprisPlayer* owner = nullptr;
    QString service;
    QDBusConnection bus = QDBusConnection::sessionBus();
    QTimer positionTimer;
    int positionTickCount = 0;

    QString identity;
    QString desktopEntry;
    PlaybackState playbackState = Stopped;
    QString trackTitle;
    QString trackArtist;
    QString trackAlbum;
    QString trackArtUrl;
    QString trackId;
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

    // ─── Async property fetch ─────────────────────────────────────────────
    // GetAll on the Properties interface pulls every property of an
    // interface in ONE round trip, and asyncCall keeps the GUI thread
    // free while the (potentially slow — Spotify can take 500+ ms)
    // reply is in flight. The watcher is parented to `owner`, so a
    // player destroyed mid-flight cancels delivery cleanly.
    void getAll(const char* iface, void (Private::*handler)(const QVariantMap&))
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(service, QLatin1String(kMprisPath),
                                                          QLatin1String(kPropsIface), QStringLiteral("GetAll"));
        msg << QLatin1String(iface);
        auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), owner);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner,
                         [this, handler](QDBusPendingCallWatcher* call) {
                             call->deleteLater();
                             const QDBusPendingReply<QVariantMap> reply = *call;
                             if (reply.isError()) {
                                 qCDebug(lcMpris) << "GetAll failed for" << service << ":" << reply.error().message();
                                 return;
                             }
                             (this->*handler)(reply.value());
                         });
    }

    // Lightweight async resync of just the Position property — used by
    // the drift-correction tick. Position is excluded from
    // PropertiesChanged by the MPRIS spec, so it must be polled.
    void requestPosition()
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(service, QLatin1String(kMprisPath),
                                                          QLatin1String(kPropsIface), QStringLiteral("Get"));
        msg << QLatin1String(kPlayerIface) << QStringLiteral("Position");
        auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), owner);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner, [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<QDBusVariant> reply = *call;
            if (reply.isError())
                return;
            const qint64 newPos = reply.value().variant().toLongLong();
            if (positionUs != newPos) {
                positionUs = newPos;
                Q_EMIT owner->positionChanged();
            }
        });
    }

    void applyRoot(const QVariantMap& props)
    {
        if (props.contains(QStringLiteral("Identity")))
            setStrField(identity, props.value(QStringLiteral("Identity")).toString(), &MprisPlayer::identityChanged,
                        owner);
        if (props.contains(QStringLiteral("DesktopEntry")))
            setStrField(desktopEntry, props.value(QStringLiteral("DesktopEntry")).toString(),
                        &MprisPlayer::desktopEntryChanged, owner);
    }

    // Applies a player-interface property map. Works for both a full
    // GetAll reply and a partial PropertiesChanged `changed` map — every
    // field is gated on contains().
    void applyPlayer(const QVariantMap& props)
    {
        if (props.contains(QStringLiteral("PlaybackStatus"))) {
            const QString s = props.value(QStringLiteral("PlaybackStatus")).toString();
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
        if (props.contains(QStringLiteral("Metadata")))
            applyMetadata(demarshallMetadata(props.value(QStringLiteral("Metadata"))));
        if (props.contains(QStringLiteral("Volume")))
            setRealField(volume, props.value(QStringLiteral("Volume")).toDouble(), &MprisPlayer::volumeChanged, owner);
        if (props.contains(QStringLiteral("Rate")))
            setRealField(rate, props.value(QStringLiteral("Rate")).toDouble(), &MprisPlayer::rateChanged, owner);
        if (props.contains(QStringLiteral("Shuffle")))
            setBoolField(shuffle, props.value(QStringLiteral("Shuffle")).toBool(), &MprisPlayer::shuffleChanged, owner);
        if (props.contains(QStringLiteral("LoopStatus"))) {
            const QString ls = props.value(QStringLiteral("LoopStatus")).toString();
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
        if (props.contains(QStringLiteral("CanPlay")))
            setBoolField(canPlay, props.value(QStringLiteral("CanPlay")).toBool(), &MprisPlayer::canPlayChanged, owner);
        if (props.contains(QStringLiteral("CanPause")))
            setBoolField(canPause, props.value(QStringLiteral("CanPause")).toBool(), &MprisPlayer::canPauseChanged,
                         owner);
        if (props.contains(QStringLiteral("CanSeek")))
            setBoolField(canSeek, props.value(QStringLiteral("CanSeek")).toBool(), &MprisPlayer::canSeekChanged, owner);
        if (props.contains(QStringLiteral("CanGoNext")))
            setBoolField(canGoNext, props.value(QStringLiteral("CanGoNext")).toBool(), &MprisPlayer::canGoNextChanged,
                         owner);
        if (props.contains(QStringLiteral("CanGoPrevious")))
            setBoolField(canGoPrevious, props.value(QStringLiteral("CanGoPrevious")).toBool(),
                         &MprisPlayer::canGoPreviousChanged, owner);
        if (props.contains(QStringLiteral("CanControl")))
            setBoolField(canControl, props.value(QStringLiteral("CanControl")).toBool(),
                         &MprisPlayer::canControlChanged, owner);
        // Position is only present in a GetAll reply (the MPRIS spec
        // forbids it in PropertiesChanged); the contains() guard makes
        // the partial-update case a no-op.
        if (props.contains(QStringLiteral("Position"))) {
            const qint64 newPos = props.value(QStringLiteral("Position")).toLongLong();
            if (positionUs != newPos) {
                positionUs = newPos;
                Q_EMIT owner->positionChanged();
            }
        }
    }

    void applyMetadata(const QVariantMap& meta)
    {
        bool changed = false;
        auto metaString = [](const QVariant& val) -> QString {
            if (val.canConvert<QDBusVariant>())
                return val.value<QDBusVariant>().variant().toString();
            return val.toString();
        };
        // mpris:trackid is a D-Bus object path (`o`). QVariant::toString()
        // does not unwrap a QDBusObjectPath, so metaString() would yield
        // an empty string — extract the path explicitly. An empty trackId
        // breaks both SetPosition() and track-change detection (which
        // gates the art-URL pin).
        auto metaObjectPath = [](const QVariant& val) -> QString {
            QVariant inner = val;
            if (inner.canConvert<QDBusVariant>())
                inner = inner.value<QDBusVariant>().variant();
            if (inner.canConvert<QDBusObjectPath>())
                return inner.value<QDBusObjectPath>().path();
            return inner.toString();
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

        // Detect track change FIRST. Some players (Spotify in particular)
        // publish a full-resolution mpris:artUrl on the initial track
        // change, then re-publish a few hundred ms later with a smaller
        // cached/cropped URL — both for the same trackid. Pin trackArtUrl
        // for the lifetime of a trackid: take the first non-empty URL we
        // see and ignore subsequent changes until the trackid changes
        // (i.e. a real track switch). Without this, the popup's Image
        // re-decodes at the smaller URL and the user sees high-quality
        // art "refresh with lower quality" shortly after a track change.
        if (meta.contains(QStringLiteral("mpris:trackid"))) {
            QString id = metaObjectPath(meta.value(QStringLiteral("mpris:trackid")));
            if (trackId != id) {
                trackId = id;
                // A new track invalidates the pinned art URL. Clearing a
                // URL we actually held is an observable change even when
                // this metadata map carries no replacement artUrl yet.
                if (!trackArtUrl.isEmpty()) {
                    trackArtUrl.clear();
                    changed = true;
                }
            }
        }
        if (meta.contains(QStringLiteral("mpris:artUrl"))) {
            QString s = metaString(meta.value(QStringLiteral("mpris:artUrl")));
            // First non-empty URL wins for this trackid (see above).
            if (trackArtUrl.isEmpty() && !s.isEmpty()) {
                trackArtUrl = s;
                changed = true;
            }
        }

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
            } else if (artistVar.canConvert<QDBusArgument>()) {
                artistStr = qdbus_cast<QStringList>(artistVar.value<QDBusArgument>()).join(QStringLiteral(", "));
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

    void onPropertiesChanged(const QString& iface, const QVariantMap& changed, const QStringList& invalidated)
    {
        if (iface == QLatin1String(kRootIface)) {
            applyRoot(changed);
            // Invalidated properties carry no value — re-fetch the whole
            // interface asynchronously to pick them up.
            if (!invalidated.isEmpty())
                getAll(kRootIface, &Private::applyRoot);
        } else if (iface == QLatin1String(kPlayerIface)) {
            applyPlayer(changed);
            if (!invalidated.isEmpty())
                getAll(kPlayerIface, &Private::applyPlayer);
        }
    }

    void onSeeked(qint64 posUs)
    {
        positionUs = posUs;
        // The reported position is now authoritative — restart the
        // resync cadence so interpolation drifts from this fresh base.
        positionTickCount = 0;
        Q_EMIT owner->positionChanged();
    }

    void tickPosition()
    {
        if (playbackState != Playing)
            return;
        ++positionTickCount;
        if (positionTickCount >= kPositionResyncTicks) {
            positionTickCount = 0;
            // Async resync — emits positionChanged itself if the value moved.
            requestPosition();
        } else {
            positionUs += static_cast<qint64>(rate * kPositionPollMs * 1000);
            Q_EMIT owner->positionChanged();
        }
    }

    void updatePositionTimer()
    {
        if (playbackState == Playing) {
            positionTickCount = 0;
            positionTimer.start(kPositionPollMs);
        } else {
            positionTimer.stop();
        }
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

    QDBusConnection::sessionBus().connect(serviceName, QLatin1String(kMprisPath), QLatin1String(kPropsIface),
                                          QStringLiteral("PropertiesChanged"), this,
                                          SLOT(_q_onPropertiesChanged(QString, QVariantMap, QStringList)));

    QDBusConnection::sessionBus().connect(serviceName, QLatin1String(kMprisPath), QLatin1String(kPlayerIface),
                                          QStringLiteral("Seeked"), this, SLOT(_q_onSeeked(qlonglong)));

    // Async initial fetch — properties populate as the replies land; the
    // GUI thread is never blocked waiting on a slow player.
    d->getAll(kRootIface, &Private::applyRoot);
    d->getAll(kPlayerIface, &Private::applyPlayer);

    // Retry GetAll(Player) on a backoff schedule if the initial reply
    // lacked mpris:artUrl. Different players publish the artUrl on
    // different timelines:
    //   - Spotify desktop: ~500 ms after track start (lazy URL gen).
    //   - Firefox: 1-3 s after track start. mpris:artUrl is a
    //     `file://` path to an image Firefox decodes and writes to
    //     ~/.local/share/firefox-mpris/<pid>_<n>.png on a background
    //     thread; the path is only set in MPRISServiceHandler once
    //     the file write completes. PropertiesChanged eventually
    //     fires with the full Metadata, but on a clean shell startup
    //     where the user is mid-track in Firefox, the image may have
    //     been written long ago and Firefox won't re-emit
    //     PropertiesChanged until the page changes metadata — so we
    //     have to poll instead of relying on the signal.
    //   - Plain Spotify-Web-via-plasma-browser-integration: similar
    //     to Firefox, file:// URL written after a short delay.
    // Three retries cover the typical 0.5/1.5/3 s windows. Each is a
    // cheap async GetAll; the guard returns early once trackArtUrl
    // is populated. Capped at 3 s after construction.
    for (int delayMs : {500, 1500, 3000}) {
        QTimer::singleShot(delayMs, this, [this]() {
            if (d->trackArtUrl.isEmpty() && d->playbackState != Stopped)
                d->getAll(kPlayerIface, &Private::applyPlayer);
        });
    }
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
    // Reject non-finite input at the boundary — a NaN/inf would be
    // marshalled straight onto the bus to the player.
    if (!std::isfinite(v))
        return;
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
    // A non-finite offset would make the static_cast<qint64> below UB.
    if (!std::isfinite(offsetSeconds))
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(d->service, QLatin1String(kMprisPath),
                                                      QLatin1String(kPlayerIface), QStringLiteral("Seek"));
    msg << static_cast<qint64>(offsetSeconds * 1e6);
    d->bus.asyncCall(msg);
}

void MprisPlayer::setPosition(qreal absoluteSeconds)
{
    // A non-finite position would make the static_cast<qint64> below UB.
    if (!std::isfinite(absoluteSeconds))
        return;
    QDBusMessage msg = QDBusMessage::createMethodCall(d->service, QLatin1String(kMprisPath),
                                                      QLatin1String(kPlayerIface), QStringLiteral("SetPosition"));
    QString trackPath = d->trackId.isEmpty() ? QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack") : d->trackId;
    msg << QVariant::fromValue(QDBusObjectPath(trackPath)) << static_cast<qint64>(absoluteSeconds * 1e6);
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
