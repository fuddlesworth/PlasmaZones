// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePipeWire/PipeWireConnection.h>

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
};

QTEST_MAIN(TestSmoke)
#include "test_smoke.moc"
