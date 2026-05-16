// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DBusLayoutService.h"
#include "../../core/logging.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <QCoreApplication>
#include <QDBusError>
#include <QDBusMessage>

namespace PlasmaZones {

namespace {

QDBusMessage callLayoutRegistry(const QString& method, const QVariantList& args = {})
{
    return PhosphorProtocol::ClientHelpers::syncCall(PhosphorProtocol::Service::Interface::LayoutRegistry, method,
                                                     args);
}

QString errorMessage(const QDBusMessage& reply)
{
    return reply.type() == QDBusMessage::ErrorMessage ? QDBusError(reply).message() : QString();
}

} // namespace

DBusLayoutService::DBusLayoutService(QObject* parent)
    : ILayoutService(parent)
{
}

QString DBusLayoutService::loadLayout(const QString& layoutId)
{
    if (layoutId.isEmpty()) {
        Q_EMIT errorOccurred(QCoreApplication::translate("DBusLayoutService", "Layout ID cannot be empty"));
        return QString();
    }

    const QDBusMessage reply = callLayoutRegistry(QStringLiteral("getLayout"), {layoutId});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        const QString err = errorMessage(reply);
        qCWarning(lcDbus) << "loadLayout: failed for" << layoutId << err;
        Q_EMIT errorOccurred(QCoreApplication::translate("DBusLayoutService", "Failed to load layout: %1").arg(err));
        return QString();
    }
    return reply.arguments().constFirst().toString();
}

QString DBusLayoutService::createLayout(const QString& jsonLayout)
{
    if (jsonLayout.isEmpty()) {
        Q_EMIT errorOccurred(QCoreApplication::translate("DBusLayoutService", "Layout JSON cannot be empty"));
        return QString();
    }

    const QDBusMessage reply = callLayoutRegistry(QStringLiteral("createLayoutFromJson"), {jsonLayout});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        const QString err = errorMessage(reply);
        qCWarning(lcDbus) << "createLayout: failed:" << err;
        Q_EMIT errorOccurred(QCoreApplication::translate("DBusLayoutService", "Failed to create layout: %1").arg(err));
        return QString();
    }
    const QString layoutId = reply.arguments().constFirst().toString();
    if (layoutId.isEmpty()) {
        QString error = QCoreApplication::translate("DBusLayoutService", "Created layout but received empty ID");
        qCWarning(lcDbus) << error;
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

    const QDBusMessage reply = callLayoutRegistry(QStringLiteral("updateLayout"), {jsonLayout});
    if (reply.type() != QDBusMessage::ReplyMessage) {
        const QString err = errorMessage(reply);
        qCWarning(lcDbus) << "updateLayout: failed:" << err;
        Q_EMIT errorOccurred(QCoreApplication::translate("DBusLayoutService", "Failed to update layout: %1").arg(err));
        return false;
    }
    return true;
}

QString DBusLayoutService::getLayoutIdForScreen(const QString& screenName)
{
    if (screenName.isEmpty()) {
        qCWarning(lcDbus) << "getLayoutIdForScreen: empty screenName";
        return QString();
    }

    const QDBusMessage reply = callLayoutRegistry(QStringLiteral("getLayoutForScreen"), {screenName});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        qCWarning(lcDbus) << "getLayoutForScreen: failed, screen=" << screenName << errorMessage(reply);
        return QString();
    }
    return reply.arguments().constFirst().toString();
}

void DBusLayoutService::assignLayoutToScreen(const QString& screenName, const QString& layoutId)
{
    if (screenName.isEmpty() || layoutId.isEmpty()) {
        qCWarning(lcDbus) << "assignLayoutToScreen: empty parameters, screen=" << screenName << "layoutId=" << layoutId;
        return;
    }

    const QDBusMessage reply = callLayoutRegistry(QStringLiteral("assignLayoutToScreen"), {screenName, layoutId});
    if (reply.type() != QDBusMessage::ReplyMessage) {
        const QString err = errorMessage(reply);
        qCWarning(lcDbus) << "assignLayoutToScreen: failed for layout" << layoutId << "to screen" << screenName << err;
        Q_EMIT errorOccurred(
            QCoreApplication::translate("DBusLayoutService", "Failed to assign layout to screen: %1").arg(err));
    }
}

} // namespace PlasmaZones
