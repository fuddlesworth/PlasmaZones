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
     */
    void bind(const QString& id, const QKeySequence& defaultSeq, const QString& description = {},
              std::function<void()> callback = {});

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
     */
    void flush();

    QKeySequence shortcut(const QString& id) const;
    QVector<Binding> bindings() const;

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
        bool dirty = true; // needs flush to backend
        bool registered = false; // has registerShortcut been sent yet?
    };

    QPointer<IBackend> m_backend;
    QHash<QString, Entry> m_entries;
};

} // namespace Phosphor::Shortcuts
