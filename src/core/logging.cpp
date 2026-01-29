// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "logging.h"

namespace PlasmaZones {

// Core module categories
Q_LOGGING_CATEGORY(lcCore, "plasmazones.core", QtInfoMsg)
Q_LOGGING_CATEGORY(lcLayout, "plasmazones.core.layout", QtInfoMsg)
Q_LOGGING_CATEGORY(lcZone, "plasmazones.core.zone", QtInfoMsg)
Q_LOGGING_CATEGORY(lcScreen, "plasmazones.core.screen", QtInfoMsg)

// Daemon module categories
Q_LOGGING_CATEGORY(lcDaemon, "plasmazones.daemon", QtInfoMsg)
Q_LOGGING_CATEGORY(lcOverlay, "plasmazones.daemon.overlay", QtInfoMsg)
Q_LOGGING_CATEGORY(lcShortcuts, "plasmazones.daemon.shortcuts", QtInfoMsg)

// D-Bus module categories
Q_LOGGING_CATEGORY(lcDbus, "plasmazones.dbus", QtInfoMsg)
Q_LOGGING_CATEGORY(lcDbusLayout, "plasmazones.dbus.layout", QtInfoMsg)
Q_LOGGING_CATEGORY(lcDbusWindow, "plasmazones.dbus.window", QtInfoMsg)
Q_LOGGING_CATEGORY(lcDbusSettings, "plasmazones.dbus.settings", QtInfoMsg)

// Editor module categories
Q_LOGGING_CATEGORY(lcEditor, "plasmazones.editor", QtInfoMsg)
Q_LOGGING_CATEGORY(lcEditorZone, "plasmazones.editor.zone", QtInfoMsg)
Q_LOGGING_CATEGORY(lcEditorUndo, "plasmazones.editor.undo", QtInfoMsg)
Q_LOGGING_CATEGORY(lcSnapping, "plasmazones.editor.snapping", QtInfoMsg)

// KWin effect module categories
Q_LOGGING_CATEGORY(lcEffect, "plasmazones.effect", QtInfoMsg)

// Autotile module categories
Q_LOGGING_CATEGORY(lcAutotile, "plasmazones.autotile", QtInfoMsg)

// Configuration module categories
Q_LOGGING_CATEGORY(lcConfig, "plasmazones.config", QtInfoMsg)

// KCM module categories
Q_LOGGING_CATEGORY(lcKcm, "plasmazones.kcm", QtInfoMsg)

} // namespace PlasmaZones
