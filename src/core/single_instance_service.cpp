// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "single_instance_service.h"

#include "logging.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QObject>

namespace PlasmaZones {

SingleInstanceService::SingleInstanceService(SingleInstanceIds ids, QObject* exportObject)
    : m_ids(std::move(ids))
    , m_exportObject(exportObject)
{
}

SingleInstanceService::~SingleInstanceService()
{
    if (!m_claimed) {
        return;
    }
    auto bus = QDBusConnection::sessionBus();
    bus.unregisterObject(m_ids.objectPath);
    bus.unregisterService(m_ids.serviceName);
}

bool SingleInstanceService::claim()
{
    if (m_claimed) {
        return true;
    }
    if (!m_exportObject) {
        qCWarning(lcCore) << "SingleInstanceService::claim: export object is null";
        return false;
    }

    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(m_ids.serviceName)) {
        return false;
    }
    if (!bus.registerObject(m_ids.objectPath, m_exportObject, QDBusConnection::ExportScriptableSlots)) {
        // Object export failed — release the name immediately so forwarders
        // don't connect to a well-known name with no reachable object.
        qCWarning(lcCore) << "Failed to export object at" << m_ids.objectPath << "— releasing service name"
                          << m_ids.serviceName;
        bus.unregisterService(m_ids.serviceName);
        return false;
    }
    m_claimed = true;
    return true;
}

bool SingleInstanceService::isRunning(const SingleInstanceIds& ids)
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return false;
    }
    auto* busIface = bus.interface();
    if (!busIface) {
        return false;
    }
    return busIface->isServiceRegistered(ids.serviceName);
}

bool SingleInstanceService::forward(const SingleInstanceIds& ids, const QString& method, const QVariantList& args,
                                    int timeoutMs)
{
    // Pre-check via NameHasOwner so we don't construct a QDBusInterface when
    // there's no running instance — that construction can otherwise trigger
    // D-Bus auto-activation if a stray .service file is installed.
    if (!isRunning(ids)) {
        return false;
    }

    auto bus = QDBusConnection::sessionBus();
    QDBusInterface proxy(ids.serviceName, ids.objectPath, ids.interfaceName, bus);
    if (!proxy.isValid()) {
        return false;
    }
    proxy.setTimeout(timeoutMs);
    QDBusMessage reply = proxy.callWithArgumentList(QDBus::Block, method, args);
    // ReplyMessage is the only success type; ErrorMessage and InvalidMessage
    // both mean the forward didn't land.
    return reply.type() == QDBusMessage::ReplyMessage;
}

} // namespace PlasmaZones
