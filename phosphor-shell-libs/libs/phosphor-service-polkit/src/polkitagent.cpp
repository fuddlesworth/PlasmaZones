// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePolkit/PolkitAgent.h>

#include <PhosphorServicePolkit/AuthRequest.h>

#include "authenticationsession_p.h"
#include "polkitdecode.h"

#include <polkitqt1-agent-listener.h>
#include <polkitqt1-agent-session.h>
#include <polkitqt1-details.h>
#include <polkitqt1-identity.h>
#include <polkitqt1-subject.h>

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QPointer>
#include <QStringList>
#include <QVariantMap>

#include <utility>

namespace {
constexpr auto kDefaultObjectPath = "/org/phosphor/PolicyKit1/AuthenticationAgent";
constexpr int kMaximumAttempts = 3;
Q_LOGGING_CATEGORY(lcPolkitAgent, "phosphor.service.polkit")
} // namespace

namespace PhosphorServicePolkit {

class NativeAuthenticationSession : public detail::AuthenticationSession
{
public:
    NativeAuthenticationSession(const PolkitQt1::Identity& identity, const QString& cookie, QObject* parent)
        : AuthenticationSession(parent)
        , m_session(new PolkitQt1::Agent::Session(identity, cookie, nullptr, this))
    {
        connect(m_session, &PolkitQt1::Agent::Session::request, this, &AuthenticationSession::requested);
        connect(m_session, &PolkitQt1::Agent::Session::showError, this, &AuthenticationSession::error);
        connect(m_session, &PolkitQt1::Agent::Session::showInfo, this, &AuthenticationSession::info);
        connect(m_session, &PolkitQt1::Agent::Session::completed, this, [this](bool gained) {
            // polkit-qt releases its native session immediately after this
            // signal. Never call cancel() on that completed native object.
            m_finished = true;
            Q_EMIT completed(gained);
        });
    }
    void initiate() override
    {
        m_session->initiate();
    }
    void respond(const QString& response) override
    {
        m_session->setResponse(response);
    }
    void cancel() override
    {
        if (!m_finished)
            m_session->cancel();
    }

private:
    PolkitQt1::Agent::Session* m_session;
    bool m_finished = false;
};

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
    void cancelAuthentication() override
    {
        m_facade->cancel();
    }

private:
    PolkitAgent* m_facade;
};

