// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"
#include <QLoggingCategory>

/**
 * @file logging.h
 * @brief Centralized logging categories for PlasmaZones
 *
 * This file defines Qt logging categories that enable runtime filtering
 * of log messages. Use these categories instead of plain qDebug/qWarning.
 *
 * Usage:
 *   #include "logging.h"
 *   qCDebug(lcCore) << "Debug message";
 *   qCInfo(lcCore) << "Info message";
 *   qCWarning(lcCore) << "Warning message";
 *   qCCritical(lcCore) << "Critical message";
 *
 * Runtime filtering via environment variable:
 *   QT_LOGGING_RULES="plasmazones.*=true"                    # Enable all
 *   QT_LOGGING_RULES="plasmazones.*.debug=false"             # Disable debug only
 *   QT_LOGGING_RULES="plasmazones.dbus.*=true"               # Enable D-Bus only
 *   QT_LOGGING_RULES="plasmazones.editor.snapping=true"      # Enable snapping only
 *
 * Severity Guidelines:
 *   qCDebug    - Development tracing, disabled in release builds
 *   qCInfo     - Significant operational events (startup, shutdown, layout loaded)
 *   qCWarning  - Recoverable errors, invalid input, missing resources
 *   qCCritical - System failures preventing normal operation
 */

namespace PlasmaZones {

// Core module - layout, zone, detection, screen management
PLASMAZONES_EXPORT Q_DECLARE_LOGGING_CATEGORY(lcCore) PLASMAZONES_EXPORT
    Q_DECLARE_LOGGING_CATEGORY(lcLayout) PLASMAZONES_EXPORT Q_DECLARE_LOGGING_CATEGORY(lcZone) PLASMAZONES_EXPORT
    Q_DECLARE_LOGGING_CATEGORY(lcScreen)

    // Daemon module - overlay, shortcuts, zone selector
    PLASMAZONES_EXPORT Q_DECLARE_LOGGING_CATEGORY(lcDaemon) PLASMAZONES_EXPORT
    Q_DECLARE_LOGGING_CATEGORY(lcOverlay) PLASMAZONES_EXPORT Q_DECLARE_LOGGING_CATEGORY(lcShortcuts)

    // D-Bus module - all D-Bus adaptor communication
    PLASMAZONES_EXPORT Q_DECLARE_LOGGING_CATEGORY(lcDbus) PLASMAZONES_EXPORT
    Q_DECLARE_LOGGING_CATEGORY(lcDbusLayout) PLASMAZONES_EXPORT
    Q_DECLARE_LOGGING_CATEGORY(lcDbusWindow) PLASMAZONES_EXPORT Q_DECLARE_LOGGING_CATEGORY(lcDbusSettings)

    // Editor module - controller, zones, undo, services
    PLASMAZONES_EXPORT Q_DECLARE_LOGGING_CATEGORY(lcEditor) PLASMAZONES_EXPORT
    Q_DECLARE_LOGGING_CATEGORY(lcEditorZone) PLASMAZONES_EXPORT
    Q_DECLARE_LOGGING_CATEGORY(lcEditorUndo) PLASMAZONES_EXPORT Q_DECLARE_LOGGING_CATEGORY(lcSnapping)

    // KWin effect module - snap visualization, navigation
    PLASMAZONES_EXPORT Q_DECLARE_LOGGING_CATEGORY(lcEffect)

    // Configuration module - settings loading/saving
    PLASMAZONES_EXPORT Q_DECLARE_LOGGING_CATEGORY(lcConfig)

    // KCM module - System Settings integration
    PLASMAZONES_EXPORT Q_DECLARE_LOGGING_CATEGORY(lcKcm)

} // namespace PlasmaZones
