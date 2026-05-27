// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DemoController.h"

#include <PhosphorIpc/IpcRouter.h>

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

void DemoController::onTargetRegistered(const QString& name)
{
    const QStringList ids = m_router->listTargets();
    m_status = QStringLiteral("listening on %1 — targets: %2 (registered '%3')")
                   .arg(m_router->socketPath(), ids.join(QStringLiteral(", ")), name);
    Q_EMIT statusChanged();
}

void DemoController::onTargetUnregistered(const QString& name)
{
    const QStringList ids = m_router->listTargets();
    m_status = QStringLiteral("listening on %1 — targets: %2 (unregistered '%3')")
                   .arg(m_router->socketPath(), ids.join(QStringLiteral(", ")), name);
    Q_EMIT statusChanged();
}

} // namespace PhosphorIpcDemo
