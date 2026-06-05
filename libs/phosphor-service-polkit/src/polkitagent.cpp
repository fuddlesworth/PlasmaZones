// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServicePolkit/PolkitAgent.h>

#include <polkitqt1-agent-listener.h>
#include <polkitqt1-agent-session.h>
#include <polkitqt1-details.h>
#include <polkitqt1-identity.h>
#include <polkitqt1-subject.h>

#include <QCoreApplication>
#include <QLoggingCategory>

namespace {
constexpr auto kDefaultObjectPath = "/org/phosphor/PolicyKit1/AuthenticationAgent";

Q_LOGGING_CATEGORY(lcPolkitAgent, "phosphor.service.polkit")
} // namespace

namespace PhosphorServicePolkit {

// Internal polkit Listener. Wraps the polkit-qt agent callback surface so the
// public PolkitAgent stays a clean QObject with no polkit-qt types in its
// header. It overrides regular virtuals (not slots), so it needs no Q_OBJECT.
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
                                PolkitQt1::Agent::AsyncResult* result) override
    {
        // Milestone 1 is registration plumbing only. Decoding the request into a
        // typed AuthRequest (milestone 3) and driving the Agent::Session PAM
        // conversation that actually authenticates (milestone 4) follow. Until
        // then, complete the result without authorization (a clean denial)
        // rather than leaving polkit's auth dialog waiting on us.
        Q_UNUSED(actionId)
        Q_UNUSED(message)
        Q_UNUSED(iconName)
        Q_UNUSED(details)
        Q_UNUSED(cookie)
        Q_UNUSED(identities)
        if (result)
            result->setCompleted();
    }

    bool initiateAuthenticationFinish() override
    {
        return true;
    }

    void cancelAuthentication() override
    {
    }

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

} // namespace PhosphorServicePolkit
