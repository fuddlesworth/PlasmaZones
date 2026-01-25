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
{
}

QString DBusLayoutService::loadLayout(const QString& layoutId)
{
    if (layoutId.isEmpty()) {
        Q_EMIT errorOccurred(QCoreApplication::translate("DBusLayoutService", "Layout ID cannot be empty"));
        return QString();
    }

    QDBusInterface layoutManager(m_serviceName, m_objectPath, m_interfaceName, QDBusConnection::sessionBus());

    if (!layoutManager.isValid()) {
        QString error = QCoreApplication::translate("DBusLayoutService", "Cannot connect to PlasmaZones daemon");
        qCWarning(lcEditor) << "Cannot connect for loadLayout(" << layoutId << ") - service:" << m_serviceName
                            << "path:" << m_objectPath;
        Q_EMIT errorOccurred(error);
        return QString();
    }

    QDBusReply<QString> reply = layoutManager.call(QStringLiteral("getLayout"), layoutId);
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

    QDBusInterface layoutManager(m_serviceName, m_objectPath, m_interfaceName, QDBusConnection::sessionBus());

    if (!layoutManager.isValid()) {
        QString error = QCoreApplication::translate("DBusLayoutService", "Cannot connect to PlasmaZones daemon");
        qCWarning(lcEditor) << "Cannot connect for createLayout - service:" << m_serviceName << "path:" << m_objectPath;
        Q_EMIT errorOccurred(error);
        return QString();
    }

    QDBusReply<QString> reply = layoutManager.call(QStringLiteral("createLayoutFromJson"), jsonLayout);
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

    QDBusInterface layoutManager(m_serviceName, m_objectPath, m_interfaceName, QDBusConnection::sessionBus());

    if (!layoutManager.isValid()) {
        QString error = QCoreApplication::translate("DBusLayoutService", "Cannot connect to PlasmaZones daemon");
        qCWarning(lcEditor) << "Cannot connect for updateLayout - service:" << m_serviceName << "path:" << m_objectPath;
        Q_EMIT errorOccurred(error);
        return false;
    }

    QDBusReply<void> reply = layoutManager.call(QStringLiteral("updateLayout"), jsonLayout);
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

    QDBusInterface layoutManager(m_serviceName, m_objectPath, m_interfaceName, QDBusConnection::sessionBus());

    if (!layoutManager.isValid()) {
        qCWarning(lcEditor) << "Cannot connect for getLayoutIdForScreen(" << screenName
                            << ") - service:" << m_serviceName;
        return QString();
    }

    QDBusReply<QString> reply = layoutManager.call(QStringLiteral("getLayoutForScreen"), screenName);
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

    QDBusInterface layoutManager(m_serviceName, m_objectPath, m_interfaceName, QDBusConnection::sessionBus());

    if (!layoutManager.isValid()) {
        QString error = QCoreApplication::translate("DBusLayoutService", "Cannot connect to PlasmaZones daemon");
        qCWarning(lcEditor) << "Cannot connect for assignLayoutToScreen(" << screenName << "," << layoutId
                            << ") - service:" << m_serviceName;
        Q_EMIT errorOccurred(error);
        return;
    }

    QDBusReply<void> reply = layoutManager.call(QStringLiteral("assignLayoutToScreen"), screenName, layoutId);
    if (!reply.isValid()) {
        QString error = QCoreApplication::translate("DBusLayoutService", "Failed to assign layout to screen: %1")
                            .arg(reply.error().message());
        qCWarning(lcEditor) << "Failed to assign layout" << layoutId << "to screen" << screenName << "-"
                            << reply.error().message();
        Q_EMIT errorOccurred(error);
    }
}
