// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePolkit/PolkitAgent.h>

#include <PhosphorServicePolkit/AuthRequest.h>

#include "polkitdecode.h"

#include <polkitqt1-agent-listener.h>
#include <polkitqt1-agent-session.h>
#include <polkitqt1-details.h>
#include <polkitqt1-identity.h>
#include <polkitqt1-subject.h>

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QStringList>
#include <QVariantMap>

namespace {
constexpr auto kDefaultObjectPath = "/org/phosphor/PolicyKit1/AuthenticationAgent";

Q_LOGGING_CATEGORY(lcPolkitAgent, "phosphor.service.polkit")
} // namespace

namespace PhosphorServicePolkit {

// Internal polkit Listener. Wraps the polkit-qt agent callback surface so the
// public PolkitAgent stays a clean QObject with no polkit-qt types in its
// header. A friend of PolkitAgent (to reach its Private) and of AuthRequest (to
// build one). It overrides regular virtuals, so it needs no Q_OBJECT.
class ListenerImpl : public PolkitQt1::Agent::Listener
{
public:
    explicit ListenerImpl(PolkitAgent* facade)
        : m_facade(facade)
    {
    }

    void initiateAuthentication(const QString& actionId, const QString& message, const QString& iconName,
                                const PolkitQt1::Details& details, const QString& cookie,
                                const PolkitQt1::Identity::List& identities,
                                PolkitQt1::Agent::AsyncResult* result) override;
    bool initiateAuthenticationFinish() override
    {
        return true;
    }
    void cancelAuthentication() override;

private:
    PolkitAgent* m_facade;
};

class PolkitAgent::Private
{
public:
    QString sessionId; // empty -> current session (resolved from pid at register time)
    QString objectPath;
    bool registered = false;
    ListenerImpl listener;

    // Active request state. polkit serialises authentication, so at most one is
    // live. The AsyncResult is owned by polkit (we complete it, never delete it);
    // the Identity::List is kept so milestone 4 can build the PAM session for the
    // selected identity.
    AuthRequest* request = nullptr;
    PolkitQt1::Identity::List identities;
    PolkitQt1::Agent::AsyncResult* result = nullptr;
    PolkitQt1::Agent::Session* session = nullptr;

