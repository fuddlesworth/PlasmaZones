// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenadaptor.h"
#include "../core/constants.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include <QGuiApplication>
#include <QScreen>
#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

ScreenAdaptor::ScreenAdaptor(QObject* parent)
    : QDBusAbstractAdaptor(parent)
{
    // Connect to screen change signals
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
        Q_EMIT screenAdded(Utils::screenIdentifier(screen));

        connect(screen, &QScreen::geometryChanged, this, [this, screen]() {
            Q_EMIT screenGeometryChanged(Utils::screenIdentifier(screen));
        });
    });

    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen* screen) {
        Q_EMIT screenRemoved(Utils::screenIdentifier(screen));
    });

    // Connect existing screens
    for (auto* screen : Utils::allScreens()) {
        connect(screen, &QScreen::geometryChanged, this, [this, screen]() {
            Q_EMIT screenGeometryChanged(Utils::screenIdentifier(screen));
        });
    }
}

QStringList ScreenAdaptor::getScreens()
{
    QStringList result;
    for (const auto* screen : Utils::allScreens()) {
        result.append(Utils::screenIdentifier(screen));
    }
    return result;
}

QString ScreenAdaptor::getScreenInfo(const QString& screenId)
{
    if (screenId.isEmpty()) {
        qCWarning(lcDbus) << "getScreenInfo: empty screen name";
        return QString();
    }

    const QScreen* screen = Utils::findScreenByIdOrName(screenId);
    if (screen) {
        QJsonObject info;
        info[JsonKeys::Name] = screen->name();
        info[JsonKeys::Manufacturer] = screen->manufacturer();
        info[JsonKeys::Model] = screen->model();
        info[JsonKeys::Geometry] = QJsonObject{{JsonKeys::X, screen->geometry().x()},
                                               {JsonKeys::Y, screen->geometry().y()},
                                               {JsonKeys::Width, screen->geometry().width()},
                                               {JsonKeys::Height, screen->geometry().height()}};
        info[JsonKeys::PhysicalSize] = QJsonObject{{JsonKeys::Width, screen->physicalSize().width()},
                                                   {JsonKeys::Height, screen->physicalSize().height()}};
        info[JsonKeys::DevicePixelRatio] = screen->devicePixelRatio();
        info[JsonKeys::RefreshRate] = screen->refreshRate();
        info[JsonKeys::Depth] = screen->depth();
        info[JsonKeys::ScreenId] = Utils::screenIdentifier(screen);
        info[QLatin1String("serialNumber")] = screen->serialNumber();

        return QString::fromUtf8(QJsonDocument(info).toJson());
    }

    qCWarning(lcDbus) << "Screen not found:" << screenId;
    return QString();
}

QString ScreenAdaptor::getPrimaryScreen()
{
    // Prefer KWin-sourced override (from Workspace::outputOrder) over Qt's
    // QGuiApplication::primaryScreen(), which may diverge from KDE Display
    // Settings on some Wayland configurations.
    if (!m_primaryScreenOverride.isEmpty()) {
        QScreen* overrideScreen = Utils::findScreenByName(m_primaryScreenOverride);
        if (overrideScreen) {
            return Utils::screenIdentifier(overrideScreen);
        }
    }
    auto* primary = Utils::primaryScreen();
    return primary ? Utils::screenIdentifier(primary) : QString();
}

void ScreenAdaptor::setPrimaryScreenFromKWin(const QString& connectorName)
{
    m_primaryScreenOverride = connectorName;
    qCInfo(lcDbus) << "Primary screen override set from KWin:" << connectorName;
}

QString ScreenAdaptor::getScreenId(const QString& connectorName)
{
    if (connectorName.isEmpty()) {
        return QString();
    }
    return Utils::screenIdForName(connectorName);
}

} // namespace PlasmaZones
