// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusConnection>
#include <QList>
#include <QString>
#include <QVariantList>
#include <functional>
#include "../../src/core/constants.h"

namespace PlasmaZones {

class Settings;

/**
 * @brief Lightweight screen descriptor returned by fetchScreens()
 */
struct ScreenInfo
{
    QString name;
    bool isPrimary = false;
    QString manufacturer;
    QString model;
    int width = 0;
    int height = 0;
    QString screenId;
    bool isVirtualScreen = false;
    QString connectorName; ///< Physical connector (e.g. "DP-2")
    int virtualIndex = -1; ///< 0-based index within the physical screen (-1 = not virtual)
    QString virtualDisplayName; ///< User-facing name (e.g. "Left", "Right")
};

/**
 * @brief Fetch the list of connected screens via D-Bus (daemon) with Qt fallback
 *
 * Each ScreenInfo contains connector name, primary flag, manufacturer, model,
 * resolution, and the stable EDID-based screenId from the daemon.
 */
QList<ScreenInfo> fetchScreens();

/**
 * @brief Convert a ScreenInfo list to QVariantList suitable for QML consumption
 *
 * Each entry is a QVariantMap with keys: name, isPrimary, manufacturer, model,
 * resolution, screenId.
 */
QVariantList screenInfoListToVariantList(const QList<ScreenInfo>& screens);

/**
 * @brief Check whether a given monitor is disabled in settings
 * @param settings The Settings instance to query
 * @param screenName The connector name of the screen
 */
bool isMonitorDisabledFor(const Settings* settings, const QString& screenName);

/**
 * @brief Enable or disable a monitor in settings
 * @param settings The Settings instance to modify
 * @param screenName The connector name of the screen
 * @param disabled Whether to disable (true) or enable (false)
 * @param onChanged Callback invoked when the disabled list actually changes
 */
void setMonitorDisabledFor(Settings* settings, const QString& screenName, bool disabled,
                           const std::function<void()>& onChanged);

/**
 * @brief Connect D-Bus screen change signals to a receiver's refreshScreens() slot.
 *
 * Call this in KCM constructors that need screen change tracking.
 */
inline void connectScreenChangeSignals(QObject* receiver)
{
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::Screen), QStringLiteral("screenAdded"), receiver,
                                          SLOT(refreshScreens()));
    QDBusConnection::sessionBus().connect(QString(DBus::ServiceName), QString(DBus::ObjectPath),
                                          QString(DBus::Interface::Screen), QStringLiteral("screenRemoved"), receiver,
                                          SLOT(refreshScreens()));
}

} // namespace PlasmaZones