    Private(PolkitAgent* facade, QString sid, QString path)
        : sessionId(std::move(sid))
        , objectPath(std::move(path))
        , listener(facade)
    {
    }
};

PolkitAgent::PolkitAgent(QObject* parent)
    : PolkitAgent(QString(), defaultObjectPath(), parent)
{
}

PolkitAgent::PolkitAgent(QString sessionId, QString objectPath, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(this, std::move(sessionId), std::move(objectPath)))
{
}

// Out-of-line: the Private dtor needs ListenerImpl complete, and ~Listener
// unregisters the agent (there is no explicit unregister API), so destroying
// this object releases the session.
PolkitAgent::~PolkitAgent() = default;

bool PolkitAgent::registered() const
{
    return d->registered;
}

QString PolkitAgent::defaultObjectPath()
{
    return QLatin1String(kDefaultObjectPath);
}

bool PolkitAgent::registerAgent()
{
    if (d->registered)
        return true;

    // An empty session id means "this process's session", resolved from the pid.
    const PolkitQt1::UnixSessionSubject subject = d->sessionId.isEmpty()
        ? PolkitQt1::UnixSessionSubject(static_cast<qint64>(QCoreApplication::applicationPid()))
        : PolkitQt1::UnixSessionSubject(d->sessionId);

    const bool ok = d->listener.registerListener(subject, d->objectPath);
    if (!ok) {
        qCInfo(lcPolkitAgent) << "could not register as the authentication agent for the session"
                              << "(another agent likely owns it) - staying inert";
    }
    if (ok != d->registered) {
        d->registered = ok;
        Q_EMIT registeredChanged();
    }
    return d->registered;
}

AuthRequest* PolkitAgent::activeRequest() const
{
    return d->request;
}

void PolkitAgent::authenticate()
{
    if (!d->request || d->session)
        return;
    const int index = d->request->selectedIdentity();
    if (index < 0 || index >= d->identities.size())
        return;

    // The Session is constructed with polkit's AsyncResult and drives the PAM
    // conversation for the chosen identity. We complete the result on completed()
    // (the documented contract), never the Session.
    auto* session = new PolkitQt1::Agent::Session(d->identities.at(index), d->request->cookie(), d->result, this);
    d->session = session;

    connect(session, &PolkitQt1::Agent::Session::request, this, [this](const QString& prompt, bool echo) {
        if (!d->request)
            return;
        d->request->m_prompt = prompt;
        d->request->m_echo = echo;
        Q_EMIT d->request->promptChanged();
    });
    connect(session, &PolkitQt1::Agent::Session::completed, this, [this](bool gained) {
        onSessionCompleted(gained);
    });
    connect(session, &PolkitQt1::Agent::Session::showError, this, [this](const QString& text) {
        Q_EMIT authenticationError(text);
    });
    connect(session, &PolkitQt1::Agent::Session::showInfo, this, [this](const QString& text) {
        Q_EMIT authenticationInfo(text);
    });

    session->initiate();
}

void PolkitAgent::respond(const QString& response)
{
    // Straight through to PAM; never retained, logged, or echoed.
    if (d->session)
        d->session->setResponse(response);
}

void PolkitAgent::cancel()
{
    if (!d->request)
        return;
    if (d->session) {
        // A running PAM conversation: cancelling it emits completed(false), which
        // completes the result and tears down via onSessionCompleted.
        d->session->cancel();
        return;
    }
    // No conversation started yet: decline the request directly (completing the
    // result without authorization).
    if (d->result) {
        d->result->setCompleted();
        d->result = nullptr;
    }
    teardownActive();
    Q_EMIT authenticationCancelled();
}

void PolkitAgent::onSessionCompleted(bool gainedAuthorization)
{
    if (d->result) {
        d->result->setCompleted();
        d->result = nullptr;
    }
    teardownActive();
    Q_EMIT authenticationCompleted(gainedAuthorization);
}

void PolkitAgent::teardownActive()
{
    if (d->session) {
        // Stop further signals before deleting, so a teardown triggered from
        // within a session signal (completed) does not re-enter.
        d->session->disconnect(this);
        d->session->deleteLater();
        d->session = nullptr;
    }
    d->result = nullptr;
    d->identities.clear();
    if (d->request) {
        AuthRequest* request = d->request;
        d->request = nullptr;
        request->deleteLater();
    }
    Q_EMIT activeRequestChanged();
}

void ListenerImpl::initiateAuthentication(const QString& actionId, const QString& message, const QString& iconName,
                                          const PolkitQt1::Details& details, const QString& cookie,
                                          const PolkitQt1::Identity::List& identities,
                                          PolkitQt1::Agent::AsyncResult* result)
{
    PolkitAgent::Private* d = m_facade->d.get();

    // polkit serialises, but defend against a lingering prior request: decline it
    // before taking the new one so its AsyncResult is not orphaned.
    if (d->request)
        m_facade->cancel();

    auto* request = new AuthRequest(actionId, message, iconName, detail::detailsToMap(details), cookie,
                                    detail::identityNames(identities), m_facade);
    d->request = request;
    d->identities = identities;
    d->result = result;

    // Milestone 3 surfaces the decoded request. Answering it (the Agent::Session
    // PAM conversation driven by respond(password)) lands in milestone 4; until
    // then a consumer can read the request and decline it via cancel().
    Q_EMIT m_facade->activeRequestChanged();
    Q_EMIT m_facade->authenticationRequested(request);
}

void ListenerImpl::cancelAuthentication()
{
    PolkitAgent::Private* d = m_facade->d.get();
    if (!d->request)
        return;
    // polkit is withdrawing the request and owns the result's teardown, so we
    // drop our state (result first) and notify without completing it ourselves.
    // teardownActive disconnects + deletes any running session, aborting PAM.
    d->result = nullptr;
    m_facade->teardownActive();
    Q_EMIT m_facade->authenticationCancelled();
}

} // namespace PhosphorServicePolkit
