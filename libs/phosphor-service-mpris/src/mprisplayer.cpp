// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceMpris/MprisPlayer.h>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(lcMprisPlayer, "phosphor.service.mpris.player")

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

namespace PhosphorServiceMpris {

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

// Watch and log the async D-Bus call's outcome. Failures (CanControl=false,
// peer not present, malformed request) used to be silently swallowed; a
// user clicking Next on a player that rejects the call would see nothing
// at all. The watcher is parented to `receiver` so it dies if the player
// vanishes. `tag` is the property or method name actually issued (e.g.
// "Volume", "Next", "Seek") used in the log line. Every call site passes
// a string literal, so the captured `const char*` lives for the program's
// lifetime; do not pass a stack-allocated buffer here.
static void watchPendingCall(const QDBusPendingCall& call, QObject* receiver, const QString& service, const char* tag)
{
    auto* watcher = new QDBusPendingCallWatcher(call, receiver);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, receiver, [service, tag](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        const QDBusPendingReply<> reply = *w;
        if (reply.isError()) {
            qCDebug(lcMprisPlayer) << tag << "failed for" << service << ":" << reply.error().message();
        }
    });
}

static void dbusSetProperty(QDBusConnection& bus, QObject* receiver, const QString& service, const char* iface,
                            const char* prop, const QVariant& value)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(service, QLatin1String(kMprisPath), QLatin1String(kPropsIface),
                                                      QStringLiteral("Set"));
    msg << QLatin1String(iface) << QLatin1String(prop) << QVariant::fromValue(QDBusVariant(value));
    watchPendingCall(bus.asyncCall(msg), receiver, service, prop);
}

