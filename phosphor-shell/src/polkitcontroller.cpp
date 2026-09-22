// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PolkitController.h"

#include <PhosphorLayer/IScreenProvider.h>

#include <QFile>
#include <QLoggingCategory>
#include <QQmlEngine>

Q_LOGGING_CATEGORY(lcPolkitController, "phosphorshell.polkit")

namespace PhosphorShellApp {

using PhosphorServicePolkit::AuthRequest;
using PhosphorServicePolkit::PolkitAgent;

PolkitController::PolkitController(PhosphorLayer::IScreenProvider* screens, QObject* parent)
    : QObject(parent)
    , m_screens(screens)
{
    connect(&m_agent, &PolkitAgent::registeredChanged, this, &PolkitController::registeredChanged);

    // A new request: start its PAM conversation at once, as the user
    // (identity 0), so the prompt appears with the field ready. The
    // prompt is a property change the dialog binds; the conversation is
    // an action the dialog should not have to take.
    connect(&m_agent, &PolkitAgent::authenticationRequested, this, [this](AuthRequest* request) {
        setLastError({});
        setPlacement({}, {});
        qCInfo(lcPolkitController) << "authentication requested for" << request->actionId() << "by pid"
                                   << pidFromDetails(request->details());
        m_agent.authenticate();
    });
    connect(&m_agent, &PolkitAgent::activeRequestChanged, this, [this] {
        if (!m_agent.activeRequest()) {
            setPlacement({}, {});
        }
        Q_EMIT activeRequestChanged();
    });
    connect(&m_agent, &PolkitAgent::authenticationError, this, &PolkitController::setLastError);
    connect(&m_agent, &PolkitAgent::authenticationCompleted, this, [this](bool gained) {
        qCInfo(lcPolkitController) << "authentication" << (gained ? "granted" : "denied");
    });
}

PolkitController::~PolkitController() = default;

bool PolkitController::registerAgent()
{
    if (m_agent.registerAgent()) {
        qCInfo(lcPolkitController) << "registered as the session's PolicyKit agent";
        return true;
    }
    qCWarning(lcPolkitController) << "not the session's PolicyKit agent (another agent holds the session); "
                                     "the polkit prompt stays inert";
    return false;
}

bool PolkitController::isRegistered() const
{
    return m_agent.registered();
}

AuthRequest* PolkitController::activeRequest() const
{
    return m_agent.activeRequest();
}

qint64 PolkitController::requesterPid() const
{
    AuthRequest* request = m_agent.activeRequest();
    return request ? pidFromDetails(request->details()) : 0;
}

QString PolkitController::requesterName() const
{
    return processName(requesterPid());
}

QString PolkitController::lastError() const
{
    return m_lastError;
}

QString PolkitController::promptScreen() const
{
    return m_promptScreen;
}

QRect PolkitController::anchorRect() const
{
    return m_anchorRect;
}

void PolkitController::respond(const QString& response)
{
    setLastError({});
    m_agent.respond(response);
}

void PolkitController::cancel()
{
    m_agent.cancel();
}

void PolkitController::setPlacement(const QString& screenName, const QRect& anchorRect)
{
    if (m_promptScreen == screenName && m_anchorRect == anchorRect) {
        return;
    }
    m_promptScreen = screenName;
    m_anchorRect = anchorRect;
    Q_EMIT placementChanged();
}

QStringList PolkitController::screenNames() const
{
    QStringList names;
    if (!m_screens) {
        return names;
    }
    const QList<QScreen*> screens = m_screens->screens();
    for (QScreen* screen : screens) {
        if (screen) {
            names.append(screen->name());
        }
    }
    return names;
}

QScreen* PolkitController::screenNamed(const QString& name) const
{
    if (!m_screens) {
        return nullptr;
    }
    QScreen* found = nullptr;
    if (!name.isEmpty()) {
        const QList<QScreen*> screens = m_screens->screens();
        for (QScreen* screen : screens) {
            if (screen && screen->name() == name) {
                found = screen;
                break;
            }
        }
    }
    if (!found) {
        found = m_screens->primary();
    }
    if (found) {
        // The screen belongs to Qt. Without this the JS wrapper's
        // collection deletes the live QScreen.
        QQmlEngine::setObjectOwnership(found, QQmlEngine::CppOwnership);
    }
    return found;
}

qint64 PolkitController::pidFromDetails(const QVariantMap& details)
{
    for (const char* key : {"polkit.subject-pid", "polkit.caller-pid"}) {
        bool ok = false;
        const qint64 pid = details.value(QLatin1String(key)).toString().toLongLong(&ok);
        if (ok && pid > 0) {
            return pid;
        }
    }
    return 0;
}

QString PolkitController::processName(qint64 pid)
{
    if (pid <= 0) {
        return {};
    }
    QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
    if (!comm.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(comm.readLine()).trimmed();
}

void PolkitController::setLastError(const QString& error)
{
    if (m_lastError == error) {
        return;
    }
    m_lastError = error;
    Q_EMIT lastErrorChanged();
}

} // namespace PhosphorShellApp
