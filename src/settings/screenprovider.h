// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorScreens/ScreenInfo.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <QDBusConnection>
#include <QList>
#include <QString>
#include <QVariantList>
#include <functional>
#include "core/constants.h"

namespace PlasmaZones {

class Settings;

/// Re-export of the lib's POD so PlasmaZones-internal callers don't need the
/// `Phosphor::Screens::` prefix. Single update-site for any future renames.
using ScreenInfo = Phosphor::Screens::ScreenInfo;

/**
 * @brief Fetch the list of connected screens via D-Bus (daemon) with Qt fallback
 *
 * Each ScreenInfo contains connector name, primary flag, manufacturer, model,
 * resolution, and the stable EDID-based screenId from the daemon. This call
 * stays in PlasmaZones because it speaks the PlasmaZones daemon's specific
 * D-Bus protocol; the generic ScreenInfo POD itself lives in phosphor-screens.
 */
QList<ScreenInfo> fetchScreens();

/**
 * @brief Check whether a given monitor is disabled in settings for the given mode
 * @param settings The Settings instance to query
 * @param mode The mode whose disable list to check
 * @param screenName The connector name of the screen
 */
bool isMonitorDisabledFor(const Settings* settings, PhosphorZones::AssignmentEntry::Mode mode,
                          const QString& screenName);

/**
 * @brief Enable or disable a monitor in settings for the given mode
 * @param settings The Settings instance to modify
 * @param mode The mode whose disable list to modify
 * @param screenName The connector name of the screen
 * @param disabled Whether to disable (true) or enable (false)
 * @param onChanged Callback invoked when the disabled list actually changes
 */
void setMonitorDisabledFor(Settings* settings, PhosphorZones::AssignmentEntry::Mode mode, const QString& screenName,
                           bool disabled, const std::function<void()>& onChanged);

/**
 * @brief Connect D-Bus screen change signals to a receiver's refreshScreens() slot.
 *
 * Call this in KCM constructors that need screen change tracking.
 */
inline void connectScreenChangeSignals(QObject* receiver)
{
    QDBusConnection::sessionBus().connect(QString(PhosphorProtocol::Service::Name),
                                          QString(PhosphorProtocol::Service::ObjectPath),
                                          QString(PhosphorProtocol::Service::Interface::Screen),
                                          QStringLiteral("screenAdded"), receiver, SLOT(refreshScreens()));
    QDBusConnection::sessionBus().connect(QString(PhosphorProtocol::Service::Name),
                                          QString(PhosphorProtocol::Service::ObjectPath),
                                          QString(PhosphorProtocol::Service::Interface::Screen),
                                          QStringLiteral("screenRemoved"), receiver, SLOT(refreshScreens()));
}

} // namespace PlasmaZones
