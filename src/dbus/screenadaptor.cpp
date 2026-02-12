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
        Q_EMIT screenAdded(screen->name());

        connect(screen, &QScreen::geometryChanged, this, [this, screen]() {
            Q_EMIT screenGeometryChanged(screen->name());
        });
    });

    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen* screen) {
        Q_EMIT screenRemoved(screen->name());
    });

    // Connect existing screens
    for (auto* screen : Utils::allScreens()) {
        connect(screen, &QScreen::geometryChanged, this, [this, screen]() {
            Q_EMIT screenGeometryChanged(screen->name());
        });
    }
}

QStringList ScreenAdaptor::getScreens()
{
    QStringList result;
    for (const auto* screen : Utils::allScreens()) {
        result.append(screen->name());
    }
    return result;
}

QString ScreenAdaptor::getScreenInfo(const QString& screenName)
{
    if (screenName.isEmpty()) {
        qCWarning(lcDbus) << "Cannot get screen info - empty screen name";
        return QString();
    }

    for (const auto* screen : Utils::allScreens()) {
        if (screen->name() == screenName) {
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

            return QString::fromUtf8(QJsonDocument(info).toJson());
        }
    }

    qCWarning(lcDbus) << "Screen not found:" << screenName;
    return QString();
}

QString ScreenAdaptor::getPrimaryScreen()
{
    auto* primary = Utils::primaryScreen();
    return primary ? primary->name() : QString();
}

} // namespace PlasmaZones
