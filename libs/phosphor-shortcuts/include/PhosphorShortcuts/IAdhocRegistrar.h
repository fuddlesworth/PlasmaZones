// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QKeySequence>
#include <QString>

#include <functional>

namespace Phosphor::Shortcuts {

/**
 * Reduced-surface registrar interface for subsystems that need to bind a
 * transient shortcut while a specific UI state is active (e.g. an Escape
 * cancel grab held only during a window drag) without taking a hard
 * dependency on the concrete shortcut-manager type that owns the Registry.
 *
 * Lives in PhosphorShortcuts so a future standalone Phosphor WM (or any
 * other consumer of the library) can offer the same contract without
 * defining its own copy in app code. The library does not provide an
 * implementation — implementing this is the consumer-side glue between
 * the consumer's "shortcut manager" object and the underlying Registry,
 * and is responsible for whatever bind-flow + flush ordering its app
 * needs (the PlasmaZones implementation, for example, refuses adhoc
 * registration during the initial settings-driven batch to avoid racing
 * the Portal BindShortcuts batch).
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

} // namespace Phosphor::Shortcuts
