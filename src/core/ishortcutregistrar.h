// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QKeySequence>
#include <QString>

#include <functional>

namespace PlasmaZones {

/**
 * Minimal interface used by subsystems that need to bind a transient
 * shortcut while a specific UI state is active (e.g. the Escape
 * cancel-overlay shortcut during a window drag) without taking a hard
 * dependency on the concrete ShortcutManager implementation.
 *
 * Concrete implementation lives in daemon/shortcutmanager.cpp; the daemon
 * wires adaptors (in plasmazones_core) to the shortcut manager via this
 * interface so core doesn't have to link the daemon target.
 */
class IShortcutRegistrar
{
public:
    virtual ~IShortcutRegistrar() = default;

    virtual void registerAdhocShortcut(const QString& id, const QKeySequence& sequence, const QString& description,
                                       std::function<void()> callback) = 0;

    virtual void unregisterAdhocShortcut(const QString& id) = 0;
};

} // namespace PlasmaZones