static void dbusCall(QDBusConnection& bus, QObject* receiver, const QString& service, const char* iface,
                     const char* method)
{
    QDBusMessage msg =
        QDBusMessage::createMethodCall(service, QLatin1String(kMprisPath), QLatin1String(iface), QLatin1String(method));
    watchPendingCall(bus.asyncCall(msg), receiver, service, method);
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
    // Per-interface "GetAll already in flight" flag. Coalesces a burst
    // of PropertiesChanged signals carrying `invalidated` into one
    // outstanding refresh per interface so a misbehaving player can't
    // spawn an unbounded queue of QDBusPendingCallWatcher objects.
    bool refreshPendingRoot = false;
    bool refreshPendingPlayer = false;
    QString trackTitle;
    QString trackArtist;
    QString trackAlbum;
    QString trackArtUrl;
    QString trackId;
    qint64 positionUs = 0;
    qint64 lengthUs = 0;
    qreal volume = 0.0;
    qreal rate = 1.0;
    bool trackArtUrlResolved = false; ///< true once art URL settled (set or scheme-rejected) for current trackid
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
    // free while the (potentially slow, Spotify can take 500+ ms)
    // reply is in flight. The watcher is parented to `owner`, so a
    // player destroyed mid-flight cancels delivery cleanly.
    void getAll(const char* iface, void (Private::*handler)(const QVariantMap&), bool* pendingFlag = nullptr)
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(service, QLatin1String(kMprisPath),
                                                          QLatin1String(kPropsIface), QStringLiteral("GetAll"));
        msg << QLatin1String(iface);
        auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), owner);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner,
                         [this, handler, pendingFlag](QDBusPendingCallWatcher* call) {
                             call->deleteLater();
                             if (pendingFlag)
                                 *pendingFlag = false;
                             const QDBusPendingReply<QVariantMap> reply = *call;
                             if (reply.isError()) {
                                 qCDebug(lcMprisPlayer)
                                     << "GetAll failed for" << service << ":" << reply.error().message();
                                 return;
                             }
                             (this->*handler)(reply.value());
                         });
    }

    // Lightweight async resync of just the Position property, used by
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
        // Cap Identity / DesktopEntry the same way Metadata strings
        // are capped (see applyMetadata). A hostile MPRIS-emulating
        // peer publishing a multi-MB Identity would otherwise propagate
        // straight into QML text layout and hang the GUI thread.
        constexpr int kMaxRootStringChars = 4096;
        if (props.contains(QStringLiteral("Identity")))
            setStrField(identity, props.value(QStringLiteral("Identity")).toString().left(kMaxRootStringChars),
                        &MprisPlayer::identityChanged, owner);
        if (props.contains(QStringLiteral("DesktopEntry")))
            setStrField(desktopEntry, props.value(QStringLiteral("DesktopEntry")).toString().left(kMaxRootStringChars),
                        &MprisPlayer::desktopEntryChanged, owner);
    }

    // Wrapper called from getAll's reply path so the member-function-
    // pointer signature stays uniform with applyRoot. Pins
    // fromGetAll=true so applyPlayer can scope Position acceptance to
    // the only path the MPRIS spec permits it on.
    void applyPlayerFromGetAll(const QVariantMap& props)
    {
        applyPlayer(props, true);
    }

    // Applies a player-interface property map. Works for both a full
    // GetAll reply and a partial PropertiesChanged `changed` map; every
    // field is gated on contains(). `fromGetAll` scopes Position
    // acceptance, see the matching comment at the Position branch.
    void applyPlayer(const QVariantMap& props, bool fromGetAll = false)
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
        if (props.contains(QStringLiteral("Volume"))) {
            // A malicious or buggy player can publish NaN/inf, which would
            // propagate through setRealField into QML bindings and slider
            // values. Clamp to the MPRIS [0.0, 1.0] range at the boundary.
            const qreal v = props.value(QStringLiteral("Volume")).toDouble();
            if (std::isfinite(v))
                setRealField(volume, std::clamp(v, 0.0, 1.0), &MprisPlayer::volumeChanged, owner);
        }
        if (props.contains(QStringLiteral("Rate"))) {
            // Same boundary check as Volume; Rate is also reported as
            // `d`. The position-tick math multiplies by `rate`, so a
            // NaN here would corrupt positionUs on the very next tick.
            // Also clamp to a defensive [-64.0, 64.0] window. MPRIS
            // declares MinimumRate/MaximumRate as opt-in player-side
            // properties; we don't read them, but a hostile player
            // publishing Rate ~= 1e15 would survive isfinite, and the
            // subsequent `rate * kPositionPollMs * 1000` in
            // tickPosition would overflow qint64 on the static_cast
            // (implementation-defined per [conv.fpint]). 64x covers
            // every realistic fast-forward / scrub / reverse-playback
            // case mainline players use.
            const qreal r = props.value(QStringLiteral("Rate")).toDouble();
            if (std::isfinite(r))
                setRealField(rate, std::clamp(r, -64.0, 64.0), &MprisPlayer::rateChanged, owner);
        }
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
        // forbids it in PropertiesChanged). Accepting it here from
        // any path would let a misbehaving player stomp the
        // interpolated value with arbitrary state. applyPlayer is
        // shared between GetAll and PropertiesChanged dispatch, so
        // the explicit `fromGetAll` flag scopes Position to the
        // GetAll path only; partial updates ignore Position even if
        // present.
        if (fromGetAll && props.contains(QStringLiteral("Position"))) {
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
        // an empty string, extract the path explicitly. An empty trackId
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
        // PropertiesChanged delivers PARTIAL metadata, missing keys
        // mean "unchanged", not "cleared". Without this guard, a
        // partial update (e.g. just mpris:length) wipes trackArtUrl.
        // Cap metadata string length at the boundary. A malicious or
        // broken player publishing a multi-MB title would inflate
        // memory and (more importantly) hang the GUI thread when QML
        // text layout chews through the run.
        constexpr int kMaxMetaStringChars = 4096;
        auto setIfPresent = [&changed, &metaString](const QVariantMap& m, const QString& key, QString& field) {
            if (!m.contains(key))
                return;
            QString s = metaString(m.value(key)).left(kMaxMetaStringChars);
            if (field == s)
                return;
            field = s;
            changed = true;
        };
        auto clearIfNonEmpty = [&changed](QString& field) {
            if (!field.isEmpty()) {
                field.clear();
                changed = true;
            }
        };

        // Detect track change FIRST so the per-trackid stale-field clear
        // runs BEFORE we apply the new map's title/artist/album/artUrl.
        // Real players (Spotify, Firefox, VLC, mpv) send the FULL
        // metadata along with the new mpris:trackid in a single
        // PropertiesChanged signal: if we cleared after the
        // setIfPresent calls, the just-applied new values would be
        // wiped before metadataChanged fires. trackArtUrl gets the
        // additional "pin first non-empty URL for this trackid"
        // treatment to avoid Spotify's flicker (full-res URL then a
        // smaller cached URL ~hundreds-of-ms later for the same track).
        if (meta.contains(QStringLiteral("mpris:trackid"))) {
            QString id = metaObjectPath(meta.value(QStringLiteral("mpris:trackid")));
            if (trackId != id) {
                trackId = id;
                trackArtUrlResolved = false;
                clearIfNonEmpty(trackArtUrl);
                clearIfNonEmpty(trackTitle);
                clearIfNonEmpty(trackArtist);
                clearIfNonEmpty(trackAlbum);
                // Reset length too: a partial-Metadata update following
                // the new trackid that omits `mpris:length` would
                // otherwise leave the previous track's length displayed.
                if (lengthUs != 0) {
                    lengthUs = 0;
                    changed = true;
                }
            }
        }

        setIfPresent(meta, QStringLiteral("xesam:title"), trackTitle);
        setIfPresent(meta, QStringLiteral("xesam:album"), trackAlbum);

        if (meta.contains(QStringLiteral("mpris:artUrl"))) {
            QString s = metaString(meta.value(QStringLiteral("mpris:artUrl"))).left(kMaxMetaStringChars);
            // First non-empty URL wins for this trackid (see above).
            // True allowlist of safe schemes. Empty-scheme / relative
            // URLs are rejected. javascript:/data:/about: would let a
            // hostile player surface markup in QML's Image cache;
            // qrc:/, ssh://, ftp://, etc. have no use case for media
            // art and an unfamiliar scheme reaching QML is a red flag.
            // file:// stays in the allowlist because Firefox's MPRIS
            // bridge writes track artwork under
            // ~/.local/share/firefox-mpris/.
            auto isSchemeAllowed = [](const QString& url) {
                const QUrl u(url);
                if (!u.isValid())
                    return false;
                const QString scheme = u.scheme().toLower();
                return scheme == QLatin1String("file") || scheme == QLatin1String("http")
                    || scheme == QLatin1String("https");
            };
            if (trackArtUrl.isEmpty() && !s.isEmpty() && isSchemeAllowed(s)) {
                trackArtUrl = s;
                trackArtUrlResolved = true;
                changed = true;
            } else if (!isSchemeAllowed(s)) {
                // Scheme-rejected URL (including the explicit empty
                // string MPRIS-compliant "no art for this track" form)
                // is a settled answer for this trackid; mark resolved so
                // the post-construction retry loop short-circuits
                // instead of re-fetching for the full 3 s window.
                trackArtUrlResolved = true;
            }
        }

        if (meta.contains(QStringLiteral("xesam:artist"))) {
            const QVariant artistVar = meta.value(QStringLiteral("xesam:artist"));
            QString artistStr;
            auto joinList = [](const QVariantList& items) {
                QStringList parts;
                parts.reserve(items.size());
                for (const QVariant& v : items)
                    parts.append(v.toString());
                return parts.join(QStringLiteral(", "));
            };
            if (artistVar.canConvert<QDBusVariant>()) {
                QVariant inner = artistVar.value<QDBusVariant>().variant();
                if (inner.typeId() == QMetaType::QStringList)
                    artistStr = inner.toStringList().join(QStringLiteral(", "));
                else if (inner.typeId() == QMetaType::QVariantList)
                    artistStr = joinList(inner.toList());
                else if (inner.canConvert<QDBusArgument>())
                    // Mirror demarshallMetadata's double-wrapped case:
                    // a QDBusVariant whose inner is itself a
                    // QDBusArgument (rare but observed on some
                    // proxies). qdbus_cast<QStringList> handles the
                    // `as` signature.
                    artistStr = qdbus_cast<QStringList>(inner.value<QDBusArgument>()).join(QStringLiteral(", "));
                else
                    artistStr = inner.toString();
            } else if (artistVar.typeId() == QMetaType::QStringList) {
                artistStr = artistVar.toStringList().join(QStringLiteral(", "));
            } else if (artistVar.typeId() == QMetaType::QVariantList) {
                // Some web-bridge players publish artist as a{sa} which
                // demarshals to QVariantList rather than QStringList.
                artistStr = joinList(artistVar.toList());
            } else if (artistVar.canConvert<QDBusArgument>()) {
                artistStr = qdbus_cast<QStringList>(artistVar.value<QDBusArgument>()).join(QStringLiteral(", "));
            } else {
                artistStr = artistVar.toString();
            }
            artistStr.truncate(kMaxMetaStringChars);
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
            // Invalidated properties carry no value, re-fetch the whole
            // interface asynchronously to pick them up. Coalesce bursts:
            // skip dispatch if a refresh for this interface is already
            // in flight.
            if (!invalidated.isEmpty() && !refreshPendingRoot) {
                refreshPendingRoot = true;
                getAll(kRootIface, &Private::applyRoot, &refreshPendingRoot);
            }
        } else if (iface == QLatin1String(kPlayerIface)) {
            applyPlayer(changed);
            if (!invalidated.isEmpty() && !refreshPendingPlayer) {
                refreshPendingPlayer = true;
                getAll(kPlayerIface, &Private::applyPlayerFromGetAll, &refreshPendingPlayer);
            }
        }
    }

    void onSeeked(qint64 posUs)
    {
        positionUs = posUs;
        // The reported position is now authoritative, restart the
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
            // Async resync, emits positionChanged itself if the value moved.
            requestPosition();
        } else {
            // Negative rate (reverse playback) can drive positionUs
            // below zero between resyncs; floor at 0 so QML sliders
            // bound to position() never observe a negative value.
            // Only emit positionChanged if the value actually moved
            // (rate=0 or floor-pinned reverse playback both produce
            // no-change ticks that would otherwise churn bindings).
            const qint64 prev = positionUs;
            positionUs = std::max<qint64>(0, positionUs + static_cast<qint64>(rate * kPositionPollMs * 1000));
            if (positionUs != prev)
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

    // bus.connect returns false on failure (broken bus, permission
    // denied). Without these subscriptions the player is a permanently
    // empty stub: PropertiesChanged never reaches us, so volume / track
    // / status stay at construction defaults. Log so the symptom isn't
    // "the media widget never updates" with zero diagnostic.
    const bool propsOk = QDBusConnection::sessionBus().connect(
        serviceName, QLatin1String(kMprisPath), QLatin1String(kPropsIface), QStringLiteral("PropertiesChanged"), this,
        SLOT(_q_onPropertiesChanged(QString, QVariantMap, QStringList)));
    if (!propsOk)
        qCWarning(lcMprisPlayer) << "PropertiesChanged subscription failed for" << serviceName;

    const bool seekedOk =
        QDBusConnection::sessionBus().connect(serviceName, QLatin1String(kMprisPath), QLatin1String(kPlayerIface),
                                              QStringLiteral("Seeked"), this, SLOT(_q_onSeeked(qlonglong)));
    if (!seekedOk)
        qCWarning(lcMprisPlayer) << "Seeked subscription failed for" << serviceName;

    // Async initial fetch, properties populate as the replies land; the
    // GUI thread is never blocked waiting on a slow player.
    d->getAll(kRootIface, &Private::applyRoot);
    d->getAll(kPlayerIface, &Private::applyPlayerFromGetAll);

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
    //     PropertiesChanged until the page changes metadata, so we
    //     have to poll instead of relying on the signal.
    //   - Plain Spotify-Web-via-plasma-browser-integration: similar
    //     to Firefox, file:// URL written after a short delay.
    // Three retries cover the typical 0.5/1.5/3 s windows. Each is a
    // cheap async GetAll; the guard returns early once trackArtUrl
    // is populated. Capped at 3 s after construction.
    for (int delayMs : {500, 1500, 3000}) {
        QTimer::singleShot(delayMs, this, [this]() {
            // Skip if the URL is set OR scheme-rejected: a player whose
            // art URL fails the allowlist will never produce an
            // acceptable URL, so re-fetching for the full window is
            // pointless wire traffic. Also skip if a PropertiesChanged-
            // invalidated refresh is already in flight (the same
            // coalescing gate the invalidation path uses).
            if (d->trackArtUrlResolved || !d->trackArtUrl.isEmpty() || d->playbackState == Stopped
                || d->refreshPendingPlayer)
                return;
            d->refreshPendingPlayer = true;
            d->getAll(kPlayerIface, &Private::applyPlayerFromGetAll, &d->refreshPendingPlayer);
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
    // Reject non-finite input at the boundary; a NaN/inf would be
    // marshalled straight onto the bus to the player. Clamp to the
    // MPRIS-spec range [0.0, 1.0]: a scroll-wheel widget that
    // accumulates past 1.0 otherwise gets silently rejected by the
    // player or accepted at unsafe gain.
    //
    // Volume IS updated optimistically (unlike setShuffle/setLoopState).
    // A scroll-wheel volume handler typically reads `player.volume`,
    // adds a delta, and writes back per scroll tick. Without the
    // optimistic local emit, multiple wheel events arriving within a
    // single bus round-trip (~10-50 ms on a busy bus) all read the
    // same stale value, so the user appears unable to scroll past one
    // tick until the player's PropertiesChanged echo arrives. The
    // PropertiesChanged Volume branch in applyPlayer uses qFuzzyCompare
    // before emitting, so a player rejecting or soft-clamping the
    // change still self-corrects on the bus echo. Shuffle/loop are
    // discrete toggles with no pile-up risk, so they stay echo-driven.
    if (!std::isfinite(v))
        return;
    v = std::clamp(v, 0.0, 1.0);
    setRealField(d->volume, v, &MprisPlayer::volumeChanged, this);
    dbusSetProperty(d->bus, this, d->service, kPlayerIface, "Volume", v);
}
void MprisPlayer::setShuffle(bool s)
{
    dbusSetProperty(d->bus, this, d->service, kPlayerIface, "Shuffle", s);
}

void MprisPlayer::setLoopState(LoopState state)
{
    QString str = QStringLiteral("None");
    if (state == LoopTrack)
        str = QStringLiteral("Track");
    else if (state == LoopPlaylist)
        str = QStringLiteral("Playlist");
    dbusSetProperty(d->bus, this, d->service, kPlayerIface, "LoopStatus", str);
}

void MprisPlayer::play()
{
    dbusCall(d->bus, this, d->service, kPlayerIface, "Play");
}
void MprisPlayer::pause()
{
    dbusCall(d->bus, this, d->service, kPlayerIface, "Pause");
}
void MprisPlayer::stop()
{
    dbusCall(d->bus, this, d->service, kPlayerIface, "Stop");
}
void MprisPlayer::togglePlaying()
{
    dbusCall(d->bus, this, d->service, kPlayerIface, "PlayPause");
}
void MprisPlayer::next()
{
    dbusCall(d->bus, this, d->service, kPlayerIface, "Next");
}
void MprisPlayer::previous()
{
    dbusCall(d->bus, this, d->service, kPlayerIface, "Previous");
}

void MprisPlayer::seek(qreal offsetSeconds)
{
    // A non-finite offset would make the static_cast<qint64> below UB.
    // Cap at the largest second-count that survives the * 1e6
    // microsecond conversion without overflowing qint64 (INT64_MAX is
    // about 9.22e18; 1e13 * 1e6 = 1e19 already overflows, so the safe
    // limit is ~9e12 seconds, about 285 millennia, well beyond any
    // legitimate seek). Short-circuit `||` evaluates `isfinite` first,
    // so `std::abs(NaN)` never reaches the cap compare.
    constexpr qreal kMaxSeekSeconds = 9.0e12;
    if (!std::isfinite(offsetSeconds) || std::abs(offsetSeconds) > kMaxSeekSeconds) {
        qCDebug(lcMprisPlayer) << "seek dropped: invalid offset" << offsetSeconds << "for" << d->service;
        return;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(d->service, QLatin1String(kMprisPath),
                                                      QLatin1String(kPlayerIface), QStringLiteral("Seek"));
    msg << static_cast<qint64>(offsetSeconds * 1e6);
    watchPendingCall(d->bus.asyncCall(msg), this, d->service, "Seek");
}

void MprisPlayer::setPosition(qreal absoluteSeconds)
{
    // Same overflow rationale as seek().
    constexpr qreal kMaxPositionSeconds = 9.0e12;
    if (!std::isfinite(absoluteSeconds) || std::abs(absoluteSeconds) > kMaxPositionSeconds) {
        qCDebug(lcMprisPlayer) << "setPosition dropped: invalid value" << absoluteSeconds << "for" << d->service;
        return;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(d->service, QLatin1String(kMprisPath),
                                                      QLatin1String(kPlayerIface), QStringLiteral("SetPosition"));
    // If trackId is empty we have not seen any Metadata yet; the spec
    // requires SetPosition's TrackId arg to match the currently
    // playing track, so the call would be ignored anyway. Log the
    // drop so the UI side (which has no signal for it) can be
    // debugged from the bus log.
    if (d->trackId.isEmpty()) {
        qCDebug(lcMprisPlayer) << "setPosition dropped: no trackId yet for" << d->service;
        return;
    }
    msg << QVariant::fromValue(QDBusObjectPath(d->trackId)) << static_cast<qint64>(absoluteSeconds * 1e6);
    watchPendingCall(d->bus.asyncCall(msg), this, d->service, "SetPosition");
}

void MprisPlayer::raise()
{
    dbusCall(d->bus, this, d->service, kRootIface, "Raise");
}
void MprisPlayer::quit()
{
    dbusCall(d->bus, this, d->service, kRootIface, "Quit");
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

} // namespace PhosphorServiceMpris