class PolkitAgent::Private
{
public:
    QString sessionId;
    QString objectPath;
    bool registered = false;
    // Listener construction/registration has GLib side effects. Keep it lazy
    // so a bare agent and the conversation tests never claim a real session.
    std::unique_ptr<ListenerImpl> listener;
    AuthRequest* request = nullptr;
    QPointer<detail::AuthenticationSession> session;
    std::function<detail::AuthenticationSession*(int, QObject*)> sessionFactory;
    std::function<void()> completeResult;
    Phase phase = Phase::Idle;
    quint64 requestGeneration = 0;
    quint64 sessionGeneration = 0;
    int failedAttempts = 0;
    bool answered = false;
    bool sawError = false;

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

PolkitAgent::~PolkitAgent()
{
    stopSession(true);
    d->request = nullptr;
    if (auto complete = std::exchange(d->completeResult, {}))
        complete();
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
    const PolkitQt1::UnixSessionSubject subject = d->sessionId.isEmpty()
        ? PolkitQt1::UnixSessionSubject(static_cast<qint64>(QCoreApplication::applicationPid()))
        : PolkitQt1::UnixSessionSubject(d->sessionId);
    if (!d->listener)
        d->listener = std::make_unique<ListenerImpl>(this);
    const bool ok = d->listener->registerListener(subject, d->objectPath);
    if (!ok)
        qCInfo(lcPolkitAgent) << "could not register as the authentication agent for the session; staying inert";
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
PolkitAgent::Phase PolkitAgent::phase() const
{
    return d->phase;
}
bool PolkitAgent::inputReady() const
{
    return d->request && d->session && d->phase == Phase::Prompt;
}
bool PolkitAgent::busy() const
{
    return d->phase == Phase::Starting || d->phase == Phase::Checking;
}
bool PolkitAgent::canRetry() const
{
    return d->request && d->phase == Phase::Unavailable && d->failedAttempts < kMaximumAttempts;
}

void PolkitAgent::setPhase(Phase phase)
{
    if (d->phase == phase)
        return;
    d->phase = phase;
    Q_EMIT stateChanged();
}

void PolkitAgent::authenticate()
{
    if (!d->request || d->session || (d->phase != Phase::Idle && d->phase != Phase::Starting))
        return;
    QPointer<AuthRequest> request = d->request;
    const int identity = request->selectedIdentity();
    if (identity < 0 || identity >= request->identities().size() || !d->sessionFactory) {
        settleActive(Phase::Failed);
        return;
    }
    setPhase(Phase::Starting);
    if (d->request != request || d->phase != Phase::Starting)
        return;
    d->answered = false;
    d->sawError = false;
    const quint64 generation = ++d->sessionGeneration;
    auto* session = d->sessionFactory(identity, this);
    if (d->request != request || d->sessionGeneration != generation) {
        if (session) {
            session->cancel();
            session->deleteLater();
        }
        return;
    }
    if (!session) {
        onSessionCompleted(false);
        return;
    }
    d->session = session;
    const auto current = [this, request, session, generation] {
        return request && d->request == request && d->session == session && d->sessionGeneration == generation;
    };
    connect(session, &detail::AuthenticationSession::requested, this,
            [this, request, current](const QString& prompt, bool echo) {
                if (!current())
                    return;
                const bool promptChanged = request->m_prompt != prompt;
                const bool echoChanged = request->m_echo != echo;
                request->m_prompt = prompt;
                request->m_echo = echo;
                if (promptChanged)
                    Q_EMIT request->promptChanged();
                if (!current())
                    return;
                if (echoChanged)
                    Q_EMIT request->echoChanged();
                if (!current())
                    return;
                setPhase(Phase::Prompt);
                if (current())
                    Q_EMIT promptRequested(prompt, echo);
            });
    connect(session, &detail::AuthenticationSession::completed, this, [this, current](bool gained) {
        if (current())
            onSessionCompleted(gained);
    });
    connect(session, &detail::AuthenticationSession::error, this, [this, current](const QString& text) {
        if (!current())
            return;
        d->sawError = true;
        Q_EMIT authenticationError(text);
    });
    connect(session, &detail::AuthenticationSession::info, this, [this, current](const QString& text) {
        if (current())
            Q_EMIT authenticationInfo(text);
    });
    connect(session, &QObject::destroyed, this, [this, request, generation] {
        if (request && d->request == request && d->sessionGeneration == generation) {
            d->session = nullptr;
            onSessionCompleted(false);
        }
    });
    session->initiate();
}

void PolkitAgent::respond(const QString& response)
{
    if (!inputReady())
        return;
    const QPointer<detail::AuthenticationSession> session = d->session;
    const quint64 generation = d->sessionGeneration;
    d->answered = true;
    setPhase(Phase::Checking);
    // A state observer may cancel or switch accounts synchronously. Never
    // send this response to a replacement conversation, or queue it for one.
    if (session && d->session == session && d->sessionGeneration == generation)
        session->respond(response);
}

void PolkitAgent::selectIdentity(int index)
{
    if (d->request)
        d->request->setSelectedIdentity(index);
}

void PolkitAgent::restartIdentity()
{
    if (!d->request)
        return;
    const QPointer<AuthRequest> request = d->request;
    stopSession(true);
    if (d->request != request)
        return;
    setPhase(Phase::Starting);
    authenticate();
}

void PolkitAgent::retry()
{
    if (!canRetry())
        return;
    setPhase(Phase::Starting);
    authenticate();
}

void PolkitAgent::cancel()
{
    settleActive(Phase::Cancelled);
}

void PolkitAgent::stopSession(bool cancel)
{
    ++d->sessionGeneration;
    const QPointer<detail::AuthenticationSession> session = d->session;
    d->session = nullptr;
    if (!session)
        return;
    session->disconnect(this);
    if (cancel)
        session->cancel();
    if (session)
        session->deleteLater();
}

void PolkitAgent::onSessionCompleted(bool gained)
{
    if (!d->request)
        return;
    if (gained) {
        settleActive(Phase::Success);
        return;
    }
    const bool answered = d->answered;
    const bool sawError = d->sawError;
    const auto request = QPointer<AuthRequest>(d->request);
    stopSession(false);
    const quint64 generation = d->sessionGeneration;
    const auto current = [this, request, generation] {
        return request && d->request == request && d->sessionGeneration == generation;
    };
    ++d->failedAttempts;
    if (d->failedAttempts >= kMaximumAttempts) {
        if (!sawError)
            Q_EMIT authenticationError(QString());
        if (current())
            settleActive(Phase::Failed);
        return;
    }
    if (!answered) {
        setPhase(Phase::Unavailable);
        if (current() && !sawError)
            Q_EMIT authenticationError(QString());
        return;
    }
    setPhase(Phase::Starting);
    if (!current())
        return;
    if (!sawError)
        Q_EMIT authenticationError(QString());
    // Let polkit-qt's completed callback release its native session before
    // initiating the next PAM conversation. The request and its result stay
    // active, and account changes/cancellation invalidate this queued retry.
    QMetaObject::invokeMethod(
        this,
        [this, request, generation] {
            if (request && d->request == request && d->sessionGeneration == generation && d->phase == Phase::Starting)
                authenticate();
        },
        Qt::QueuedConnection);
}

void PolkitAgent::settleActive(Phase outcome)
{
    if (!d->request)
        return;
    AuthRequest* request = std::exchange(d->request, nullptr);
    const quint64 generation = d->requestGeneration;
    auto complete = std::exchange(d->completeResult, {});
    d->sessionFactory = {};
    stopSession(true);
    setPhase(outcome);
    if (d->requestGeneration == generation && !d->request)
        Q_EMIT activeRequestChanged();
    request->deleteLater();
    if (d->requestGeneration == generation && !d->request) {
        if (outcome == Phase::Cancelled)
            Q_EMIT authenticationCancelled();
        else
            Q_EMIT authenticationCompleted(outcome == Phase::Success);
    }
    // polkit-qt's cancellation callback does not resolve AsyncResult. Every
    // terminal path, including daemon withdrawal, must complete it once.
    if (complete)
        complete();
}

void PolkitAgent::beginRequest(const QString& actionId, const QString& message, const QString& iconName,
                               const QVariantMap& details, const QString& cookie, const QStringList& identities,
                               std::function<detail::AuthenticationSession*(int, QObject*)> factory,
                               std::function<void()> complete)
{
    const quint64 generation = ++d->requestGeneration;
    settleActive(Phase::Cancelled);
    // Completing the previous result can synchronously deliver a newer
    // request. That newer request wins; never orphan either result.
    if (d->requestGeneration != generation) {
        if (complete)
            complete();
        return;
    }
    auto* request = new AuthRequest(actionId, message, iconName, details, cookie, identities, this);
    d->request = request;
    d->sessionFactory = std::move(factory);
    d->completeResult = std::move(complete);
    d->failedAttempts = 0;
    d->answered = false;
    d->sawError = false;
    connect(request, &AuthRequest::selectedIdentityChanged, this, [this, request] {
        if (d->request == request)
            restartIdentity();
    });
    setPhase(Phase::Idle);
    if (d->request != request)
        return;
    Q_EMIT activeRequestChanged();
    if (d->request == request)
        Q_EMIT authenticationRequested(request);
}

void ListenerImpl::initiateAuthentication(const QString& actionId, const QString& message, const QString& iconName,
                                          const PolkitQt1::Details& details, const QString& cookie,
                                          const PolkitQt1::Identity::List& identities,
                                          PolkitQt1::Agent::AsyncResult* result)
{
    // polkit-qt allocates this wrapper for the listener and never deletes it.
    // Keep one owner until the request is resolved; individual PAM sessions
    // need only the cookie and do not own or complete the result.
    auto ownedResult = std::shared_ptr<PolkitQt1::Agent::AsyncResult>(result);
    m_facade->beginRequest(
        actionId, message, iconName, detail::detailsToMap(details), cookie, detail::identityNames(identities),
        [identities, cookie](int index, QObject* parent) -> detail::AuthenticationSession* {
            if (index < 0 || index >= identities.size() || !identities.at(index).isValid())
                return nullptr;
            return new NativeAuthenticationSession(identities.at(index), cookie, parent);
        },
        [ownedResult] {
            if (ownedResult)
                ownedResult->setCompleted();
        });
}

} // namespace PhosphorServicePolkit
