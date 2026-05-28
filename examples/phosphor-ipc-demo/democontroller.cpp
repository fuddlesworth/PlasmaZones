// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DemoController.h"

#include <PhosphorIpc/IpcRouter.h>

#include <QDateTime>
#include <QStringList>
#include <QVariant>

namespace {
constexpr int EventLogMaxEntries = 20;
} // namespace

namespace PhosphorIpcDemo {

DemoController::DemoController(QObject* parent)
    : QObject(parent)
    , m_router(std::make_unique<PhosphorIpc::IpcRouter>())
    , m_status(QStringLiteral("router not started"))
{
    // Reflect registry churn into the status panel so the demo
    // window shows live state without the user having to run
    // `phosphorctl list` in another terminal.
    QObject::connect(m_router.get(), &PhosphorIpc::IpcRouter::targetRegistered, this,
                     &DemoController::onTargetRegistered);
    QObject::connect(m_router.get(), &PhosphorIpc::IpcRouter::targetUnregistered, this,
                     &DemoController::onTargetUnregistered);
}

DemoController::~DemoController() = default;

bool DemoController::start(const QString& socketPath)
{
    // m_router is initialised by make_unique in the ctor and no
    // member function reassigns it; it never becomes null over the
    // DemoController's lifetime. No null-check needed. (The
    // Q_DISABLE_COPY_MOVE on the class is orthogonal — it prevents
    // the controller itself from being moved/copied, not its
    // members from being reassigned, but the no-reassign invariant
    // is the real reason m_router stays valid.)
    const QString previousSocketPath = m_router->socketPath();
    const bool ok = m_router->start(socketPath);
    if (ok) {
        // Defer the success-path status update to refreshStatus()
        // (same shape onTargetRegistered/Unregistered consume) so
        // there's exactly one status format across every code path.
        // refreshStatus() emits statusChanged itself iff the value
        // moved; the failure branch below handles the other case
        // since refreshStatus() can't see "router failed to start".
        refreshStatus();
    } else {
        const QString failureStatus = QStringLiteral("router failed to start (see logs)");
        if (m_status != failureStatus) {
            m_status = failureStatus;
            Q_EMIT statusChanged();
        }
    }
    // CLAUDE.md rule: only emit when the value actually changed.
    // Idempotent restarts on the same path would otherwise spam
    // bindings on every call.
    if (m_router->socketPath() != previousSocketPath) {
        Q_EMIT socketPathChanged();
    }
    return ok;
}

QString DemoController::socketPath() const
{
    return m_router->socketPath();
}

QString DemoController::status() const
{
    return m_status;
}

PhosphorIpc::IpcRouter* DemoController::router() const
{
    return m_router.get();
}

QStringList DemoController::eventLog() const
{
    return m_eventLog;
}

void DemoController::recordEvent(const QString& targetName, const QString& signalName, const QVariantList& args)
{
    // Format: "HH:mm:ss  target.signal(arg1, arg2)". Timestamp lets
    // the user correlate a phosphorctl call burst against the visible
    // event stream when running side-by-side.
    QStringList parts;
    parts.reserve(args.size());
    for (const QVariant& v : args) {
        parts.append(v.toString());
    }
    const QString line = QStringLiteral("%1  %2.%3(%4)")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), targetName,
                                  signalName, parts.join(QStringLiteral(", ")));
    m_eventLog.prepend(line);
    while (m_eventLog.size() > EventLogMaxEntries) {
        m_eventLog.removeLast();
    }
    Q_EMIT eventLogChanged();
}

void DemoController::onTargetRegistered(const QString& /*name*/)
{
    refreshStatus();
}

void DemoController::onTargetUnregistered(const QString& /*name*/)
{
    refreshStatus();
}

void DemoController::refreshStatus()
{
    // Steady-state status reflects only the currently-live registry
    // shape; the prior format included a transient "(registered 'x')"
    // / "(unregistered 'x')" suffix that never cleared once any
    // target had churned, so the UI showed misleading historical
    // events as the persistent status.
    const QStringList ids = m_router->listTargets();
    const QString next = ids.isEmpty()
        ? QStringLiteral("listening on %1; no targets registered yet").arg(m_router->socketPath())
        : QStringLiteral("listening on %1; targets: %2").arg(m_router->socketPath(), ids.join(QStringLiteral(", ")));
    if (next == m_status) {
        return;
    }
    m_status = next;
    Q_EMIT statusChanged();
}

} // namespace PhosphorIpcDemo
