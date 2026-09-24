// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePolkit/PolkitAgent.h>

#include "authenticationsession_p.h"

#include <QPointer>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <memory>
#include <utility>

namespace PhosphorServicePolkit {

class FakeSession : public detail::AuthenticationSession
{
public:
    explicit FakeSession(int identity, QObject* parent)
        : AuthenticationSession(parent)
        , identity(identity)
    {
    }
    void initiate() override
    {
        started = true;
    }
    void respond(const QString&) override
    {
        ++responses;
    }
    void cancel() override
    {
        cancelled = true;
        Q_EMIT completed(false);
    }
    void ask(const QString& prompt = QStringLiteral("Password:"), bool echo = false)
    {
        Q_EMIT requested(prompt, echo);
    }
    void finish(bool gained)
    {
        Q_EMIT completed(gained);
    }
    void failMessage(const QString& text)
    {
        Q_EMIT error(text);
    }
    void inform(const QString& text)
    {
        Q_EMIT info(text);
    }
    int identity;
    int responses = 0;
    bool started = false;
    bool cancelled = false;
};

class PolkitAgentTest : public QObject
{
    Q_OBJECT
    struct Rig
    {
        int completions = 0;
        QList<QPointer<FakeSession>> sessions;
        PolkitAgent agent;
    };
    static void begin(Rig& rig, const QString& cookie = QStringLiteral("fixture-cookie"),
                      const QStringList& identities = {QStringLiteral("unix-user:alice"),
                                                       QStringLiteral("unix-user:root")})
    {
        rig.agent.beginRequest(
            QStringLiteral("test.action"), QStringLiteral("Allow the fixture action"),
            QStringLiteral("dialog-password"), {{QStringLiteral("program"), QStringLiteral("/fixture/app")}}, cookie,
            identities,
            [&rig](int identity, QObject* parent) {
                auto* session = new FakeSession(identity, parent);
                rig.sessions.append(session);
                return session;
            },
            [&rig] {
                ++rig.completions;
            });
    }

private Q_SLOTS:
    void responsesRequireAnOutstandingPrompt()
    {
        Rig rig;
        begin(rig);
        QCOMPARE(rig.agent.phase(), PolkitAgent::Phase::Idle);
        rig.agent.respond(QStringLiteral("ignored"));
        rig.agent.authenticate();
        QCOMPARE(rig.sessions.size(), 1);
        auto* session = rig.sessions.last().data();
        QVERIFY(session->started);
        QVERIFY(rig.agent.busy());
        QVERIFY(!rig.agent.inputReady());
        rig.agent.respond(QStringLiteral("ignored"));
        QCOMPARE(session->responses, 0);
        session->ask();
        QVERIFY(rig.agent.inputReady());
        QVERIFY(!rig.agent.busy());
        rig.agent.respond(QStringLiteral("fixture answer"));
        rig.agent.respond(QStringLiteral("duplicate"));
        QCOMPARE(session->responses, 1);
        QCOMPARE(rig.agent.phase(), PolkitAgent::Phase::Checking);
        QVERIFY(!rig.agent.inputReady());
        QCOMPARE(rig.completions, 0);
    }

    void identicalAndEchoedPromptsStillEmitAnInputEvent()
    {
        Rig rig;
        begin(rig);
        rig.agent.authenticate();
        QSignalSpy prompts(&rig.agent, &PolkitAgent::promptRequested);
        QSignalSpy propertyChanges(rig.agent.activeRequest(), &AuthRequest::promptChanged);
        auto* session = rig.sessions.last().data();
        session->ask();
        rig.agent.respond(QStringLiteral("fixture answer"));
        session->ask();
        QCOMPARE(prompts.count(), 2);
        QCOMPARE(propertyChanges.count(), 1);
        QVERIFY(rig.agent.inputReady());
        rig.agent.respond(QStringLiteral("fixture answer"));
        session->ask(QStringLiteral("Verification code:"), true);
        QCOMPARE(prompts.count(), 3);
        QCOMPARE(prompts.last().at(1).toBool(), true);
        QVERIFY(rig.agent.activeRequest()->echo());
        QCOMPARE(rig.agent.activeRequest()->prompt(), QStringLiteral("Verification code:"));
    }

