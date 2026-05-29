// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PwNode.h>

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
            // disconnect() should fire a nodeRemoved per node before
            // tearing the registry down.
            const int expectedRemovals = nodes.size();
            conn.disconnect();
            QTest::qWait(150);
            QCOMPARE(removedSpy.count(), expectedRemovals);
            QCOMPARE(conn.nodes().size(), 0);
        } else {
            // No daemon — confirm the absence is graceful: no nodes,
            // no spurious adds.
            QCOMPARE(addedSpy.count(), 0);
            QCOMPARE(conn.nodes().size(), 0);
        }
        Q_UNUSED(connectedSpy);
    }
};

QTEST_MAIN(TestSmoke)
#include "test_smoke.moc"
