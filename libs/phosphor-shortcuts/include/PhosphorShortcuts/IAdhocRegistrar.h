// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QKeySequence>
#include <QString>

#include <functional>

namespace Phosphor::Shortcuts::Integration {

/**
 * Reduced-surface registrar interface for subsystems that need to bind a
 * transient shortcut while a specific UI state is active (e.g. an Escape
 * cancel grab held only during a window drag) without taking a hard
 * dependency on the concrete shortcut-manager type that owns the Registry.
 *
 * Lives in the `Integration` sub-namespace rather than directly under
 * `Phosphor::Shortcuts` to make the split explicit: the library (Registry,
 * IBackend, the concrete backends) is the shortcut machinery itself;
 * `Integration` is the set of contracts consumers *implement* to plug
 * their own glue into that machinery. The library provides no
 * implementation of IAdhocRegistrar — each consumer wires its own
 * "shortcut manager" object to the underlying Registry, with whatever
 * bind-flow + flush ordering its app needs (the PlasmaZones
 * implementation, for example, queues adhoc registrations that arrive
 * during the initial settings-driven batch and drains them once the
 * Portal BindShortcuts Response lands).
 *
 * Pure abstract C++; no QObject inheritance, no Qt signals — keeps the
 * interface usable from non-QObject consumers.
 */
class IAdhocRegistrar
{
public:
    virtual ~IAdhocRegistrar() = default;

    /**
     * Register an ad-hoc shortcut that lives outside any settings-driven
     * binding table. Implementations forward this to a Registry::bind()
     * with persistent=false (so the binding is excluded from KCM-style
     * enumerations) followed by an immediate flush().
     *
     * Idempotent — a second call with the same id should replace the
     * callback and description in place.
     */
    virtual void registerAdhocShortcut(const QString& id, const QKeySequence& sequence, const QString& description,
                                       std::function<void()> callback) = 0;

    /**
     * Release a shortcut previously bound via registerAdhocShortcut().
     * Idempotent — unknown ids are silently ignored.
     *
     * Subject to the per-backend release semantics documented on
     * IBackend::unregisterShortcut (Portal cannot release a single id;
     * KGlobalAccel and DBusTrigger release cleanly).
     */
    virtual void unregisterAdhocShortcut(const QString& id) = 0;
};

} // namespace Phosphor::Shortcuts::Integration