    void failedCredentialSessionsRetryThreeTimesWithoutResolvingEarly()
    {
        Rig rig;
        begin(rig);
        auto* request = rig.agent.activeRequest();
        QSignalSpy completed(&rig.agent, &PolkitAgent::authenticationCompleted);
        QSignalSpy errors(&rig.agent, &PolkitAgent::authenticationError);
        rig.agent.authenticate();
        for (int attempt = 0; attempt < 3; ++attempt) {
            QTRY_COMPARE(rig.sessions.size(), attempt + 1);
            auto* session = rig.sessions.last().data();
            session->ask();
            rig.agent.respond(QStringLiteral("fixture answer"));
            session->failMessage(QStringLiteral("Authentication failure"));
            session->finish(false);
            if (attempt < 2) {
                QCOMPARE(rig.agent.activeRequest(), request);
                QCOMPARE(rig.agent.phase(), PolkitAgent::Phase::Starting);
                QCOMPARE(rig.completions, 0);
                QCOMPARE(completed.count(), 0);
                rig.agent.respond(QStringLiteral("must not be queued"));
            }
        }
        QCOMPARE(errors.count(), 3);
        QCOMPARE(rig.completions, 1);
        QCOMPARE(completed.count(), 1);
        QCOMPARE(completed.first().first().toBool(), false);
        QVERIFY(!rig.agent.activeRequest());
        QCOMPARE(rig.agent.phase(), PolkitAgent::Phase::Failed);
        QVERIFY(!rig.agent.canRetry());
        rig.agent.retry();
        rig.agent.authenticate();
        QCOMPARE(rig.sessions.size(), 3);
    }

    void unavailableSessionsRequireExplicitBoundedRetry()
    {
        Rig rig;
        begin(rig);
        rig.agent.authenticate();
        for (int attempt = 0; attempt < 3; ++attempt) {
            rig.sessions.last()->finish(false);
            if (attempt < 2) {
                QCOMPARE(rig.agent.phase(), PolkitAgent::Phase::Unavailable);
                QVERIFY(rig.agent.canRetry());
                QVERIFY(!rig.agent.busy());
                QVERIFY(!rig.agent.inputReady());
                QCOMPARE(rig.completions, 0);
                QCoreApplication::processEvents();
                QCOMPARE(rig.sessions.size(), attempt + 1);
                rig.agent.authenticate();
                QCOMPARE(rig.sessions.size(), attempt + 1);
                rig.agent.retry();
                QCOMPARE(rig.sessions.size(), attempt + 2);
            }
        }
        QCOMPARE(rig.agent.phase(), PolkitAgent::Phase::Failed);
        QCOMPARE(rig.completions, 1);
        QVERIFY(!rig.agent.activeRequest());
    }

    void accountSwitchRetainsRequestAndInvalidatesOldCallbacks()
    {
        Rig rig;
        begin(rig);
        rig.agent.authenticate();
        auto* request = rig.agent.activeRequest();
        auto* previous = rig.sessions.first().data();
        previous->ask();
        QSignalSpy prompts(&rig.agent, &PolkitAgent::promptRequested);
        QSignalSpy errors(&rig.agent, &PolkitAgent::authenticationError);
        QSignalSpy info(&rig.agent, &PolkitAgent::authenticationInfo);
        rig.agent.selectIdentity(1);
        QCOMPARE(rig.agent.activeRequest(), request);
        QCOMPARE(request->cookie(), QStringLiteral("fixture-cookie"));
        QCOMPARE(request->selectedIdentity(), 1);
        QCOMPARE(rig.sessions.size(), 2);
        QCOMPARE(rig.sessions.last()->identity, 1);
        QVERIFY(previous->cancelled);
        QCOMPARE(rig.completions, 0);
        previous->ask(QStringLiteral("Stale prompt"));
        previous->failMessage(QStringLiteral("Stale error"));
        previous->inform(QStringLiteral("Stale info"));
        previous->finish(true);
        QCOMPARE(prompts.count(), 0);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(info.count(), 0);
        QCOMPARE(rig.agent.phase(), PolkitAgent::Phase::Starting);
        rig.agent.selectIdentity(-1);
        rig.agent.selectIdentity(9);
        rig.agent.selectIdentity(1);
        QCOMPARE(rig.sessions.size(), 2);
        rig.sessions.last()->ask();
        rig.agent.respond(QStringLiteral("fixture answer"));
        rig.sessions.last()->finish(true);
        QCOMPARE(rig.completions, 1);
        QCOMPARE(rig.agent.phase(), PolkitAgent::Phase::Success);
        QVERIFY(!rig.agent.activeRequest());
    }

    void directIdentityPropertyChangeAlsoRestartsSafely()
    {
        Rig rig;
        begin(rig);
        rig.agent.authenticate();
        rig.agent.activeRequest()->setSelectedIdentity(1);
        QCOMPARE(rig.sessions.size(), 2);
        QCOMPARE(rig.sessions.last()->identity, 1);
        QCOMPARE(rig.completions, 0);
    }

