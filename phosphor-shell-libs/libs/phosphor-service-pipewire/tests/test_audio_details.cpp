// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorServicePipeWire/PipeWireConnection.h>
#include <PhosphorServicePipeWire/PwAudioProbe.h>
#include <PhosphorServicePipeWire/PwNodeModel.h>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace PhosphorServicePipeWire;

// All writes and captured samples belong to a private PipeWire server. The
// policy-only WirePlumber profile never enumerates the host's audio hardware.
class TestAudioDetails : public QObject
{
    Q_OBJECT
    QTemporaryDir sandbox;
    QProcess server;
    QProcess policy;
    QProcess player;
    std::unique_ptr<PipeWireConnection> connection;

    static void stop(QProcess& process)
    {
        if (process.state() == QProcess::NotRunning)
            return;
        process.terminate();
        if (!process.waitForFinished(3000)) {
            process.kill();
            process.waitForFinished();
        }
    }
    QByteArray command(const QString& program, const QStringList& arguments)
    {
        QProcess process;
        process.start(program, arguments);
        if (!process.waitForFinished(5000) || process.exitCode() != 0) {
            QTest::qFail(qPrintable(program + QStringLiteral(": ") + QString::fromUtf8(process.readAllStandardError())),
                         __FILE__, __LINE__);
            return {};
        }
        return process.readAllStandardOutput();
    }
    PwNode* node(const QString& name) const
    {
        for (auto* node : connection->nodes())
            if (node->name() == name)
                return node;
        return nullptr;
    }
    bool linkedTo(quint32 stream, quint32 sink)
    {
        const auto objects = QJsonDocument::fromJson(command(QStringLiteral("pw-dump"), {})).array();
        for (const auto& value : objects) {
            const auto object = value.toObject();
            if (object.value(QStringLiteral("type")).toString() != QLatin1String("PipeWire:Interface:Link"))
                continue;
            const auto info = object.value(QStringLiteral("info")).toObject();
            if (info.value(QStringLiteral("output-node-id")).toInt() == static_cast<int>(stream)
                && info.value(QStringLiteral("input-node-id")).toInt() == static_cast<int>(sink))
                return true;
        }
        return false;
    }
private Q_SLOTS:
    void initTestCase()
    {
        for (const auto& executable : {"pipewire", "wireplumber", "pw-cli", "pw-cat", "pw-dump"})
            if (QStandardPaths::findExecutable(QString::fromLatin1(executable)).isEmpty())
                QSKIP("Private PipeWire integration requires PipeWire tools and WirePlumber");
        QVERIFY(sandbox.isValid());
        const auto config = sandbox.path() + QStringLiteral("/config");
        QVERIFY(QDir().mkpath(config + QStringLiteral("/pipewire/pipewire.conf.d")));
        QFile fragment(config + QStringLiteral("/pipewire/pipewire.conf.d/99-audio-test.conf"));
        QVERIFY(fragment.open(QIODevice::WriteOnly));
        fragment.write("context.spa-libs = { audiotestsrc = audiotestsrc/libspa-audiotestsrc }\n");
        fragment.close();
        qputenv("PIPEWIRE_RUNTIME_DIR", sandbox.path().toUtf8());
        qputenv("PIPEWIRE_REMOTE", "pipewire-0");
        qputenv("XDG_CONFIG_HOME", config.toUtf8());
        qputenv("XDG_STATE_HOME", (sandbox.path() + QStringLiteral("/state")).toUtf8());
        qunsetenv("PIPEWIRE_CONFIG_DIR");
        qunsetenv("PIPEWIRE_CONFIG_NAME");
        server.start(QStringLiteral("pipewire"));
        QVERIFY(server.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(sandbox.path() + QStringLiteral("/pipewire-0")), 5000);
        policy.start(QStringLiteral("wireplumber"), {QStringLiteral("--profile=policy")});
        QVERIFY(policy.waitForStarted());
        for (const auto& name : {QStringLiteral("fixture_a"), QStringLiteral("fixture_b")}) {
            command(QStringLiteral("pw-cli"),
                    {QStringLiteral("create-node"), QStringLiteral("adapter"),
                     QStringLiteral("{ factory.name=support.null-audio-sink node.name=%1 media.class=Audio/Sink "
                                    "audio.position=[FL FR] object.linger=true }")
                         .arg(name)});
        }
        command(QStringLiteral("pw-cli"),
                {QStringLiteral("create-node"), QStringLiteral("adapter"),
                 QStringLiteral("{ factory.name=audiotestsrc node.name=fixture_mic media.class=Audio/Source "
                                "audio.position=[MONO] object.linger=true }")});
        connection = std::make_unique<PipeWireConnection>();
        connection->connectToDaemon();
        QTRY_VERIFY_WITH_TIMEOUT(connection->isConnected(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(node(QStringLiteral("fixture_a")) && node(QStringLiteral("fixture_b"))
                                     && node(QStringLiteral("fixture_mic")),
                                 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!connection->defaultSinkName().isEmpty(), 5000);
        player.start(QStringLiteral("pw-cat"),
                     {QStringLiteral("--playback"), QStringLiteral("--raw"), QStringLiteral("--format=f32"),
                      QStringLiteral("--target=fixture_a"), QStringLiteral("--properties"),
                      QStringLiteral("{ node.name=fixture_player application.name=FixturePlayer "
                                     "application.icon-name=audio-headphones media.name=FixtureMusic }"),
                      QStringLiteral("/dev/zero")});
        QVERIFY(player.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(
            node(QStringLiteral("fixture_player")) && node(QStringLiteral("fixture_player"))->running(), 5000);
    }
    void defaultsVolumeAndMuteFollowServerEcho()
    {
        QSignalSpy errors(connection.get(), &PipeWireConnection::operationFailed);
        connection->setDefaultSink(QStringLiteral("fixture_b"));
        QTRY_COMPARE(connection->defaultSinkName(), QStringLiteral("fixture_b"));
        connection->setDefaultSource(QStringLiteral("fixture_mic"));
        QTRY_COMPARE(connection->defaultSourceName(), QStringLiteral("fixture_mic"));
        auto* sink = node(QStringLiteral("fixture_b"));
        QTRY_VERIFY(!sink->volumes().isEmpty());
        sink->setVolume(0.37);
        QTRY_VERIFY(qAbs(sink->volumes().first() - 0.37) < 0.001);
        sink->setMuted(true);
        QTRY_VERIFY(sink->muted());
        sink->setMuted(false);
        QTRY_VERIFY(!sink->muted());
        QVERIFY(errors.isEmpty());
    }
    void streamRoutingChangesLinksAndCanFollowDefault()
    {
        auto* stream = node(QStringLiteral("fixture_player"));
        QCOMPARE(stream->applicationName(), QStringLiteral("FixturePlayer"));
        QCOMPARE(stream->mediaName(), QStringLiteral("FixtureMusic"));
        QCOMPARE(stream->iconName(), QStringLiteral("audio-headphones"));
        QVERIFY(!stream->serial().isEmpty());
        QVERIFY(stream->canMove());
        connection->setStreamTarget(stream->id(), QStringLiteral("fixture_a"));
        QTRY_COMPARE(stream->targetName(), QStringLiteral("fixture_a"));
        QTRY_VERIFY(linkedTo(stream->id(), node(QStringLiteral("fixture_a"))->id()));
        connection->setStreamTarget(stream->id(), QStringLiteral("fixture_b"));
        QTRY_COMPARE(stream->targetName(), QStringLiteral("fixture_b"));
        QTRY_VERIFY(linkedTo(stream->id(), node(QStringLiteral("fixture_b"))->id()));
        connection->setStreamTarget(stream->id(), {});
        QTRY_COMPARE(stream->targetName(), QStringLiteral("-1"));
        connection->setDefaultSink(QStringLiteral("fixture_a"));
        QTRY_VERIFY(linkedTo(stream->id(), node(QStringLiteral("fixture_a"))->id()));
        QSignalSpy errors(connection.get(), &PipeWireConnection::operationFailed);
        connection->setStreamTarget(stream->id(), QStringLiteral("missing_output"));
        QCOMPARE(errors.size(), 1);
        QCOMPARE(stream->targetName(), QStringLiteral("-1"));
        connection->setStreamTarget(node(QStringLiteral("fixture_mic"))->id(), {});
        QCOMPARE(errors.size(), 2);
    }
    void audioTestsUseRealSamplesAndStopCleanly()
    {
        PwAudioProbe probe;
        QSignalSpy errors(&probe, &PwAudioProbe::error);
        QVERIFY(!probe.listening());
        QVERIFY(!probe.playing());
        QCOMPARE(probe.level(), 0);
        probe.startInputTest(QStringLiteral("fixture_mic"));
        QVERIFY(probe.listening());
        QTRY_VERIFY_WITH_TIMEOUT(probe.level() > 0.01, 5000);
        probe.playTestSound(QStringLiteral("fixture_a"));
        QVERIFY(!probe.listening());
        QVERIFY(probe.playing());
        QCOMPARE(probe.level(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(!probe.playing(), 3000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
        probe.startInputTest(QStringLiteral("fixture_mic"));
        QTRY_VERIFY(probe.level() > 0);
        probe.stop();
        QVERIFY(!probe.listening());
        QCOMPARE(probe.level(), 0);
        auto transient = std::make_unique<PwAudioProbe>();
        transient->startInputTest(QStringLiteral("fixture_mic"));
        transient.reset(); // A queued connection/process callback must not outlive the probe.
        probe.playTestSound(QStringLiteral("missing_output"));
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 7000);
        QVERIFY(!probe.playing());
    }
    void unplugInvalidatesNodesAndRejectsStaleRoutes()
    {
        PwSinkModel sinks;
        sinks.setConnection(connection.get());
        const auto before = sinks.rowCount();
        QPointer<PwNode> sink = node(QStringLiteral("fixture_b"));
        const auto id = sink->id();
        command(QStringLiteral("pw-cli"), {QStringLiteral("destroy"), QString::number(id)});
        QTRY_VERIFY(sink.isNull());
        QTRY_COMPARE(sinks.rowCount(), before - 1);
        QSignalSpy errors(connection.get(), &PipeWireConnection::operationFailed);
        connection->setStreamTarget(node(QStringLiteral("fixture_player"))->id(), QStringLiteral("fixture_b"));
        QCOMPARE(errors.size(), 1);
    }
    void cleanupTestCase()
    {
        connection.reset();
        stop(player);
        stop(policy);
        stop(server);
    }
};
QTEST_GUILESS_MAIN(TestAudioDetails)
#include "test_audio_details.moc"
