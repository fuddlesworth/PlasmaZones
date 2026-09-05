// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overviewadaptor.h"

#include "core/platform/logging.h"
#include "kwinsendertrust.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusServiceWatcher>

namespace PlasmaZones {

OverviewAdaptor::OverviewAdaptor(QObject* parent)
    : QDBusAbstractAdaptor(parent)
{
    m_kwinTrust = new KwinSenderTrust(this);
}

OverviewAdaptor::~OverviewAdaptor() = default;

IOverviewPolicy* OverviewAdaptor::controller() const
{
    return m_controller;
}

void OverviewAdaptor::setController(IOverviewPolicy* controller)
{
    if (m_controller == controller) {
        return;
    }
    if (m_controller) {
        disconnect(m_controller, nullptr, this, nullptr);
        // Feature teardown underneath an open overview: close the gate here
        // (the controller is going away, its own close would be lost) and
        // tell the effect to let go.
        if (m_open) {
            applyOpen(false, QString());
            Q_EMIT closeOverviewRequested();
        }
    }
    m_controller = controller;
    if (!m_controller) {
        return;
    }
    connect(m_controller, &IOverviewPolicy::modelPublished, this, &OverviewAdaptor::onControllerModel);
    connect(m_controller, &IOverviewPolicy::closeRequested, this, [this]() {
        if (m_open) {
            applyOpen(false, QString());
        }
        Q_EMIT closeOverviewRequested();
    });
}

void OverviewAdaptor::requestToggle()
{
    if (!m_controller) {
        qCDebug(lcDbus) << "overview: toggle ignored, workspaces feature is off";
        return;
    }
    Q_EMIT toggleOverviewRequested();
}

void OverviewAdaptor::requestClose()
{
    if (m_open) {
        applyOpen(false, QString());
    }
    Q_EMIT closeOverviewRequested();
}

bool OverviewAdaptor::authenticate()
{
    if (!calledFromDBus()) {
        return true;
    }
    if (m_kwinTrust->isTrustedSender(message().service(), connection())) {
        return true;
    }
    qCWarning(lcDbus) << "overview: rejecting call from unauthenticated sender" << message().service();
    return false;
}

void OverviewAdaptor::applyOpen(bool open, const QString& owner)
{
    if (m_ownerWatcher) {
        m_ownerWatcher->deleteLater();
        m_ownerWatcher = nullptr;
    }
    m_open = open;
    m_owner = open ? owner : QString();
    if (open && !owner.isEmpty()) {
        // The opener's unique name: when it vanishes (effect crash, kwin
        // restart) the overview is gone with it, and the model must stop
        // streaming to nobody.
        m_ownerWatcher = new QDBusServiceWatcher(owner, QDBusConnection::sessionBus(),
                                                 QDBusServiceWatcher::WatchForUnregistration, this);
        connect(m_ownerWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this](const QString& name) {
            if (!m_open || name != m_owner) {
                return;
            }
            qCDebug(lcDbus) << "overview: owner" << name << "vanished, closing";
            applyOpen(false, QString());
        });
    }
    if (m_controller) {
        m_controller->setOpen(open);
    }
    Q_EMIT overviewStateChanged(m_open);
}

void OverviewAdaptor::setOverviewOpen(bool open)
{
    if (!authenticate()) {
        return;
    }
    const QString sender = calledFromDBus() ? message().service() : QString();
    if (!m_controller) {
        qCDebug(lcDbus) << "overview: setOverviewOpen ignored, workspaces feature is off";
        return;
    }
    if (open) {
        if (m_open && sender == m_owner) {
            return;
        }
        applyOpen(true, sender);
        return;
    }
    if (!m_open) {
        return;
    }
    // Only the opener closes. A stray close from another peer must not tear
    // down a running overview.
    if (sender != m_owner) {
        qCDebug(lcDbus) << "overview: close from" << sender << "ignored, owner is" << m_owner;
        return;
    }
    applyOpen(false, QString());
}

QString OverviewAdaptor::overviewModel() const
{
    if (!m_open || !m_controller) {
        return QString();
    }
    return m_controller->modelJson();
}

void OverviewAdaptor::reportOverviewState(bool open)
{
    if (!authenticate()) {
        return;
    }
    const QString sender = calledFromDBus() ? message().service() : QString();
    // A report of "not running" from the owner while the gate is open means
    // the effect's start was refused (another fullscreen effect, a failed
    // keyboard grab) after it had already opened the gate; close it so the
    // stream stops. A report of "running" is informational: the gate opens
    // through setOverviewOpen, which the effect calls first.
    if (!open && m_open && sender == m_owner) {
        applyOpen(false, QString());
    }
}

void OverviewAdaptor::onControllerModel(const QString& modelJson)
{
    if (!m_open) {
        return;
    }
    Q_EMIT overviewModelChanged(modelJson);
}

} // namespace PlasmaZones
