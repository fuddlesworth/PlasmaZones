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

namespace PlasmaZones {

QList<ScreenInfo> fetchScreens()
{
    QList<ScreenInfo> result;

    // Get primary screen name from daemon
    QString primaryScreenName;
    QDBusMessage primaryReply =
        KCMDBus::callDaemon(QString(DBus::Interface::Screen), QStringLiteral("getPrimaryScreen"));
    if (primaryReply.type() == QDBusMessage::ReplyMessage && !primaryReply.arguments().isEmpty()) {
        primaryScreenName = primaryReply.arguments().first().toString();
    }

    // Get screens from daemon via D-Bus
    QDBusMessage screenReply = KCMDBus::callDaemon(QString(DBus::Interface::Screen), QStringLiteral("getScreens"));

    if (screenReply.type() == QDBusMessage::ReplyMessage && !screenReply.arguments().isEmpty()) {
        const QStringList screenNames = screenReply.arguments().first().toStringList();

        for (const QString& screenName : screenNames) {
            ScreenInfo info;
            info.name = screenName;
            info.isPrimary = (screenName == primaryScreenName);

            QDBusMessage infoReply =
                KCMDBus::callDaemon(QString(DBus::Interface::Screen), QStringLiteral("getScreenInfo"), {screenName});

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
                        info.width = geom[JsonKeys::Width].toInt();
                        info.height = geom[JsonKeys::Height].toInt();
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
            info.screenId = screen->name();
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
        if (s.width > 0 && s.height > 0)
            map[QStringLiteral("resolution")] = QStringLiteral("%1\u00d7%2").arg(s.width).arg(s.height);
        if (!s.screenId.isEmpty())
            map[QStringLiteral("screenId")] = s.screenId;
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

    QString id = Utils::screenIdForName(screenName);
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
