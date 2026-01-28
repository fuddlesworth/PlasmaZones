// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DBusLayoutService.h"
#include "../../core/constants.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QCoreApplication>
#include "../../core/logging.h"

using namespace PlasmaZones;

DBusLayoutService::DBusLayoutService(QObject* parent)
    : ILayoutService(parent)
    , m_serviceName(QString::fromLatin1(DBus::ServiceName))
    , m_objectPath(QString::fromLatin1(DBus::ObjectPath))
    , m_interfaceName(QString::fromLatin1(DBus::Interface::LayoutManager))
    , m_interface(nullptr)
{
}

DBusLayoutService::~DBusLayoutService()
{
    // m_interface has `this` as parent, so Qt handles deletion automatically
    // Just null the pointer to prevent any accidental access
    m_interface = nullptr;
}

QDBusInterface* DBusLayoutService::getInterface()
{
    // Reuse cached interface if valid (performance optimization)
    if (m_interface && m_interface->isValid()) {
        return m_interface;
    }
    
    // Clean up invalid interface
    delete m_interface;
    m_interface = nullptr;
    
    // Create new interface
    m_interface = new QDBusInterface(m_serviceName, m_objectPath, m_interfaceName, QDBusConnection::sessionBus(), this);
    
    if (!m_interface->isValid()) {
        qCWarning(lcEditor) << "Cannot connect to PlasmaZones daemon - service:" << m_serviceName
                            << "path:" << m_objectPath;
        delete m_interface;
        m_interface = nullptr;
        return nullptr;
    }
    
    return m_interface;
}

QString DBusLayoutService::loadLayout(const QString& layoutId)
{
    if (layoutId.isEmpty()) {
        Q_EMIT errorOccurred(QCoreApplication::translate("DBusLayoutService", "Layout ID cannot be empty"));
        return QString();
    }

    QDBusInterface* layoutManager = getInterface();
    if (!layoutManager) {
        QString error = QCoreApplication::translate("DBusLayoutService", "Cannot connect to PlasmaZones daemon");
        Q_EMIT errorOccurred(error);
        return QString();
    }

    QDBusReply<QString> reply = layoutManager->call(QStringLiteral("getLayout"), layoutId);
    if (!reply.isValid()) {
        QString error =
            QCoreApplication::translate("DBusLayoutService", "Failed to load layout: %1").arg(reply.error().message());
        qCWarning(lcEditor) << "Failed to load layout" << layoutId << "-" << reply.error().message();
        Q_EMIT errorOccurred(error);
        return QString();
    }

    return reply.value();
}

QString DBusLayoutService::createLayout(const QString& jsonLayout)
{
    if (jsonLayout.isEmpty()) {
        Q_EMIT errorOccurred(QCoreApplication::translate("DBusLayoutService", "Layout JSON cannot be empty"));
        return QString();
    }

    QDBusInterface* layoutManager = getInterface();
    if (!layoutManager) {
        QString error = QCoreApplication::translate("DBusLayoutService", "Cannot connect to PlasmaZones daemon");
        Q_EMIT errorOccurred(error);
        return QString();
    }

    QDBusReply<QString> reply = layoutManager->call(QStringLiteral("createLayoutFromJson"), jsonLayout);
    if (!reply.isValid()) {
        QString error = QCoreApplication::translate("DBusLayoutService", "Failed to create layout: %1")
                            .arg(reply.error().message());
        qCWarning(lcEditor) << error;
        Q_EMIT errorOccurred(error);
        return QString();
    }

    QString layoutId = reply.value();
    if (layoutId.isEmpty()) {
        QString error = QCoreApplication::translate("DBusLayoutService", "Created layout but received empty ID");
        qCWarning(lcEditor) << error;
        Q_EMIT errorOccurred(error);
        return QString();
    }

    return layoutId;
}

bool DBusLayoutService::updateLayout(const QString& jsonLayout)
{
    if (jsonLayout.isEmpty()) {
        Q_EMIT errorOccurred(QCoreApplication::translate("DBusLayoutService", "Layout JSON cannot be empty"));
        return false;
    }

    QDBusInterface* layoutManager = getInterface();
    if (!layoutManager) {
        QString error = QCoreApplication::translate("DBusLayoutService", "Cannot connect to PlasmaZones daemon");
        Q_EMIT errorOccurred(error);
        return false;
    }

    QDBusReply<void> reply = layoutManager->call(QStringLiteral("updateLayout"), jsonLayout);
    if (!reply.isValid()) {
        QString error = QCoreApplication::translate("DBusLayoutService", "Failed to update layout: %1")
                            .arg(reply.error().message());
        qCWarning(lcEditor) << error;
        Q_EMIT errorOccurred(error);
        return false;
    }

    return true;
}

QString DBusLayoutService::getLayoutIdForScreen(const QString& screenName)
{
    if (screenName.isEmpty()) {
        qCWarning(lcEditor) << "getLayoutIdForScreen called with empty screenName";
        return QString();
    }

    QDBusInterface* layoutManager = getInterface();
    if (!layoutManager) {
        qCWarning(lcEditor) << "Cannot connect for getLayoutIdForScreen(" << screenName << ")";
        return QString();
    }

    QDBusReply<QString> reply = layoutManager->call(QStringLiteral("getLayoutForScreen"), screenName);
    if (!reply.isValid()) {
        qCWarning(lcEditor) << "Failed to get layout for screen" << screenName << "-" << reply.error().message();
        return QString();
    }

    return reply.value();
}

void DBusLayoutService::assignLayoutToScreen(const QString& screenName, const QString& layoutId)
{
    if (screenName.isEmpty() || layoutId.isEmpty()) {
        qCWarning(lcEditor) << "assignLayoutToScreen called with empty parameters - screen:" << screenName
                            << "layoutId:" << layoutId;
        return;
    }

    QDBusInterface* layoutManager = getInterface();
    if (!layoutManager) {
        QString error = QCoreApplication::translate("DBusLayoutService", "Cannot connect to PlasmaZones daemon");
        Q_EMIT errorOccurred(error);
        return;
    }

    QDBusReply<void> reply = layoutManager->call(QStringLiteral("assignLayoutToScreen"), screenName, layoutId);
    if (!reply.isValid()) {
        QString error = QCoreApplication::translate("DBusLayoutService", "Failed to assign layout to screen: %1")
                            .arg(reply.error().message());
        qCWarning(lcEditor) << "Failed to assign layout" << layoutId << "to screen" << screenName << "-"
                            << reply.error().message();
        Q_EMIT errorOccurred(error);
    }
}
