// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DBusLayoutService.h"
#include "core/platform/logging.h"

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
    // An empty document is how getLayout reports a layout it does not hold, and
    // that is a ReplyMessage like any other. The caller reads an empty return as
    // failure and stays silent on the strength of this method having reported
    // it, so the report has to happen here or the load fails with no diagnostic.
    const QString jsonLayout = reply.arguments().constFirst().toString();
    if (jsonLayout.isEmpty()) {
        qCWarning(lcDbus) << "loadLayout: daemon holds no layout with id" << layoutId;
        Q_EMIT errorOccurred(QCoreApplication::translate("DBusLayoutService", "That layout is no longer available."));
        return QString();
    }
    return jsonLayout;
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
    // A reply arrived, which only says the daemon was reachable. updateLayout is
    // declared bool and answers false for a payload it refused (schema gate,
    // unknown layout id, empty autotile algorithm key), and that refusal is a
    // ReplyMessage like any other. Reporting success on the message type alone
    // would let the editor clear its unsaved-changes flag and undo stack over a
    // write the daemon threw away.
    if (reply.arguments().isEmpty() || !reply.arguments().constFirst().toBool()) {
        qCWarning(lcDbus) << "updateLayout: daemon rejected the layout";
        Q_EMIT errorOccurred(QCoreApplication::translate("DBusLayoutService", "The layout could not be saved."));
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
