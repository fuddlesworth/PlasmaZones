// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePolkit/PolkitAgent.h>

#include <PhosphorServicePolkit/AuthRequest.h>

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

QVariantMap detailsToMap(const PolkitQt1::Details& details)
{
    QVariantMap map;
    const QStringList keys = details.keys();
    for (const QString& key : keys)
        map.insert(key, details.lookup(key));
    return map;
}

QStringList identityNames(const PolkitQt1::Identity::List& identities)
{
    QStringList names;
    names.reserve(identities.size());
    for (const PolkitQt1::Identity& identity : identities)
        names << identity.toString();
    return names;
}
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

void PolkitAgent::cancel()
{
    if (!d->request)
        return;
    // Completing the result without the user authenticating is a clean decline.
    if (d->result) {
        d->result->setCompleted();
        d->result = nullptr;
    }
    AuthRequest* request = d->request;
    d->request = nullptr;
    d->identities.clear();
    Q_EMIT activeRequestChanged();
    Q_EMIT authenticationCancelled();
    request->deleteLater();
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

    auto* request = new AuthRequest(actionId, message, iconName, detailsToMap(details), cookie,
                                    identityNames(identities), m_facade);
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
    // drop our state and notify without completing the result ourselves.
    AuthRequest* request = d->request;
    d->request = nullptr;
    d->result = nullptr;
    d->identities.clear();
    Q_EMIT m_facade->activeRequestChanged();
    Q_EMIT m_facade->authenticationCancelled();
    request->deleteLater();
}

} // namespace PhosphorServicePolkit
