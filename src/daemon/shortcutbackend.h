// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Abstract global shortcut backend.
//
// Provides an interface for registering, updating, and unregistering
// global keyboard shortcuts.  Implementations:
//
// - KGlobalAccelBackend: KDE's kglobalaccel service (Plasma)
// - PortalShortcutBackend: XDG Desktop Portal GlobalShortcuts
//   (Hyprland, GNOME 48+, any portal-supporting compositor)
// - DBusTriggerBackend: fallback — exposes a D-Bus method that
//   users bind to compositor-native keybindings (Sway, COSMIC)
//
// ShortcutManager owns the backend and calls these methods.

#pragma once

#include <QKeySequence>
#include <QObject>
#include <QString>
#include <functional>
#include "plasmazones_export.h"

class QAction;

namespace PlasmaZones {

class PLASMAZONES_EXPORT IShortcutBackend : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~IShortcutBackend() override = default;

    /// Register a shortcut with a default key binding.
    /// The backend may defer the actual key grab (async).
    virtual void setDefaultShortcut(QAction* action, const QKeySequence& defaultShortcut) = 0;

    /// Set (update) the active shortcut for an action.
    /// This triggers the actual key grab.
    virtual void setShortcut(QAction* action, const QKeySequence& shortcut) = 0;

    /// Register + grab in one call (convenience).
    virtual void setGlobalShortcut(QAction* action, const QKeySequence& shortcut) = 0;

    /// Unregister all shortcuts for an action, releasing key grabs.
    virtual void removeAllShortcuts(QAction* action) = 0;

    /// Flush any queued async operations.  Emits shortcutsReady() when done.
    virtual void flush() = 0;

Q_SIGNALS:
    /// Emitted when all queued shortcut registrations are complete.
    void shortcutsReady();
};

/// Create the appropriate shortcut backend for the current desktop environment.
/// Detects KDE (kglobalaccel), portal availability, or falls back to D-Bus trigger.
PLASMAZONES_EXPORT std::unique_ptr<IShortcutBackend> createShortcutBackend(QObject* parent = nullptr);

} // namespace PlasmaZones
