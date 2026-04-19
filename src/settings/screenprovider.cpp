// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenprovider.h"
#include "dbusutils.h"
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include "../../src/config/settings.h"
#include "../../src/core/constants.h"
#include "../../src/core/utils.h"
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

QList<ScreenInfo> fetchScreens()
{
    QList<ScreenInfo> result;

    // Get primary screen name from daemon
    QString primaryScreenName;
    QDBusMessage primaryReply =
        DaemonDBus::callDaemon(QString(DBus::Interface::Screen), QStringLiteral("getPrimaryScreen"));
    if (primaryReply.type() == QDBusMessage::ReplyMessage && !primaryReply.arguments().isEmpty()) {
        primaryScreenName = primaryReply.arguments().first().toString();
    }

    // Get screens from daemon via D-Bus
    QDBusMessage screenReply = DaemonDBus::callDaemon(QString(DBus::Interface::Screen), QStringLiteral("getScreens"));

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

            QDBusMessage infoReply =
                DaemonDBus::callDaemon(QString(DBus::Interface::Screen), QStringLiteral("getScreenInfo"), {screenName});

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

QVariantList screenInfoListToVariantList(const QList<ScreenInfo>& screens)
{
    QVariantList list;
    list.reserve(screens.size());

    for (const ScreenInfo& s : screens) {
        QVariantMap map;
        map[QStringLiteral("name")] = s.name;
        map[QStringLiteral("isPrimary")] = s.isPrimary;
        if (!s.manufacturer.isEmpty())
            map[QStringLiteral("manufacturer")] = s.manufacturer;
        if (!s.model.isEmpty())
            map[QStringLiteral("model")] = s.model;
        if (s.width > 0 && s.height > 0) {
            map[QStringLiteral("resolution")] = QStringLiteral("%1\u00d7%2").arg(s.width).arg(s.height);
            map[QStringLiteral("width")] = s.width;
            map[QStringLiteral("height")] = s.height;
        }
        if (!s.screenId.isEmpty())
            map[QStringLiteral("screenId")] = s.screenId;
        if (s.isVirtualScreen) {
            map[QStringLiteral("isVirtualScreen")] = true;
            map[QStringLiteral("virtualIndex")] = s.virtualIndex;
            if (!s.virtualDisplayName.isEmpty())
                map[QStringLiteral("virtualDisplayName")] = s.virtualDisplayName;
        }
        if (!s.connectorName.isEmpty())
            map[QStringLiteral("connectorName")] = s.connectorName;

        // Pre-computed display label for QML consumers (context menus, selectors, etc.).
        // Single source of truth — avoids duplicating label-building logic in QML.
        {
            QString label;
            if (s.isVirtualScreen) {
                QString vsName = s.virtualDisplayName.isEmpty() ? QStringLiteral("VS%1").arg(s.virtualIndex + 1)
                                                                : s.virtualDisplayName;
                QStringList parts;
                if (!s.manufacturer.isEmpty())
                    parts.append(s.manufacturer);
                if (!s.model.isEmpty())
                    parts.append(s.model);
                QString monitorName = parts.isEmpty() ? s.connectorName : parts.join(QLatin1Char(' '));
                label = monitorName.isEmpty() ? vsName : vsName + QStringLiteral(" \u2014 ") + monitorName;
            } else {
                QStringList parts;
                if (!s.manufacturer.isEmpty())
                    parts.append(s.manufacturer);
                if (!s.model.isEmpty())
                    parts.append(s.model);
                label = parts.isEmpty() ? s.name : parts.join(QLatin1Char(' '));
            }
            if (s.width > 0 && s.height > 0) {
                label += QStringLiteral(" (%1\u00d7%2)").arg(s.width).arg(s.height);
            }
            map[QStringLiteral("displayLabel")] = label;
        }

        list.append(map);
    }

    return list;
}

bool isMonitorDisabledFor(const Settings* settings, const QString& screenName)
{
    return settings && settings->isMonitorDisabled(screenName);
}

void setMonitorDisabledFor(Settings* settings, const QString& screenName, bool disabled,
                           const std::function<void()>& onChanged)
{
    if (!settings || screenName.isEmpty())
        return;

    QString id = Phosphor::Screens::ScreenIdentity::idForName(screenName);
    QStringList list = settings->disabledMonitors();

    if (disabled) {
        if (!list.contains(id)) {
            list.append(id);
            settings->setDisabledMonitors(list);
            if (onChanged)
                onChanged();
        }
    } else {
        bool changed = list.removeAll(id) > 0;
        if (id != screenName) {
            changed |= list.removeAll(screenName) > 0;
        }
        if (changed) {
            settings->setDisabledMonitors(list);
            if (onChanged)
                onChanged();
        }
    }
}

} // namespace PlasmaZones
