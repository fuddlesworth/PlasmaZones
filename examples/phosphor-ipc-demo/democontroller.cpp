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
    const bool ok = m_router->start(socketPath);
    if (ok) {
        m_status = QStringLiteral("listening on %1").arg(m_router->socketPath());
    } else {
        m_status = QStringLiteral("router failed to start (see logs)");
    }
    Q_EMIT socketPathChanged();
    Q_EMIT statusChanged();
    return ok;
}

QString DemoController::socketPath() const
{
    return m_router ? m_router->socketPath() : QString();
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

void DemoController::onTargetRegistered(const QString& name)
{
    const QStringList ids = m_router->listTargets();
    m_status = QStringLiteral("listening on %1; targets: %2 (registered '%3')")
                   .arg(m_router->socketPath(), ids.join(QStringLiteral(", ")), name);
    Q_EMIT statusChanged();
}

void DemoController::onTargetUnregistered(const QString& name)
{
    const QStringList ids = m_router->listTargets();
    m_status = QStringLiteral("listening on %1; targets: %2 (unregistered '%3')")
                   .arg(m_router->socketPath(), ids.join(QStringLiteral(", ")), name);
    Q_EMIT statusChanged();
}

} // namespace PhosphorIpcDemo
