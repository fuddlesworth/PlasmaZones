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
    explicit IBackend(QObject* parent = nullptr)
        : QObject(parent)
    {
    }
    ~IBackend() override = default;

    /**
     * Register a new shortcut id.
     *
     * @param id          Stable string id. The library imposes no prefix
     *                    convention — PlasmaZones uses plain snake_case ids
     *                    like "move_window_left" because KGlobalAccel and
     *                    XDG Portal persist the id verbatim; renaming is an
     *                    on-disk rename users pay for. Pick a scheme that
     *                    won't need to churn.
     * @param defaultSeq  Compiled-in default key sequence — the "factory"
     *                    value a user can reset to. KGlobalAccel records
     *                    this via setDefaultShortcut so System Settings'
     *                    "Reset to default" resets to the correct value.
     *                    Portal backends use this as `preferred_trigger`
     *                    (advisory — the compositor assigns the actual key).
     *                    DBusTrigger ignores it entirely.
     * @param currentSeq  The key sequence to actually grab now. Usually the
     *                    user's customised value read from config; equals
     *                    defaultSeq on a fresh install. May be empty (no grab).
     * @param description Human-readable label surfaced in portal settings
     *                    UIs and kglobalaccel listings.
     *
     * Registration is queued until flush() is called.
     */
    virtual void registerShortcut(const QString& id, const QKeySequence& defaultSeq, const QKeySequence& currentSeq,
                                  const QString& description) = 0;

    /**
     * Change the active binding for an already-registered id. Takes both
     * sequences — defaultSeq stays for backends that need to keep the
     * "factory default" target current (PortalBackend's preferred_trigger,
     * which is keyed off defaultSeq for consistency with registerShortcut),
     * currentSeq is the new value to grab.
     *
     * KGlobalAccel backend ignores defaultSeq here because the default
     * target is independently refreshed via registerShortcut whenever the
     * compiled-in default changes (Registry re-invokes registerShortcut for
     * defaultSeq changes; updateShortcut only fires on currentSeq-only
     * deltas).
     *
     * Does NOT carry a description — description updates require a fresh
     * registerShortcut call. Takes effect after the next flush().
     */
    virtual void updateShortcut(const QString& id, const QKeySequence& defaultSeq, const QKeySequence& newTrigger) = 0;

    /**
     * Release the key grab for an id. Idempotent; unknown ids are ignored.
     * This call is NOT queued — backends apply it immediately (subject to
     * per-backend semantics; see PortalBackend notes in the .cpp).
     *
     * PortalBackend caveat: XDG GlobalShortcuts has no per-id release, so
     * this is a LOCAL-ONLY clear on that backend (onActivated will drop
     * the event, but the key stays grabbed compositor-side until the
     * session closes). Consumers needing truly transient grabs on Portal
     * compositors should bind once and gate via a flag inside the
     * callback. See docs/phosphor-shortcuts-api.md for details.
     */
    virtual void unregisterShortcut(const QString& id) = 0;

    /**
     * Commit any queued register/update ops. Emits ready() once the
     * underlying backend has acknowledged the batch (may be synchronous or
     * asynchronous depending on backend). unregisterShortcut() is NOT
     * queued — see its doc above.
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
