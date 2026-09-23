// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceMpris/MprisHost.h>
#include <PhosphorServiceMpris/MprisPlayer.h>
#include <PhosphorServiceMpris/MprisPlayerModel.h>

#include <QCoreApplication>
#include <QtTest/QtTest>

class TestSmoke : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void hostConstructsAndExposesInitialState()
    {
        // CI runners have no MPRIS players running, so we can't assert
        // anything about discovery; just that construction is non-blocking
        // and the initial state is sane.
        PhosphorServiceMpris::MprisHost host;
        QCOMPARE(host.playerCount(), 0);
        QVERIFY(host.players().isEmpty());
        QVERIFY(host.playerAt(0) == nullptr);
        QVERIFY(host.playerAt(-1) == nullptr);
    }

    void modelWithoutHostIsEmpty()
    {
        PhosphorServiceMpris::MprisPlayerModel model;
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!model.host());
    }

    void modelExposesContractRoles()
    {
        PhosphorServiceMpris::MprisPlayerModel model;
        const auto roles = model.roleNames();
        QVERIFY(roles.contains(PhosphorServiceMpris::MprisPlayerModel::PlayerRole));
        QVERIFY(roles.contains(PhosphorServiceMpris::MprisPlayerModel::IdentityRole));
        QVERIFY(roles.contains(PhosphorServiceMpris::MprisPlayerModel::PlaybackStateRole));
        QVERIFY(roles.contains(PhosphorServiceMpris::MprisPlayerModel::TrackTitleRole));
        QVERIFY(roles.contains(PhosphorServiceMpris::MprisPlayerModel::TrackArtistRole));
        QVERIFY(roles.contains(PhosphorServiceMpris::MprisPlayerModel::ArtUrlRole));
        // Pin role-name strings, they're the QML delegate contract.
        QCOMPARE(roles[PhosphorServiceMpris::MprisPlayerModel::PlayerRole], QByteArrayLiteral("player"));
        QCOMPARE(roles[PhosphorServiceMpris::MprisPlayerModel::IdentityRole], QByteArrayLiteral("identity"));
        QCOMPARE(roles[PhosphorServiceMpris::MprisPlayerModel::PlaybackStateRole], QByteArrayLiteral("playbackState"));
        QCOMPARE(roles[PhosphorServiceMpris::MprisPlayerModel::TrackTitleRole], QByteArrayLiteral("trackTitle"));
        QCOMPARE(roles[PhosphorServiceMpris::MprisPlayerModel::TrackArtistRole], QByteArrayLiteral("trackArtist"));
        QCOMPARE(roles[PhosphorServiceMpris::MprisPlayerModel::ArtUrlRole], QByteArrayLiteral("artUrl"));
    }

    void modelAttachesAndDetachesHost()
    {
        PhosphorServiceMpris::MprisPlayerModel model;
        PhosphorServiceMpris::MprisHost host;
        QSignalSpy hostSpy(&model, &PhosphorServiceMpris::MprisPlayerModel::hostChanged);
        QSignalSpy countSpy(&model, &PhosphorServiceMpris::MprisPlayerModel::countChanged);
        model.setHost(&host);
        QCOMPARE(model.host(), &host);
        QCOMPARE(hostSpy.count(), 1);
        // CI runs with no MPRIS players on the bus, so both old and new
        // row sets are empty. countChanged should NOT fire on a 0 -> 0
        // attach per the "only emit on change" contract.
        QCOMPARE(countSpy.count(), 0);

        model.setHost(nullptr);
        QCOMPARE(model.host(), nullptr);
        QCOMPARE(hostSpy.count(), 2);
        // Same reasoning for detach (0 -> 0).
        QCOMPARE(countSpy.count(), 0);
    }

    void mprisEnumValuesArePublicContract()
    {
        // PlaybackState values are used as wire constants across QML
        // delegates and any external binding consumers; pin them.
        QCOMPARE(static_cast<int>(PhosphorServiceMpris::MprisPlayer::Stopped), 0);
        QCOMPARE(static_cast<int>(PhosphorServiceMpris::MprisPlayer::Playing), 1);
        QCOMPARE(static_cast<int>(PhosphorServiceMpris::MprisPlayer::Paused), 2);

        QCOMPARE(static_cast<int>(PhosphorServiceMpris::MprisPlayer::LoopNone), 0);
        QCOMPARE(static_cast<int>(PhosphorServiceMpris::MprisPlayer::LoopTrack), 1);
        QCOMPARE(static_cast<int>(PhosphorServiceMpris::MprisPlayer::LoopPlaylist), 2);
    }
};

QTEST_MAIN(TestSmoke)
#include "test_smoke.moc"
