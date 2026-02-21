// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace PlasmaZones {

/**
 * @brief D-Bus service constants for KWin effect
 *
 * Centralized D-Bus interface names to avoid magic strings
 * throughout the effect code.
 */
namespace DBus {
inline const QString ServiceName = QStringLiteral("org.plasmazones");
inline const QString ObjectPath = QStringLiteral("/PlasmaZones");

namespace Interface {
inline const QString Settings = QStringLiteral("org.plasmazones.Settings");
inline const QString WindowDrag = QStringLiteral("org.plasmazones.WindowDrag");
inline const QString WindowTracking = QStringLiteral("org.plasmazones.WindowTracking");
inline const QString Overlay = QStringLiteral("org.plasmazones.Overlay");
inline const QString Autotile = QStringLiteral("org.plasmazones.Autotile");
}
}

} // namespace PlasmaZones
