// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PipeWireHost.h>
#include <PhosphorServicePipeWire/PwNode.h>
#include <PhosphorServicePipeWire/PwNodeModel.h>

#include <QSignalSpy>
#include <QtTest/QtTest>

class TestSmoke : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Construction must not block on a missing daemon and must leave
    /// the connection in the "no PipeWire, no error yet" baseline so a
    /// QML status indicator binding starts at the right value.
    void constructionDefaults()
    {
        PhosphorServicePipeWire::PipeWireConnection conn;
        QCOMPARE(conn.isConnected(), false);
        QCOMPARE(conn.isDaemonAvailable(), false);
    }

    /// disconnect() before connect() is a documented no-op; calling it
    /// must not emit spurious signals or crash the loop thread.
    void disconnectBeforeConnectIsNoop()
    {
        PhosphorServicePipeWire::PipeWireConnection conn;
        QSignalSpy connSpy(&conn, &PhosphorServicePipeWire::PipeWireConnection::connectedChanged);
        QSignalSpy availSpy(&conn, &PhosphorServicePipeWire::PipeWireConnection::daemonAvailableChanged);

        conn.disconnect();
        // Process any queued events from the loop thread to make sure
        // a misbehaving doDisconnect doesn't leak signals into the GUI
        // queue.
        QTest::qWait(50);

        QCOMPARE(connSpy.count(), 0);
        QCOMPARE(availSpy.count(), 0);
    }

    /// Multiple connect() calls collapse into one effective attempt;
    /// the test asserts at minimum that the calls don't crash. We
    /// cannot pin a `connectedChanged` count without a controlled
    /// PipeWire fixture, so this is a survivability check.
    void connectIdempotent()
    {
        PhosphorServicePipeWire::PipeWireConnection conn;
        conn.connect();
        conn.connect();
        conn.connect();
        QTest::qWait(100);
        QVERIFY(true);
    }

    /// Destruction must join the loop thread cleanly even when a
    /// connect attempt is in flight. The test crashes with use-after-
    /// free or hangs if the teardown sequence in ~PipeWireConnection
    /// regresses.
    void destructionWhileConnectInFlightIsSafe()
    {
        auto conn = std::make_unique<PhosphorServicePipeWire::PipeWireConnection>();
        conn->connect();
        // Drop the unique_ptr without waiting; ~PipeWireConnection has
        // to win the race against the loop thread's pw_context_connect.
        conn.reset();
        QVERIFY(true);
    }

    /// `nodes()` returns an empty list pre-connect. The list type is
    /// QList<PwNode*> by value (snapshot semantics) — verify nothing
    /// throws and the result is the expected shape.
    void nodesEmptyBeforeConnect()
    {
        PhosphorServicePipeWire::PipeWireConnection conn;
        const auto nodes = conn.nodes();
        QCOMPARE(nodes.size(), 0);
    }

    /// Run a real handshake against the local daemon (if present) and
    /// observe registry events. On the developer host this surfaces
    /// every audio sink + source + stream PipeWire reports; CI hosts
    /// without a daemon just stay on the empty list with no signals.
    /// In either case the test crashes or hangs if the registry path
    /// regresses.
    void registryEnumerationSurvivesLiveHandshake()
    {
        PhosphorServicePipeWire::PipeWireConnection conn;
        QSignalSpy addedSpy(&conn, &PhosphorServicePipeWire::PipeWireConnection::nodeAdded);
        QSignalSpy removedSpy(&conn, &PhosphorServicePipeWire::PipeWireConnection::nodeRemoved);
        QSignalSpy connectedSpy(&conn, &PhosphorServicePipeWire::PipeWireConnection::connectedChanged);

        conn.connect();
        // 250ms is plenty for PipeWire's local-socket handshake +
        // initial registry walk on a developer host; on a no-daemon
        // host the wait just times out cleanly.
        QTest::qWait(250);

        if (conn.isConnected()) {
            // Daemon present — registry should have surfaced at least
            // the system's default audio sink. Don't assert the count
            // (varies by host) but every node we received must have a
            // valid audio media class.
            const auto nodes = conn.nodes();
            for (auto* node : nodes) {
                QVERIFY(node != nullptr);
                const QString mc = node->mediaClass();
                QVERIFY2(mc == QLatin1String("Audio/Sink") || mc == QLatin1String("Audio/Source")
                             || mc == QLatin1String("Stream/Output/Audio") || mc == QLatin1String("Stream/Input/Audio"),
                         qPrintable(QStringLiteral("unexpected mediaClass: %1").arg(mc)));
            }
            // disconnect() should fire a nodeRemoved for at least
            // every node we observed. Use >= rather than == because
            // the daemon may add or remove nodes mid-shutdown (a
            // hot-plugged USB sink, a Firefox stream ending) and
            // exact-equality would flake on a live host.
            const int expectedRemovals = nodes.size();
            conn.disconnect();
            QTest::qWait(150);
            QVERIFY2(
                removedSpy.count() >= expectedRemovals,
                qPrintable(QStringLiteral("removed %1, expected >= %2").arg(removedSpy.count()).arg(expectedRemovals)));
            QCOMPARE(conn.nodes().size(), 0);
        } else {
            // No daemon — confirm the absence is graceful: no nodes,
            // no spurious adds.
            QCOMPARE(addedSpy.count(), 0);
            QCOMPARE(conn.nodes().size(), 0);
        }
        Q_UNUSED(connectedSpy);
    }

    /// Pin the role-name → integer mapping. QML consumers key on the
    /// string names, but breaking the int values would silently break
    /// any C++ caller doing `data(idx, PwNodeModel::VolumesRole)`. The
    /// only way to evolve these is intentionally, with a coordinated
    /// QML migration.
    void roleNamesArePinned()
    {
        using PhosphorServicePipeWire::PwNodeModel;
        PwNodeModel model;
        const auto names = model.roleNames();
        QCOMPARE(names.value(PwNodeModel::NodeRole), QByteArrayLiteral("node"));
        QCOMPARE(names.value(PwNodeModel::IdRole), QByteArrayLiteral("id"));
        QCOMPARE(names.value(PwNodeModel::NameRole), QByteArrayLiteral("name"));
        QCOMPARE(names.value(PwNodeModel::NickRole), QByteArrayLiteral("nick"));
        QCOMPARE(names.value(PwNodeModel::DescriptionRole), QByteArrayLiteral("description"));
        QCOMPARE(names.value(PwNodeModel::MediaClassRole), QByteArrayLiteral("mediaClass"));
        QCOMPARE(names.value(PwNodeModel::ChannelCountRole), QByteArrayLiteral("channelCount"));
        QCOMPARE(names.value(PwNodeModel::VolumesRole), QByteArrayLiteral("volumes"));
        QCOMPARE(names.value(PwNodeModel::MutedRole), QByteArrayLiteral("muted"));
        QCOMPARE(names.value(Qt::DisplayRole), QByteArrayLiteral("display"));

        QCOMPARE(int(PwNodeModel::NodeRole), int(Qt::UserRole) + 1);
        QCOMPARE(int(PwNodeModel::IdRole), int(Qt::UserRole) + 2);
        QCOMPARE(int(PwNodeModel::NameRole), int(Qt::UserRole) + 3);
        QCOMPARE(int(PwNodeModel::NickRole), int(Qt::UserRole) + 4);
        QCOMPARE(int(PwNodeModel::DescriptionRole), int(Qt::UserRole) + 5);
        QCOMPARE(int(PwNodeModel::MediaClassRole), int(Qt::UserRole) + 6);
        QCOMPARE(int(PwNodeModel::ChannelCountRole), int(Qt::UserRole) + 7);
        QCOMPARE(int(PwNodeModel::VolumesRole), int(Qt::UserRole) + 8);
        QCOMPARE(int(PwNodeModel::MutedRole), int(Qt::UserRole) + 9);
    }

    /// PwSinkModel / PwSourceModel / PwStreamModel pre-set their
    /// filters; verify the contract so a stray edit in the convenience
    /// constructors trips the test before it reaches a consumer.
    void convenienceSubclassesPinTheirFilters()
    {
        PhosphorServicePipeWire::PwSinkModel sinks;
        PhosphorServicePipeWire::PwSourceModel sources;
        PhosphorServicePipeWire::PwStreamModel streams;
        QCOMPARE(sinks.mediaClasses(), QStringList{QStringLiteral("Audio/Sink")});
        QCOMPARE(sources.mediaClasses(), QStringList{QStringLiteral("Audio/Source")});
        QCOMPARE(streams.mediaClasses(),
                 (QStringList{QStringLiteral("Stream/Output/Audio"), QStringLiteral("Stream/Input/Audio")}));
    }

    /// Write APIs must survive being called for a non-existent node id
    /// and before connect(). The dispatch goes via pw_loop_invoke, so
    /// a buggy implementation would either crash on the unique_ptr
    /// guard or leak the request struct.
    void writeAPIsAreSafeForUnknownTargets()
    {
        PhosphorServicePipeWire::PipeWireConnection conn;
        // Pre-connect writes must early-out cleanly (loop running but
        // no core/registry yet, so the node id lookup will fail).
        conn.writeVolumes(99999, {0.5, 0.5});
        conn.writeMuted(99999, true);
        QTest::qWait(50);
        QVERIFY(true);

        // Post-connect writes for a non-existent id should still be
        // safe — the loop-thread handler logs and returns without
        // touching a proxy.
        conn.connect();
        QTest::qWait(150);
        conn.writeVolumes(99999, {0.5});
        conn.writeMuted(99999, false);
        QTest::qWait(50);
        QVERIFY(true);
    }

    /// PipeWireHost auto-connects on construction and forwards every
    /// PipeWireConnection signal. Verify the forwarding wires + the
    /// connection accessor.
    void hostAutoConnectsAndForwardsSignals()
    {
        PhosphorServicePipeWire::PipeWireHost host;
        QVERIFY(host.connection() != nullptr);
        // Signals must be wired so observers binding to the host see
        // changes without reaching for `.connection.connected`.
        QSignalSpy connSpy(&host, &PhosphorServicePipeWire::PipeWireHost::connectedChanged);
        QSignalSpy availSpy(&host, &PhosphorServicePipeWire::PipeWireHost::daemonAvailableChanged);
        // Host construction kicked off connect() — give it a moment.
        QTest::qWait(250);
        if (host.isConnected()) {
            QVERIFY(connSpy.count() >= 1);
            QVERIFY(availSpy.count() >= 1);
            // Property forwarding matches the connection's truth.
            QCOMPARE(host.defaultSinkName(), host.connection()->defaultSinkName());
        }
        // reconnect() exercises the disconnect → connect cycle.
        host.reconnect();
        QTest::qWait(150);
        QVERIFY(true);
    }

    /// WirePlumber's default metadata should surface a non-empty
    /// default sink name after a successful handshake when running
    /// against a real session-managed PipeWire instance. No-daemon
    /// hosts and bare-daemon (no WirePlumber) hosts both legitimately
    /// stay empty — assert only the daemon-and-WirePlumber-present
    /// path to avoid false failures.
    void defaultSinkNameSurfacesFromWirePlumber()
    {
        PhosphorServicePipeWire::PipeWireConnection conn;
        QSignalSpy sinkSpy(&conn, &PhosphorServicePipeWire::PipeWireConnection::defaultSinkNameChanged);
        conn.connect();
        QTest::qWait(300);
        if (!conn.isConnected())
            return;
        // After handshake + initial registry walk, defaults should
        // have landed if WirePlumber is running. If they didn't, we
        // accept that (bare-daemon edge case) without failing.
        if (!conn.defaultSinkName().isEmpty())
            QVERIFY(sinkSpy.count() >= 1);
    }

    /// Models hooked to a live connection should populate based on the
    /// daemon's actual audio nodes — sinks-only model sees only sinks,
    /// streams model sees only streams, etc. On a no-daemon host the
    /// models stay empty.
    void modelsFilterLiveRegistry()
    {
        using PhosphorServicePipeWire::PwNodeModel;
        PhosphorServicePipeWire::PipeWireConnection conn;
        PhosphorServicePipeWire::PwSinkModel sinks;
        PhosphorServicePipeWire::PwSourceModel sources;
        PhosphorServicePipeWire::PwStreamModel streams;
        sinks.setConnection(&conn);
        sources.setConnection(&conn);
        streams.setConnection(&conn);

        conn.connect();
        QTest::qWait(300);

        if (!conn.isConnected())
            return; // No daemon — nothing more to assert.

        // Every row of each model must match the model's filter.
        for (int i = 0; i < sinks.rowCount(); ++i) {
            QCOMPARE(sinks.data(sinks.index(i), PwNodeModel::MediaClassRole).toString(), QStringLiteral("Audio/Sink"));
        }
        for (int i = 0; i < sources.rowCount(); ++i) {
            QCOMPARE(sources.data(sources.index(i), PwNodeModel::MediaClassRole).toString(),
                     QStringLiteral("Audio/Source"));
        }
        for (int i = 0; i < streams.rowCount(); ++i) {
            const QString mc = streams.data(streams.index(i), PwNodeModel::MediaClassRole).toString();
            QVERIFY2(mc == QLatin1String("Stream/Output/Audio") || mc == QLatin1String("Stream/Input/Audio"),
                     qPrintable(mc));
        }
    }
};

QTEST_MAIN(TestSmoke)
#include "test_smoke.moc"
