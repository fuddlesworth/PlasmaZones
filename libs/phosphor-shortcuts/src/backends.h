// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// Internal header: concrete backend declarations. Not installed. Only the
// factory translation unit and the backend .cpp files include this.

#include "PhosphorShortcuts/IBackend.h"

#include <QHash>
#include <QKeySequence>
#include <QString>

class QDBusPendingCallWatcher;
class QDBusObjectPath;

namespace Phosphor::Shortcuts {

// ─── D-Bus trigger fallback ──────────────────────────────────────────────────

class DBusTriggerBackend : public IBackend
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.Phosphor.Shortcuts")
public:
    explicit DBusTriggerBackend(QObject* parent = nullptr);

    void registerShortcut(const QString& id, const QKeySequence& preferredTrigger, const QString& description) override;
    void updateShortcut(const QString& id, const QKeySequence& newTrigger) override;
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

    void registerShortcut(const QString& id, const QKeySequence& preferredTrigger, const QString& description) override;
    void updateShortcut(const QString& id, const QKeySequence& newTrigger) override;
    void unregisterShortcut(const QString& id) override;
    void flush() override;

private Q_SLOTS:
    void onSessionCreated(QDBusPendingCallWatcher* watcher);
    void onActivated(const QDBusObjectPath& sessionHandle, const QString& shortcutId, const QVariantMap& options);

private:
    struct Pending
    {
        QKeySequence preferred;
        QString description;
    };

    void createSession();
    void sendBindShortcuts();

    QString m_sessionToken; // used for session_handle_token — derived from applicationName()
    QString m_sessionHandle; // populated once CreateSession returns
    bool m_flushRequested = false;

    // Pending set = shortcuts that still need BindShortcuts sent to the portal.
    // On each flush() we send everything in pending and clear; subsequent
    // register/update calls re-populate it.
    QHash<QString, Pending> m_pending;

    // Full table of known ids + descriptions, kept for bookkeeping + logs.
    QHash<QString, QString> m_descriptions;
};

#ifdef PHOSPHORSHORTCUTS_HAVE_KGLOBALACCEL

// ─── KGlobalAccel (KDE Plasma) ───────────────────────────────────────────────

class KGlobalAccelBackend : public IBackend
{
    Q_OBJECT
public:
    explicit KGlobalAccelBackend(QObject* parent = nullptr);
    ~KGlobalAccelBackend() override;

    void registerShortcut(const QString& id, const QKeySequence& preferredTrigger, const QString& description) override;
    void updateShortcut(const QString& id, const QKeySequence& newTrigger) override;
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
