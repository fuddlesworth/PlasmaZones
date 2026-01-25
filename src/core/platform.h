// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QString>

namespace PlasmaZones {

/**
 * @brief Platform detection and utilities
 *
 * Provides runtime detection of display server (Wayland/X11)
 * and platform-specific features.
 */
namespace Platform {

/**
 * @brief Check if running on Wayland
 * @return true if WAYLAND_DISPLAY environment variable is set
 */
PLASMAZONES_EXPORT bool isWayland();

/**
 * @brief Check if running on X11
 * @return true if DISPLAY is set and not Wayland
 */
PLASMAZONES_EXPORT bool isX11();

/**
 * @brief Get the display server name
 * @return "wayland", "x11", or "unknown"
 */
PLASMAZONES_EXPORT QString displayServer();

/**
 * @brief Check if LayerShellQt is available at runtime
 * @return true if LayerShellQt is compiled in and available
 */
PLASMAZONES_EXPORT bool hasLayerShell();

/**
 * @brief Check if overlay support is available
 * @return true if either LayerShellQt is available or running on X11
 */
PLASMAZONES_EXPORT bool hasOverlaySupport();

} // namespace Platform

} // namespace PlasmaZones
