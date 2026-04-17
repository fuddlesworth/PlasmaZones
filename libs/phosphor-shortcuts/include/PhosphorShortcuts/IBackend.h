// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QKeySequence>
#include <QObject>
#include <QString>

#include "phosphorshortcuts_export.h"

namespace Phosphor::Shortcuts {

/**
 * Pluggable global shortcut backend.
 *
 * Implementations bridge to a specific binding mechanism: KGlobalAccel
 * (KDE), XDG Desktop Portal GlobalShortcuts, D-Bus trigger fallback, or a
 * future compositor-native grabber.
 *
 * Shortcuts are addressed by stable string id. The library does not expose
 * QAction in this interface on purpose — QAction is kept as an implementation
 * detail of the KGlobalAccel backend, since that API requires it.
 *
 * Concurrency: all methods and signals run on the thread that owns the
 * backend (typically the GUI thread). Not thread-safe.
 */
class PHOSPHORSHORTCUTS_EXPORT IBackend : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~IBackend() override = default;

    /**
     * Register a new shortcut id with a preferred trigger.
     *
     * @param id                Stable string id (e.g. "pz.move-window-left").
     * @param preferredTrigger  Compiled-in default key sequence. Portal
     *                          backends treat this as advisory — the
     *                          compositor assigns the actual key. DBusTrigger
     *                          ignores it entirely.
     * @param description       Human-readable label surfaced in portal
     *                          settings UIs and kglobalaccel listings.
     *
     * Registration is queued until flush() is called.
     */
    virtual void registerShortcut(const QString& id, const QKeySequence& preferredTrigger,
                                  const QString& description) = 0;

    /**
     * Change the active binding for an already-registered id. Takes effect
     * after the next flush().
     */
    virtual void updateShortcut(const QString& id, const QKeySequence& newTrigger) = 0;

    /**
     * Release the key grab for an id. Idempotent; unknown ids are ignored.
     */
    virtual void unregisterShortcut(const QString& id) = 0;

    /**
     * Commit any queued register/update/unregister ops. Emits ready() once
     * the underlying backend has acknowledged the batch (may be synchronous
     * or asynchronous depending on backend).
     */
    virtual void flush() = 0;

Q_SIGNALS:
    /**
     * Emitted when the backend observes the user triggering a registered
     * shortcut. The id matches what was passed to registerShortcut().
     */
    void activated(QString id);

    /**
     * Emitted after flush() completes. Consumers that need to know when
     * initial registration is live (e.g. to un-grey a UI element) connect
     * here.
     */
    void ready();
};

} // namespace Phosphor::Shortcuts
