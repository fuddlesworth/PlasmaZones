// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal header: concrete backend declarations. Not installed. Only the
// factory translation unit and the backend .cpp files include this.

#include "PhosphorShortcuts/IBackend.h"

#include <QHash>
#include <QKeySequence>
#include <QSet>
#include <QString>

class QDBusPendingCallWatcher;
class QDBusObjectPath;
class QDBusMessage;

namespace Phosphor::Shortcuts {

// ─── D-Bus trigger fallback ──────────────────────────────────────────────────

class DBusTriggerBackend : public IBackend
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.Phosphor.Shortcuts")
public:
    explicit DBusTriggerBackend(QObject* parent = nullptr);

    void registerShortcut(const QString& id, const QKeySequence& defaultSeq, const QKeySequence& currentSeq,
                          const QString& description) override;
    void updateShortcut(const QString& id, const QKeySequence& defaultSeq, const QKeySequence& newTrigger) override;
    void unregisterShortcut(const QString& id) override;
    void flush() override;

public Q_SLOTS:
    Q_SCRIPTABLE void TriggerAction(const QString& id);

private:
    QHash<QString, QString> m_descriptions;
};

// ─── XDG Desktop Portal GlobalShortcuts ──────────────────────────────────────

class PortalBackend : public IBackend
{
    Q_OBJECT
public:
    explicit PortalBackend(QObject* parent = nullptr);
    ~PortalBackend() override;

    void registerShortcut(const QString& id, const QKeySequence& defaultSeq, const QKeySequence& currentSeq,
                          const QString& description) override;
    void updateShortcut(const QString& id, const QKeySequence& defaultSeq, const QKeySequence& newTrigger) override;
    void unregisterShortcut(const QString& id) override;
    void flush() override;

private Q_SLOTS:
    void onSessionCreated(QDBusPendingCallWatcher* watcher);
    // Unified dispatcher for every org.freedesktop.portal.Request::Response
    // signal from the portal service, regardless of Request path. The trailing
    // QDBusMessage carries the signal path, which tells us whether this
    // Response belongs to our in-flight CreateSession, our in-flight
    // BindShortcuts, or neither (other processes' or already-consumed
    // Responses — dropped). Subscribing with an empty path eliminates the
    // race where the portal fires Response before our QDBusPendingCallWatcher
    // finishes and before we could switch subscriptions to the actual
    // Request path the portal picked.
    void onAnyRequestResponse(uint response, const QVariantMap& results, const QDBusMessage& msg);
    void onActivated(const QDBusObjectPath& sessionHandle, const QString& shortcutId, qulonglong timestamp,
                     const QVariantMap& options);

private:
    struct Pending
    {
        QKeySequence preferred;
        QString description;
    };

    void createSession();
    void sendBindShortcuts();
    // Called from onAnyRequestResponse once the Response has been identified
    // as belonging to the CreateSession / BindShortcuts request by path.
    void handleCreateSessionResponse(uint response, const QVariantMap& results);
    void handleBindShortcutsResponse(uint response, const QVariantMap& results);

    QString m_sessionToken; // used for session_handle_token — derived from applicationName()
    QString m_sessionHandle; // populated once Request::Response delivers session_handle
    // Request paths we're currently expecting Response on. Cleared the moment
    // the matching Response arrives OR the request fails, so later Responses
    // can't re-enter the same handler and the unified slot knows when to drop
    // unknown-path signals.
    QString m_createRequestPath;
    QString m_bindRequestPath;
    bool m_flushRequested = false;
    // Latched true if CreateSession failed. flush() then keeps emitting
    // ready() synchronously (with a warning) so consumers don't hang waiting
    // for a signal that will never arrive, while still being able to tell
    // something is wrong from the logs.
    bool m_sessionFailed = false;

    // Pending set = shortcuts that still need BindShortcuts sent to the portal.
    // On each flush() we send everything in pending and clear; subsequent
    // register/update calls re-populate it.
    QHash<QString, Pending> m_pending;

    // Full table of known ids + descriptions, kept for bookkeeping + logs.
    QHash<QString, QString> m_descriptions;

    // Ids the compositor has confirmed bound (BindShortcuts Response success).
    // Used by unregisterShortcut to distinguish "never reached the compositor"
    // (local-only cleanup, no user-visible effect) from "still grabbed
    // compositor-side" (the spec limitation where the key stays routed to us
    // until session close — see unregisterShortcut for the full rationale).
    QSet<QString> m_confirmedBound;

    // Snapshot of m_pending captured right before the most recent
    // sendBindShortcuts dispatch. Consumed by handleBindShortcutsResponse on
    // a successful Response to promote ids into m_lastSentPreferred; cleared
    // on RPC failure (no promotion). Superseded responses never reach this
    // field because onAnyRequestResponse drops them by path compare.
    QHash<QString, Pending> m_pendingBindResponse;

    // Per-id preferred_trigger that the compositor has successfully
    // acknowledged via a BindShortcuts Response. updateShortcut uses this
    // to short-circuit "rebind with same defaultSeq": Portal's newTrigger
    // argument is ignored (spec limitation), so when defaultSeq hasn't
    // changed since the last confirmed send there is nothing to tell the
    // portal. Without this gate, every user rebind of currentSeq on
    // KGlobalAccel-side would round-trip a useless BindShortcuts RPC on
    // Portal compositors.
    QHash<QString, QKeySequence> m_lastSentPreferred;
};

#ifdef PHOSPHORSHORTCUTS_HAVE_KGLOBALACCEL

// ─── KGlobalAccel (KDE Plasma) ───────────────────────────────────────────────

class KGlobalAccelBackend : public IBackend
{
    Q_OBJECT
public:
    explicit KGlobalAccelBackend(QObject* parent = nullptr);
    ~KGlobalAccelBackend() override;

    void registerShortcut(const QString& id, const QKeySequence& defaultSeq, const QKeySequence& currentSeq,
                          const QString& description) override;
    void updateShortcut(const QString& id, const QKeySequence& defaultSeq, const QKeySequence& newTrigger) override;
    void unregisterShortcut(const QString& id) override;
    void flush() override;

private:
    // QAction ownership is an implementation detail of this backend —
    // KGlobalAccel's C++ API demands QAction* values. They are deleteLater'd
    // on unregisterShortcut / destructor.
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif // PHOSPHORSHORTCUTS_HAVE_KGLOBALACCEL

} // namespace Phosphor::Shortcuts
