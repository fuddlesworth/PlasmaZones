// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenprovider.h"
#include "dbusutils.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include "config/settings.h"
#include "core/constants.h"
#include "core/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

QList<ScreenInfo> fetchScreens()
{
    QList<ScreenInfo> result;

    // Get primary screen name from daemon
    QString primaryScreenName;
    QDBusMessage primaryReply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Screen),
                                                       QStringLiteral("getPrimaryScreen"));
    if (primaryReply.type() == QDBusMessage::ReplyMessage && !primaryReply.arguments().isEmpty()) {
        primaryScreenName = primaryReply.arguments().first().toString();
    }

    // Get screens from daemon via D-Bus
    QDBusMessage screenReply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Screen), QStringLiteral("getScreens"));

    if (screenReply.type() == QDBusMessage::ReplyMessage && !screenReply.arguments().isEmpty()) {
        const QStringList screenNames = screenReply.arguments().first().toStringList();

        for (const QString& screenName : screenNames) {
            ScreenInfo info;
            info.name = screenName;
            // Compare physical parent for virtual screens (primary is always a physical ID)
            QString physName = PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenName);
            // For virtual screens, only the first child (vs:0) is considered primary
            // to avoid showing multiple "Primary" badges in the monitor selector.
            if (PhosphorIdentity::VirtualScreenId::isVirtual(screenName)) {
                info.isPrimary =
                    (physName == primaryScreenName && PhosphorIdentity::VirtualScreenId::extractIndex(screenName) == 0);
            } else {
                info.isPrimary = (physName == primaryScreenName);
            }

            QDBusMessage infoReply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Screen),
                                                            QStringLiteral("getScreenInfo"), {screenName});

            if (infoReply.type() == QDBusMessage::ReplyMessage && !infoReply.arguments().isEmpty()) {
                QString infoJson = infoReply.arguments().first().toString();
                QJsonDocument doc = QJsonDocument::fromJson(infoJson.toUtf8());
                if (!doc.isNull() && doc.isObject()) {
                    QJsonObject jsonObj = doc.object();

                    info.screenId =
                        jsonObj.contains(JsonKeys::ScreenId) ? jsonObj[JsonKeys::ScreenId].toString() : screenName;

                    if (jsonObj.contains(JsonKeys::Manufacturer))
                        info.manufacturer = jsonObj[JsonKeys::Manufacturer].toString();
                    if (jsonObj.contains(JsonKeys::Model))
                        info.model = jsonObj[JsonKeys::Model].toString();

                    if (jsonObj.contains(JsonKeys::Geometry)) {
                        QJsonObject geom = jsonObj[JsonKeys::Geometry].toObject();
                        info.width = geom[::PhosphorZones::ZoneJsonKeys::Width].toInt();
                        info.height = geom[::PhosphorZones::ZoneJsonKeys::Height].toInt();
                    }
                    if (jsonObj.contains(::PhosphorZones::ZoneJsonKeys::Name))
                        info.connectorName = jsonObj[::PhosphorZones::ZoneJsonKeys::Name].toString();
                    if (jsonObj.value(JsonKeys::IsVirtualScreen).toBool()) {
                        info.isVirtualScreen = true;
                        info.virtualIndex = PhosphorIdentity::VirtualScreenId::extractIndex(screenName);
                        info.virtualDisplayName = jsonObj.value(JsonKeys::VirtualDisplayName).toString();
                    }
                } else {
                    info.screenId = screenName;
                }
            } else {
                info.screenId = screenName;
            }

            result.append(info);
        }
    }

    // Fallback: if no screens from daemon, get from Qt
    if (result.isEmpty()) {
        QScreen* primaryScreen = QGuiApplication::primaryScreen();
        for (QScreen* screen : QGuiApplication::screens()) {
            ScreenInfo info;
            info.name = screen->name();
            info.isPrimary = (screen == primaryScreen);
            info.manufacturer = screen->manufacturer();
            info.model = screen->model();
            info.width = screen->geometry().width();
            info.height = screen->geometry().height();
            info.connectorName = screen->name();
            info.screenId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
            result.append(info);
        }
    }

    return result;
}

bool isMonitorDisabledFor(const Settings* settings, PhosphorZones::AssignmentEntry::Mode mode,
                          const QString& screenName)
{
    return settings && settings->isMonitorDisabled(mode, screenName);
}

void setMonitorDisabledFor(Settings* settings, PhosphorZones::AssignmentEntry::Mode mode, const QString& screenName,
                           bool disabled, const std::function<void()>& onChanged)
{
    if (!settings || screenName.isEmpty())
        return;

    QString id = Phosphor::Screens::ScreenIdentity::idForName(screenName);
    QStringList list = settings->disabledMonitors(mode);

    if (disabled) {
        if (!list.contains(id)) {
            list.append(id);
            settings->setDisabledMonitors(mode, list);
            if (onChanged)
                onChanged();
        }
    } else {
        bool changed = list.removeAll(id) > 0;
        if (id != screenName) {
            changed |= list.removeAll(screenName) > 0;
        }
        if (changed) {
            settings->setDisabledMonitors(mode, list);
            if (onChanged)
                onChanged();
        }
    }
}

} // namespace PlasmaZones