    void cancelledRequestCompletesOnceAndCannotRetryFromQueuedWork()
    {
        Rig rig;
        begin(rig);
        rig.agent.authenticate();
        auto* session = rig.sessions.last().data();
        session->ask();
        rig.agent.respond(QStringLiteral("fixture answer"));
        session->finish(false);
        QSignalSpy cancelled(&rig.agent, &PolkitAgent::authenticationCancelled);
        rig.agent.cancel();
        rig.agent.cancel();
        QCoreApplication::processEvents();
        QCOMPARE(rig.sessions.size(), 1);
        QCOMPARE(rig.completions, 1);
        QCOMPARE(cancelled.count(), 1);
        QCOMPARE(rig.agent.phase(), PolkitAgent::Phase::Cancelled);
        QVERIFY(!rig.agent.activeRequest());
    }

    void replacementRequestCannotBeCompletedByOldSession()
    {
        Rig rig;
        begin(rig);
        rig.agent.authenticate();
        auto* previous = rig.sessions.last().data();
        begin(rig, QStringLiteral("new-cookie"));
        QCOMPARE(rig.completions, 1);
        QCOMPARE(rig.agent.activeRequest()->cookie(), QStringLiteral("new-cookie"));
        rig.agent.authenticate();
        previous->finish(true);
        QCOMPARE(rig.completions, 1);
        QCOMPARE(rig.agent.phase(), PolkitAgent::Phase::Starting);
        rig.agent.cancel();
        QCOMPARE(rig.completions, 2);
    }

    void stateObserverCanCancelBeforeResponseReachesSession()
    {
        Rig rig;
        begin(rig);
        rig.agent.authenticate();
        auto* session = rig.sessions.last().data();
        session->ask();
        connect(&rig.agent, &PolkitAgent::stateChanged, &rig.agent, [&rig] {
            if (rig.agent.phase() == PolkitAgent::Phase::Checking)
                rig.agent.cancel();
        });
        rig.agent.respond(QStringLiteral("fixture answer"));
        QCOMPARE(session->responses, 0);
        QCOMPARE(rig.completions, 1);
        QVERIFY(!rig.agent.activeRequest());
    }

    void infoAndErrorsDoNotInventReadinessOrSuccess()
    {
        Rig rig;
        begin(rig);
        rig.agent.authenticate();
        QSignalSpy info(&rig.agent, &PolkitAgent::authenticationInfo);
        QSignalSpy errors(&rig.agent, &PolkitAgent::authenticationError);
        rig.sessions.last()->inform(QStringLiteral("Touch your security key"));
        rig.sessions.last()->failMessage(QStringLiteral("Device unavailable"));
        QCOMPARE(info.first().first().toString(), QStringLiteral("Touch your security key"));
        QCOMPARE(errors.first().first().toString(), QStringLiteral("Device unavailable"));
        QVERIFY(!rig.agent.inputReady());
        QCOMPARE(rig.agent.phase(), PolkitAgent::Phase::Starting);
        QCOMPARE(rig.completions, 0);
    }

    void emptyIdentityListFailsWithoutStartingPam()
    {
        Rig rig;
        begin(rig, QStringLiteral("empty-identities"), {});
        rig.agent.authenticate();
        QVERIFY(rig.sessions.isEmpty());
        QCOMPARE(rig.completions, 1);
        QCOMPARE(rig.agent.phase(), PolkitAgent::Phase::Failed);
    }

    void destructionCancelsAndCompletesOutstandingRequest()
    {
        int completions = 0;
        QPointer<FakeSession> session;
        auto agent = std::make_unique<PolkitAgent>();
        agent->beginRequest(
            {}, {}, {}, {}, QStringLiteral("fixture-cookie"), {QStringLiteral("unix-user:alice")},
            [&session](int identity, QObject* parent) {
                session = new FakeSession(identity, parent);
                return session.data();
            },
            [&completions] {
                ++completions;
            });
        agent->authenticate();
        bool cancelled = false;
        connect(session, &detail::AuthenticationSession::completed, this, [&cancelled](bool) {
            cancelled = true;
        });
        agent.reset();
        QVERIFY(cancelled);
        QVERIFY(!session);
        QCOMPARE(completions, 1);
    }

    void reentrantNewRequestWinsOverAnOlderReplacement()
    {
        Rig rig;
        bool replaceOnCompletion = true;
        rig.agent.beginRequest({}, {}, {}, {}, QStringLiteral("original"), {QStringLiteral("unix-user:alice")}, {},
                               [&] {
                                   ++rig.completions;
                                   if (std::exchange(replaceOnCompletion, false))
                                       begin(rig, QStringLiteral("newest"));
                               });
        begin(rig, QStringLiteral("older-replacement"));
        QCOMPARE(rig.completions, 2);
        QCOMPARE(rig.agent.activeRequest()->cookie(), QStringLiteral("newest"));
        rig.agent.cancel();
        QCOMPARE(rig.completions, 3);
    }
};

} // namespace PhosphorServicePolkit

QTEST_GUILESS_MAIN(PhosphorServicePolkit::PolkitAgentTest)
#include "test_lifecycle.moc"
