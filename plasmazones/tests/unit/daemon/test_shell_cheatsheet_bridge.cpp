// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "daemon/controllers/shellcheatsheetbridge.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <functional>
#include <utility>

using PlasmaZones::ShellCheatsheetBridge;

class TestShellCheatsheetBridge : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_directory;
    QLocalServer m_server;
    QList<QJsonObject> m_calls;
    QList<QPointer<QLocalSocket>> m_peers;
    std::function<void(QLocalSocket*, const QJsonObject&)> m_handler;

    QString path() const
    {
        return m_directory.filePath(QStringLiteral("shell.sock"));
    }
    static void reply(QLocalSocket* peer, const QJsonObject& request, bool accepted = true)
    {
        peer->write(QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("reply")},
                                              {QStringLiteral("id"), request.value(QStringLiteral("id"))},
                                              {QStringLiteral("result"), accepted}})
                        .toJson(QJsonDocument::Compact)
                    + '\n');
        peer->flush();
    }

private Q_SLOTS:
    void init()
    {
        QVERIFY(m_directory.isValid());
        QVERIFY(m_server.listen(path()));
        m_calls.clear();
        m_handler = {};
        connect(&m_server, &QLocalServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                auto* peer = m_server.nextPendingConnection();
                m_peers.append(peer);
                connect(peer, &QLocalSocket::readyRead, this, [this, peer] {
                    while (peer->canReadLine()) {
                        const auto request = QJsonDocument::fromJson(peer->readLine()).object();
                        m_calls.append(request);
                        if (m_handler)
                            m_handler(peer, request);
                    }
                });
            }
        });
    }
    void cleanup()
    {
        m_server.disconnect(this);
        m_server.close();
        for (const auto& peer : std::as_const(m_peers)) {
            if (peer) {
                peer->disconnect(this);
                peer->abort();
                peer->deleteLater();
            }
        }
        m_peers.clear();
    }
    void absentShellRequestsTheCapturedFallbackScreen()
    {
        ShellCheatsheetBridge bridge(m_directory.filePath(QStringLiteral("absent.sock")), 100);
        QSignalSpy fallback(&bridge, &ShellCheatsheetBridge::fallbackRequested);
        bridge.toggle(QStringLiteral("monitor/vs:2"), QStringLiteral("DP-1"));
        QTRY_COMPARE(fallback.size(), 1);
        QCOMPARE(fallback.first().first().toString(), QStringLiteral("monitor/vs:2"));
    }
    void acceptedReplyUsesTheRegisteredScreenTarget()
    {
        ShellCheatsheetBridge bridge(path(), 200);
        QSignalSpy accepted(&bridge, &ShellCheatsheetBridge::accepted);
        QSignalSpy fallback(&bridge, &ShellCheatsheetBridge::fallbackRequested);
        m_handler = [](QLocalSocket* peer, const QJsonObject& request) {
            auto stale = request;
            stale[QStringLiteral("id")] = request.value(QStringLiteral("id")).toInt() + 10;
            reply(peer, stale, false);
            reply(peer, request);
        };
        bridge.toggle(QStringLiteral("monitor"), QStringLiteral("DP-2"));
        QTRY_COMPARE(accepted.size(), 1);
        QCOMPARE(fallback.size(), 0);
        QCOMPARE(m_calls.first().value(QStringLiteral("target")).toString(), QStringLiteral("cheatsheet"));
        QCOMPARE(m_calls.first().value(QStringLiteral("fn")).toString(), QStringLiteral("toggleForScreen"));
        QCOMPARE(m_calls.first().value(QStringLiteral("args")).toArray().first().toString(), QStringLiteral("DP-2"));
    }
    void oldShellWithoutTheTargetFallsBack()
    {
        ShellCheatsheetBridge bridge(path(), 200);
        QSignalSpy fallback(&bridge, &ShellCheatsheetBridge::fallbackRequested);
        m_handler = [](QLocalSocket* peer, const QJsonObject& request) {
            const QJsonObject error{{QStringLiteral("type"), QStringLiteral("error")},
                                    {QStringLiteral("id"), request.value(QStringLiteral("id"))},
                                    {QStringLiteral("code"), QStringLiteral("NO_SUCH_FN")}};
            peer->write(QJsonDocument(error).toJson(QJsonDocument::Compact) + '\n');
        };
        bridge.toggle(QStringLiteral("monitor"), QStringLiteral("DP-1"));
        QTRY_COMPARE(fallback.size(), 1);
    }
    void twoTogglesAreSerializedAndNotReplacedByLateReplies()
    {
        ShellCheatsheetBridge bridge(path(), 500);
        QSignalSpy accepted(&bridge, &ShellCheatsheetBridge::accepted);
        bridge.toggle(QStringLiteral("monitor"), QStringLiteral("DP-1"));
        bridge.toggle(QStringLiteral("monitor"), QStringLiteral("DP-1"));
        QTRY_COMPARE(m_calls.size(), 1);
        reply(m_peers.first(), m_calls.first());
        QTRY_COMPARE(m_calls.size(), 2);
        QCOMPARE(accepted.size(), 1);
        reply(m_peers.last(), m_calls.first(), false);
        reply(m_peers.last(), m_calls.last());
        QTRY_COMPARE(accepted.size(), 2);
    }
    void pairedUnsentTogglesCancelEachOther()
    {
        ShellCheatsheetBridge bridge(path(), 200);
        QSignalSpy accepted(&bridge, &ShellCheatsheetBridge::accepted);
        bridge.toggle(QStringLiteral("monitor"), QStringLiteral("DP-1"));
        bridge.toggle(QStringLiteral("monitor"), QStringLiteral("DP-1"));
        bridge.toggle(QStringLiteral("monitor"), QStringLiteral("DP-1"));
        QTRY_COMPARE(m_calls.size(), 1);
        reply(m_peers.first(), m_calls.first());
        QTRY_COMPARE(accepted.size(), 1);
        QTRY_COMPARE(bridge.findChildren<QLocalSocket*>().size(), 0);
        QCOMPARE(m_calls.size(), 1);
    }
    void timeoutAfterDispatchDoesNotCreateASecondPopup()
    {
        ShellCheatsheetBridge bridge(path(), 50);
        QSignalSpy fallback(&bridge, &ShellCheatsheetBridge::fallbackRequested);
        QSignalSpy accepted(&bridge, &ShellCheatsheetBridge::accepted);
        bridge.toggle(QStringLiteral("monitor"), QStringLiteral("DP-1"));
        QTRY_COMPARE(m_calls.size(), 1);
        QTRY_COMPARE(bridge.findChildren<QLocalSocket*>().size(), 0);
        QCOMPARE(fallback.size(), 0);
        QCOMPARE(accepted.size(), 0);
    }
    void disconnectedOwnerCannotCompleteTheNextRequest()
    {
        ShellCheatsheetBridge bridge(path(), 200);
        QSignalSpy fallback(&bridge, &ShellCheatsheetBridge::fallbackRequested);
        QSignalSpy accepted(&bridge, &ShellCheatsheetBridge::accepted);
        bridge.toggle(QStringLiteral("monitor"), QStringLiteral("DP-1"));
        QTRY_COMPARE(m_calls.size(), 1);
        m_peers.first()->abort();
        QTRY_COMPARE(bridge.findChildren<QLocalSocket*>().size(), 0);
        QCOMPARE(fallback.size(), 0);
        m_handler = [](QLocalSocket* peer, const QJsonObject& request) {
            reply(peer, request);
        };
        bridge.toggle(QStringLiteral("monitor"), QStringLiteral("DP-1"));
        QTRY_COMPARE(accepted.size(), 1);
        QCOMPARE(m_calls.size(), 2);
    }
    void cancellationDropsQueuedRequestsWithoutFallback()
    {
        ShellCheatsheetBridge bridge(path(), 200);
        QSignalSpy fallback(&bridge, &ShellCheatsheetBridge::fallbackRequested);
        bridge.toggle(QStringLiteral("monitor"), QStringLiteral("DP-1"));
        bridge.toggle(QStringLiteral("other"), QStringLiteral("DP-2"));
        QTRY_COMPARE(m_calls.size(), 1);
        bridge.cancel();
        QTRY_COMPARE(bridge.findChildren<QLocalSocket*>().size(), 0);
        QCOMPARE(m_calls.size(), 1);
        QCOMPARE(fallback.size(), 0);
    }
};

QTEST_GUILESS_MAIN(TestShellCheatsheetBridge)
#include "test_shell_cheatsheet_bridge.moc"
