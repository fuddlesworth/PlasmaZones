// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QStringList>

namespace PlasmaZones {

/**
 * @brief D-Bus adaptor for screen management operations
 *
 * Provides D-Bus interface: org.plasmazones.Screen
 *  Screen information and monitoring
 *
 * NOTE: Interface name must match dbus/org.plasmazones.Screen.xml and
 * DBus::Interface::Screen constant for KCM signal connections to work.
 */
class PLASMAZONES_EXPORT ScreenAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Screen")

public:
    explicit ScreenAdaptor(QObject* parent = nullptr);
    ~ScreenAdaptor() override = default;

public Q_SLOTS:
    // Screen queries
    QStringList getScreens();
    QString getScreenInfo(const QString& screenName);
    QString getPrimaryScreen();
    int getScreenCount();

Q_SIGNALS:
    void screenAdded(const QString& screenName);
    void screenRemoved(const QString& screenName);
    void screenGeometryChanged(const QString& screenName);
};

} // namespace PlasmaZones
