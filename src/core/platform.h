// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QString>

namespace PlasmaZones {

/**
 * @brief Platform detection and utilities
 *
 * Provides runtime detection of Wayland display server.
 * PlasmaZones requires Wayland with LayerShellQt - X11 is not supported.
 */
namespace Platform {

/**
 * @brief Check if running on Wayland
 * @return true if WAYLAND_DISPLAY environment variable is set
 */
PLASMAZONES_EXPORT bool isWayland();

/**
 * @brief Get the display server name
 * @return "wayland" or "unknown"
 */
PLASMAZONES_EXPORT QString displayServer();

/**
 * @brief Check if the platform is supported
 * @return true if running on Wayland (required for PlasmaZones)
 */
PLASMAZONES_EXPORT bool isSupported();

} // namespace Platform

} // namespace PlasmaZones
