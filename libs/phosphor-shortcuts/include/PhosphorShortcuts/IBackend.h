// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QKeySequence>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

#include "phosphorshortcuts_export.h"

namespace PhosphorShortcuts {

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
     *                    convention — Phosphor uses plain snake_case ids
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
     * @param persistent  When false, the binding is transient — the backend
     *                    must avoid leaving an entry in any user-visible
     *                    persistent registry (e.g. KGlobalAccel's
     *                    kglobalshortcutsrc) that would survive an
     *                    unexpected daemon exit. The backend is responsible
     *                    for purging the entry on destruction so a crash
     *                    cannot leak a global key grab into the user's
     *                    System Settings (discussion #461 item 14).
     *                    Persistent (true) is the historical default and
     *                    the only correct value for user-customizable
     *                    shortcuts.
     *
     * Registration is queued until flush() is called.
     */
    virtual void registerShortcut(const QString& id, const QKeySequence& defaultSeq, const QKeySequence& currentSeq,
                                  const QString& description, bool persistent = true) = 0;

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
     * callback.
     */
    virtual void unregisterShortcut(const QString& id) = 0;

    /**
     * Commit any queued register/update ops. Emits ready() once the
     * underlying backend has acknowledged the batch (may be synchronous or
     * asynchronous depending on backend). unregisterShortcut() is NOT
     * queued — see its doc above.
     */
    virtual void flush() = 0;

    /**
     * The key sequence(s) the backend believes are EFFECTIVELY bound for an
     * id right now — i.e. what the user actually has to press, including any
     * override applied outside this process (System Settings rebind on
     * KGlobalAccel, compositor-assigned trigger on Portal).
     *
     * Returned as display strings rather than QKeySequence because the XDG
     * Portal only reports a human-readable `trigger_description`; backends
     * that do have structured sequences (KGlobalAccel) return
     * QKeySequence::toString(QKeySequence::PortableText) per sequence. The
     * strings are DISPLAY-ONLY and not format-stable across backends —
     * Portal relays the compositor's localized description verbatim while
     * KGlobalAccel yields PortableText — so callers must not string-compare
     * results across backends or parse them back into sequences.
     *
     * Tri-state on purpose:
     *  - std::nullopt   → this backend cannot report for the id; callers
     *    fall back to their own stored current sequence.
     *  - engaged, empty → the backend reports the id as unbound as far as
     *    it can tell (e.g. the user cleared the key in System Settings).
     *    Callers must NOT fall back — the stored sequence is stale. This is
     *    a best-effort report, not a hard guarantee that the user cleared
     *    the key: a momentarily unavailable binding service (kglobalacceld
     *    down, grab not yet landed) can also read back empty, in which case
     *    the display shows unbound until the next triggersChanged report.
     *  - engaged, non-empty → the effective trigger strings.
     * Folding the first two into one empty list made a cleared binding
     * display as its stale stored value.
     *
     * The default implementation reports nullopt (correct for the D-Bus
     * trigger fallback, which has no key grabs at all).
     */
    virtual std::optional<QStringList> currentTriggers(const QString& id) const
    {
        Q_UNUSED(id);
        return std::nullopt;
    }

    /**
     * Rebind ANOTHER component's global shortcut (kglobalaccel's
     * setForeignShortcutKeys, the API the Shortcuts KCM uses). The dynamic-
     * workspaces feature neutralizes KWin's stock desktop-switch chords with
     * this while enabled, restoring them on disable. The list carries the
     * action's FULL binding — primary plus alternates — so a restore puts
     * back exactly what foreignShortcuts() reported; an empty list clears
     * the binding. Only the KGlobalAccel backend can do this; the default
     * refuses (false) so callers fall back to snap-back-with-hint.
     */
    virtual bool setForeignShortcuts(const QString& componentName, const QString& actionName,
                                     const QList<QKeySequence>& sequences)
    {
        Q_UNUSED(componentName);
        Q_UNUSED(actionName);
        Q_UNUSED(sequences);
        return false;
    }

    /**
     * The current bindings of another component's action (backup before a
     * foreign rebind) — primary first, alternates after.
     *
     * Tri-state, for the same reason currentTriggers() above is:
     *  - std::nullopt   → the query FAILED or this backend cannot answer (no
     *    foreign-rebind support, binding service unreachable, malformed
     *    reply). The caller must NOT proceed to clear the action: it has no
     *    backup, so a later restore would write the "unbound" sentinel over a
     *    binding the user still has.
     *  - engaged, empty → the action is genuinely unbound; there is nothing to
     *    back up and nothing to steal.
     *  - engaged, non-empty → the action's full binding.
     * Folding the first two into one empty list made an unreachable
     * kglobalacceld look exactly like an already-unbound action.
     *
     * The default reports nullopt: a backend without setForeignShortcuts
     * cannot answer this either.
     */
    virtual std::optional<QList<QKeySequence>> foreignShortcuts(const QString& componentName,
                                                                const QString& actionName) const
    {
        Q_UNUSED(componentName);
        Q_UNUSED(actionName);
        return std::nullopt;
    }

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

    /**
     * Emitted when the effective binding for an id changes outside the
     * register/update flow — e.g. the user rebinds it in System Settings
     * (KGlobalAccel) or the compositor confirms/assigns a trigger (Portal
     * BindShortcuts Response). Consumers displaying bindings re-query
     * currentTriggers() on this signal. Backends that cannot observe
     * external changes simply never emit it.
     */
    void triggersChanged(QString id);
};

} // namespace PhosphorShortcuts
