// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceMpris/phosphorservicempris_export.h>

#include <QObject>
#include <QString>

#include <memory>

namespace PhosphorServiceMpris {

class PHOSPHORSERVICEMPRIS_EXPORT MprisPlayer : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MprisPlayer)

    Q_PROPERTY(QString identity READ identity NOTIFY identityChanged)
    Q_PROPERTY(QString desktopEntry READ desktopEntry NOTIFY desktopEntryChanged)
    Q_PROPERTY(QString serviceName READ serviceName CONSTANT)
    Q_PROPERTY(PlaybackState playbackState READ playbackState NOTIFY playbackStateChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playbackStateChanged)
    Q_PROPERTY(QString trackTitle READ trackTitle NOTIFY metadataChanged)
    Q_PROPERTY(QString trackArtist READ trackArtist NOTIFY metadataChanged)
    Q_PROPERTY(QString trackAlbum READ trackAlbum NOTIFY metadataChanged)
    Q_PROPERTY(QString trackArtUrl READ trackArtUrl NOTIFY metadataChanged)
    Q_PROPERTY(qreal position READ position NOTIFY positionChanged)
    Q_PROPERTY(qreal length READ length NOTIFY metadataChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(qreal rate READ rate NOTIFY rateChanged)
    Q_PROPERTY(LoopState loopState READ loopState WRITE setLoopState NOTIFY loopStateChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    Q_PROPERTY(bool canPlay READ canPlay NOTIFY canPlayChanged)
    Q_PROPERTY(bool canPause READ canPause NOTIFY canPauseChanged)
    Q_PROPERTY(bool canSeek READ canSeek NOTIFY canSeekChanged)
    Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY canGoNextChanged)
    Q_PROPERTY(bool canGoPrevious READ canGoPrevious NOTIFY canGoPreviousChanged)
    Q_PROPERTY(bool canControl READ canControl NOTIFY canControlChanged)

public:
    enum PlaybackState {
        Stopped,
        Playing,
        Paused
    };
    Q_ENUM(PlaybackState)

    enum LoopState {
        LoopNone,
        LoopTrack,
        LoopPlaylist
    };
    Q_ENUM(LoopState)

    explicit MprisPlayer(const QString& serviceName, QObject* parent = nullptr);
    ~MprisPlayer() override;

    [[nodiscard]] QString identity() const;
    [[nodiscard]] QString desktopEntry() const;
    [[nodiscard]] QString serviceName() const;
    [[nodiscard]] PlaybackState playbackState() const;
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] QString trackTitle() const;
    [[nodiscard]] QString trackArtist() const;
    [[nodiscard]] QString trackAlbum() const;
    [[nodiscard]] QString trackArtUrl() const;
    [[nodiscard]] qreal position() const;
    [[nodiscard]] qreal length() const;
    [[nodiscard]] qreal volume() const;
    void setVolume(qreal volume);
    [[nodiscard]] qreal rate() const;
    [[nodiscard]] LoopState loopState() const;
    void setLoopState(LoopState state);
    [[nodiscard]] bool shuffle() const;
    void setShuffle(bool shuffle);
    [[nodiscard]] bool canPlay() const;
    [[nodiscard]] bool canPause() const;
    [[nodiscard]] bool canSeek() const;
    [[nodiscard]] bool canGoNext() const;
    [[nodiscard]] bool canGoPrevious() const;
    [[nodiscard]] bool canControl() const;

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void togglePlaying();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qreal offsetSeconds);
    Q_INVOKABLE void setPosition(qreal absoluteSeconds);
    Q_INVOKABLE void raise();
    Q_INVOKABLE void quit();

Q_SIGNALS:
    void identityChanged();
    void desktopEntryChanged();
    void playbackStateChanged();
    void metadataChanged();
    void positionChanged();
    void volumeChanged();
    void rateChanged();
    void loopStateChanged();
    void shuffleChanged();
    void canPlayChanged();
    void canPauseChanged();
    void canSeekChanged();
    void canGoNextChanged();
    void canGoPreviousChanged();
    void canControlChanged();

private Q_SLOTS:
    void _q_onPropertiesChanged(const QString& iface, const QVariantMap& changed, const QStringList& invalidated);
    void _q_onSeeked(qlonglong position);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceMpris
