// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QHash>
#include <QKeySequence>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <functional>

#include "phosphorshortcuts_export.h"

namespace Phosphor::Shortcuts {

class IBackend;

/**
 * Consumer-facing facade over IBackend.
 *
 * Owns a table of `(id, default sequence, current sequence, description,
 * optional callback)` rows. Forwards registration + rebind calls to the
 * backend and fans activation signals out as either per-id callbacks or the
 * triggered() signal — consumers may use either pattern interchangeably.
 *
 * The Registry does not own the backend; the caller keeps the backend alive
 * for the Registry's lifetime. Passing a null backend is a programming error.
 */
class PHOSPHORSHORTCUTS_EXPORT Registry : public QObject
{
    Q_OBJECT
public:
    struct Binding
    {
        QString id;
        QKeySequence defaultSeq;
        QKeySequence currentSeq;
        QString description;
    };

    explicit Registry(IBackend* backend, QObject* parent = nullptr);
    ~Registry() override;

    /**
     * Register a shortcut. Safe to call multiple times for the same id —
     * subsequent calls update the default sequence, description, and callback
     * in place but PRESERVE the current sequence (any user-applied rebind is
     * kept). Takes effect after flush().
     *
     * @param callback Optional. Invoked on activation in addition to the
     *                 triggered() signal. Nullptr callbacks are stored but
     *                 never invoked; consumers relying purely on the signal
     *                 can omit the argument.
     * @param persistent If false, the binding is considered transient (e.g. a
     *                   grab bound around a specific UI state) and is excluded
     *                   from bindings(true) enumeration. Does not affect
     *                   backend behaviour. Defaults to true.
     *
     * Note: descriptions are captured at first-flush time and NOT forwarded on
     * subsequent bind() calls — IBackend::updateShortcut doesn't carry a
     * description argument. Description changes at runtime are local-only.
     */
    void bind(const QString& id, const QKeySequence& defaultSeq, const QString& description = {},
              std::function<void()> callback = {}, bool persistent = true);

    /**
     * Change the active binding for an already-registered id. Takes effect
     * after flush(). Unknown ids are logged and ignored. Passing an empty
     * QKeySequence routes through unbind() — releasing the grab cleanly
     * rather than leaving an empty sequence registered.
     */
    void rebind(const QString& id, const QKeySequence& seq);

    /**
     * Drop a binding entirely. Releases any key grab and forgets the
     * callback. Idempotent. Applied immediately — NOT batched until flush()
     * — because the backends' unregister paths all act synchronously or
     * with trivial state, and a late flush would be surprising for a
     * "release this grab now" API.
     */
    void unbind(const QString& id);

    /**
     * Forward queued bind/rebind ops to the backend. Does NOT include
     * unbind() — those are applied immediately at the call site. Matches
     * the backend's queue-then-flush model for register / update.
     *
     * ready() fires after EVERY flush, including no-op flushes where no
     * entry actually changed. This matches the backend contract (each
     * IBackend::flush() emits ready) and lets consumers gate UI on "any
     * flush has settled" without per-entry bookkeeping. If you need
     * "only fire when something actually changed", track that on the
     * caller side.
     */
    void flush();

    QKeySequence shortcut(const QString& id) const;

    /**
     * Enumerate registered bindings, sorted by id for deterministic output.
     *
     * @param persistentOnly If true, transient bindings (those registered with
     *                       persistent=false) are excluded. Intended for
     *                       settings UIs that should not expose internal
     *                       ad-hoc grabs to the user. Defaults to false so
     *                       tests and library-internal callers see everything.
     */
    QVector<Binding> bindings(bool persistentOnly = false) const;

Q_SIGNALS:
    /**
     * Emitted on every activation, regardless of whether the binding has a
     * callback. Use this for centralised dispatch (one slot, switch on id).
     *
     * Signature matches IBackend::activated (QString by value) so the two
     * signals can be cross-connected without adapter slots.
     */
    void triggered(QString id);

    /**
     * Forwarded from IBackend::ready().
     */
    void ready();

private Q_SLOTS:
    void onBackendActivated(QString id);
    void onBackendReady();

private:
    struct Entry
    {
        Binding binding;
        std::function<void()> callback;
        // Last values successfully pushed to the backend. Used to decide
        // whether flush() should re-register (default changed) or update-only
        // (current changed), and to short-circuit no-op flushes.
        QKeySequence lastSentDefault;
        QKeySequence lastSentCurrent;
        bool registered = false; // has registerShortcut been sent yet?
        bool persistent = true; // surfaces in bindings(persistentOnly=true)
    };

    QPointer<IBackend> m_backend;
    QHash<QString, Entry> m_entries;
};

} // namespace Phosphor::Shortcuts
