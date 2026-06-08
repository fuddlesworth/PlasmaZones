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

#include <utility>

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

    // Created lazily by registerAgent(), never in the constructor. Constructing a
    // polkit-qt Listener has glib side effects (it allocates and globally tracks a
    // PolkitAgentListener), and ~Listener calls polkit_agent_listener_unregister(),
    // which blocks indefinitely when no glib event loop is iterating its context.
    // Keeping this null until registration makes a bare PolkitAgent genuinely
    // side-effect-free (and destructible without blocking), as the class contract
    // promises - and lets it be instantiated from QML purely to read properties.
    std::unique_ptr<ListenerImpl> listener;

    // Active request state. polkit serialises authentication, so at most one is
    // live. The AsyncResult is owned by polkit (we complete it, never delete it);
    // the Identity::List is kept so authenticate() can build the PAM session for
    // the selected identity.
    AuthRequest* request = nullptr;
    PolkitQt1::Identity::List identities;
    PolkitQt1::Agent::AsyncResult* result = nullptr;
    PolkitQt1::Agent::Session* session = nullptr;

    Private(QString sid, QString path)
        : sessionId(std::move(sid))
        , objectPath(std::move(path))
    {
    }
};

PolkitAgent::PolkitAgent(QObject* parent)
    : PolkitAgent(QString(), defaultObjectPath(), parent)
{
}

PolkitAgent::PolkitAgent(QString sessionId, QString objectPath, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>(std::move(sessionId), std::move(objectPath)))
{
}

// Out-of-line: the Private dtor needs ListenerImpl complete. When the agent
// registered, ~Listener unregisters it (there is no explicit unregister API), so
// destroying this object releases the session; when it never registered, the
// lazy listener is null and teardown does not touch polkit-qt at all.
PolkitAgent::~PolkitAgent()
{
    // Complete any in-flight polkit result so a pending authentication does not
    // dangle when the agent is destroyed mid-conversation. The Session +
    // AuthRequest are QObject children, reclaimed by ~QObject; we deliberately do
    // not run the signal-emitting settleActive() from the destructor.
    //
    // Sever the session first so its asynchronous completed() can never re-enter
    // onSessionCompleted() during teardown, and clear the result as we complete
    // it so "polkit's result is completed exactly once" holds by construction
    // rather than by trusting ~Session to stay silent.
    if (d->session)
        d->session->disconnect(this);
    if (auto* result = std::exchange(d->result, nullptr))
        result->setCompleted();
}

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

    // Construct the polkit-qt Listener on first registration (see Private::listener):
    // this is the first and only point where the agent touches polkit-qt.
    if (!d->listener)
        d->listener = std::make_unique<ListenerImpl>(this);

    const bool ok = d->listener->registerListener(subject, d->objectPath);
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
        // Update the display properties with change-guarded notifies, then emit
        // the answer-now EVENT. The event fires every time (including a same-text
        // retry after a wrong answer), which the change-guarded prompt property
        // would not re-notify.
        if (d->request->m_prompt != prompt) {
            d->request->m_prompt = prompt;
            Q_EMIT d->request->promptChanged();
        }
        if (d->request->m_echo != echo) {
            d->request->m_echo = echo;
            Q_EMIT d->request->echoChanged();
        }
        Q_EMIT promptRequested(prompt, echo);
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
    // Decline: complete polkit's result without authorization and tear down.
    // Synchronous and deterministic even mid-conversation; it does not wait on
    // the session's asynchronous completed() signal.
    settleActive(/*completeResult=*/true);
    Q_EMIT authenticationCancelled();
}

void PolkitAgent::onSessionCompleted(bool gainedAuthorization)
{
    // The session finished; complete polkit's result exactly once and tear down.
    settleActive(/*completeResult=*/true);
    Q_EMIT authenticationCompleted(gainedAuthorization);
}

void PolkitAgent::settleActive(bool completeResult)
{
    if (!d->request)
        return;

    // Capture and clear the active state FIRST, so any re-entrant call (a slot on
    // activeRequestChanged, or a synchronous session signal) sees no active
    // request and returns. This makes the result completion exactly-once and the
    // teardown idempotent regardless of polkit-qt's signal-emission timing
    // (Session::completed is asynchronous, fired when the PAM helper exits).
    AuthRequest* request = std::exchange(d->request, nullptr);
    PolkitQt1::Agent::Session* session = std::exchange(d->session, nullptr);
    PolkitQt1::Agent::AsyncResult* result = std::exchange(d->result, nullptr);
    d->identities.clear();

    if (session) {
        // Disconnect before deleting so the session's pending/asynchronous
        // completed() can never re-enter this path; ~Session aborts the PAM
        // helper.
        session->disconnect(this);
        session->deleteLater();
    }
    // Complete polkit's result unless the daemon withdrew the request, in which
    // case polkit owns the result's teardown and completing it would double-free.
    if (completeResult && result)
        result->setCompleted();

    Q_EMIT activeRequestChanged();
    request->deleteLater();
}

void ListenerImpl::initiateAuthentication(const QString& actionId, const QString& message, const QString& iconName,
                                          const PolkitQt1::Details& details, const QString& cookie,
                                          const PolkitQt1::Identity::List& identities,
                                          PolkitQt1::Agent::AsyncResult* result)
{
    PolkitAgent::Private* d = m_facade->d.get();

    // polkit serialises authentication, but defend against a lingering prior
    // request: decline it synchronously (completing its result) before adopting
    // the new one, so its AsyncResult is never orphaned. settleActive does NOT
    // rely on the old session's asynchronous completed(), so the state is fully
    // cleared before the assignments below.
    if (d->request) {
        m_facade->settleActive(/*completeResult=*/true);
        Q_EMIT m_facade->authenticationCancelled();
    }

    auto* request = new AuthRequest(actionId, message, iconName, detail::detailsToMap(details), cookie,
                                    detail::identityNames(identities), m_facade);
    d->request = request;
    d->identities = identities;
    d->result = result;

    // Surface the decoded request. A consumer calls authenticate() to begin the
    // PAM conversation, or cancel() to decline.
    Q_EMIT m_facade->activeRequestChanged();
    Q_EMIT m_facade->authenticationRequested(request);
}

void ListenerImpl::cancelAuthentication()
{
    PolkitAgent::Private* d = m_facade->d.get();
    if (!d->request)
        return;
    // polkit is withdrawing the request and owns the result's teardown, so settle
    // WITHOUT completing the result ourselves (completing it would double-free).
    // settleActive disconnects + deletes any running session, aborting PAM.
    m_facade->settleActive(/*completeResult=*/false);
    Q_EMIT m_facade->authenticationCancelled();
}

} // namespace PhosphorServicePolkit
