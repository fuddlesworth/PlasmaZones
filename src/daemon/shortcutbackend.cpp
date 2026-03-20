// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shortcutbackend.h"
#include "../core/logging.h"

#include <QAction>
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QGuiApplication>

#ifdef USE_KDE_FRAMEWORKS
#include <KGlobalAccel>
#endif

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// KGlobalAccel Backend (KDE Plasma)
//
// Uses the KGlobalAccel C++ library for Wayland key grab protocol,
// component registration, and signal routing.  Only available when
// built with USE_KDE_FRAMEWORKS=ON.
// ═══════════════════════════════════════════════════════════════════════════════

#ifdef USE_KDE_FRAMEWORKS
class KGlobalAccelBackend : public IShortcutBackend
{
    Q_OBJECT
public:
    explicit KGlobalAccelBackend(QObject* parent)
        : IShortcutBackend(parent)
    {
    }

    void setDefaultShortcut(QAction* action, const QKeySequence& defaultShortcut) override
    {
        if (!action)
            return;
        KGlobalAccel::self()->setDefaultShortcut(action, {defaultShortcut});
    }

    void setShortcut(QAction* action, const QKeySequence& shortcut) override
    {
        if (!action)
            return;
        KGlobalAccel::self()->setShortcut(action, {shortcut});
    }

    void setGlobalShortcut(QAction* action, const QKeySequence& shortcut) override
    {
        if (!action)
            return;
        KGlobalAccel::setGlobalShortcut(action, shortcut);
    }

    void removeAllShortcuts(QAction* action) override
    {
        if (!action)
            return;
        KGlobalAccel::self()->removeAllShortcuts(action);
    }

    void flush() override
    {
        Q_EMIT shortcutsReady();
    }
};
#endif // USE_KDE_FRAMEWORKS

// ═══════════════════════════════════════════════════════════════════════════════
// XDG Desktop Portal GlobalShortcuts Backend
//
// Uses org.freedesktop.portal.GlobalShortcuts (Hyprland, GNOME 48+, KDE).
// The compositor assigns the actual key — preferredTrigger is a hint.
//
// Portal uses Request/Response pattern for session creation.
// ═══════════════════════════════════════════════════════════════════════════════

class PortalShortcutBackend : public IShortcutBackend
{
    Q_OBJECT
public:
    explicit PortalShortcutBackend(QObject* parent)
        : IShortcutBackend(parent)
    {
        createSession();
    }

    ~PortalShortcutBackend() override
    {
        if (!m_sessionHandle.isEmpty()) {
            QDBusMessage msg = QDBusMessage::createMethodCall(s_portalService, m_sessionHandle,
                                                              QStringLiteral("org.freedesktop.portal.Session"),
                                                              QStringLiteral("Close"));
            QDBusConnection::sessionBus().asyncCall(msg);
        }
    }

    void setDefaultShortcut(QAction* action, const QKeySequence& /*defaultShortcut*/) override
    {
        if (action)
            m_actions.insert(action->objectName(), action);
    }

    void setShortcut(QAction* action, const QKeySequence& shortcut) override
    {
        setGlobalShortcut(action, shortcut);
    }

    void setGlobalShortcut(QAction* action, const QKeySequence& shortcut) override
    {
        if (!action)
            return;
        m_actions.insert(action->objectName(), action);

        // Queue for batch bind after flush (even if session isn't ready yet)
        m_pendingBinds.insert(action->objectName(), shortcut);
    }

    void removeAllShortcuts(QAction* action) override
    {
        if (action) {
            m_actions.remove(action->objectName());
            m_pendingBinds.remove(action->objectName());
        }
    }

    void flush() override
    {
        if (m_sessionHandle.isEmpty() || m_pendingBinds.isEmpty()) {
            Q_EMIT shortcutsReady();
            return;
        }

        sendBindShortcuts();
    }

private:
    static inline const QString s_portalService = QStringLiteral("org.freedesktop.portal.Desktop");
    static inline const QString s_portalPath = QStringLiteral("/org/freedesktop/portal/desktop");

    void sendBindShortcuts()
    {
        // Build the shortcuts array for BindShortcuts
        // Portal expects: BindShortcuts(o session, a(sa{sv}) shortcuts, s parent_window, a{sv} options)
        // We must use QDBusArgument to produce the correct a(sa{sv}) D-Bus struct array.
        QDBusArgument shortcutsArg;
        shortcutsArg.beginArray(qMetaTypeId<QDBusArgument>());
        for (auto it = m_pendingBinds.constBegin(); it != m_pendingBinds.constEnd(); ++it) {
            shortcutsArg.beginStructure();
            shortcutsArg << it.key(); // shortcut id (string)

            // Options dict: a{sv}
            QVariantMap options;
            QAction* action = m_actions.value(it.key());
            if (action) {
                options[QStringLiteral("description")] = action->text();
            }
            if (!it.value().isEmpty()) {
                options[QStringLiteral("preferred_trigger")] = it.value().toString();
            }
            shortcutsArg << options;
            shortcutsArg.endStructure();
        }
        shortcutsArg.endArray();

        QDBusMessage msg = QDBusMessage::createMethodCall(s_portalService, s_portalPath,
                                                          QStringLiteral("org.freedesktop.portal.GlobalShortcuts"),
                                                          QStringLiteral("BindShortcuts"));
        msg.setArguments({
            QVariant::fromValue(QDBusObjectPath(m_sessionHandle)),
            QVariant::fromValue(shortcutsArg),
            QVariant::fromValue(QStringLiteral("")), // parent_window
            QVariant::fromValue(QVariantMap{}), // options
        });

        QDBusConnection::sessionBus().asyncCall(msg);
        m_pendingBinds.clear();
        Q_EMIT shortcutsReady();
    }

    void createSession()
    {
        // Use the portal's CreateSession asynchronously to avoid blocking the event loop.
        QVariantMap options;
        options[QStringLiteral("session_handle_token")] = QStringLiteral("plasmazones");
        options[QStringLiteral("handle_token")] = QStringLiteral("plasmazones_session");

        QDBusMessage msg = QDBusMessage::createMethodCall(s_portalService, s_portalPath,
                                                          QStringLiteral("org.freedesktop.portal.GlobalShortcuts"),
                                                          QStringLiteral("CreateSession"));
        msg.setArguments({QVariant::fromValue(options)});

        QDBusPendingCall pending = QDBusConnection::sessionBus().asyncCall(msg);
        auto* watcher = new QDBusPendingCallWatcher(pending, this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, &PortalShortcutBackend::onSessionCreated);
    }

private Q_SLOTS:
    void onSessionCreated(QDBusPendingCallWatcher* watcher)
    {
        watcher->deleteLater();
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        if (reply.isError()) {
            qCWarning(lcShortcuts) << "Portal CreateSession failed:" << reply.error().message();
            return;
        }

        // Construct session handle from sender + token (portal convention)
        QString sender = QDBusConnection::sessionBus().baseService();
        sender.replace(QLatin1Char('.'), QLatin1Char('_'));
        if (sender.startsWith(QLatin1Char(':')))
            sender = sender.mid(1);
        m_sessionHandle = QStringLiteral("/org/freedesktop/portal/desktop/session/%1/plasmazones").arg(sender);

        // Listen for Activated signal on the session
        QDBusConnection::sessionBus().connect(
            s_portalService, m_sessionHandle, QStringLiteral("org.freedesktop.portal.GlobalShortcuts"),
            QStringLiteral("Activated"), this, SLOT(onActivated(QDBusObjectPath, QString, QVariantMap)));

        qCInfo(lcShortcuts) << "Portal GlobalShortcuts session:" << m_sessionHandle;

        // If shortcuts were queued before the session was ready, bind them now
        if (!m_pendingBinds.isEmpty()) {
            sendBindShortcuts();
        }
    }

    void onActivated(const QDBusObjectPath& /*sessionHandle*/, const QString& shortcutId,
                     const QVariantMap& /*options*/)
    {
        QAction* action = m_actions.value(shortcutId);
        if (action) {
            action->trigger();
        }
    }

private:
    QString m_sessionHandle;
    QHash<QString, QAction*> m_actions;
    QHash<QString, QKeySequence> m_pendingBinds;
};

// ═══════════════════════════════════════════════════════════════════════════════
// D-Bus Trigger Fallback Backend
//
// For compositors without portal support (Sway, COSMIC).
// Exposes TriggerAction(actionId) on D-Bus at /org/plasmazones/Shortcuts.
// Users bind compositor keybindings to:
//   dbus-send --session --dest=org.plasmazones.daemon
//     /org/plasmazones/Shortcuts org.plasmazones.daemon.TriggerAction
//     string:"toggle-autotile"
// ═══════════════════════════════════════════════════════════════════════════════

class DBusTriggerBackend : public IShortcutBackend
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.daemon")
public:
    explicit DBusTriggerBackend(QObject* parent)
        : IShortcutBackend(parent)
    {
        QDBusConnection::sessionBus().registerObject(QStringLiteral("/org/plasmazones/Shortcuts"), this,
                                                     QDBusConnection::ExportScriptableSlots);
        qCInfo(lcShortcuts) << "D-Bus trigger backend active — bind shortcuts via:"
                            << "dbus-send --session --dest=org.plasmazones.daemon"
                            << "/org/plasmazones/Shortcuts org.plasmazones.daemon.TriggerAction"
                            << "string:\"<action-id>\"";
    }

    void setDefaultShortcut(QAction* action, const QKeySequence& /*defaultShortcut*/) override
    {
        if (action)
            m_actions.insert(action->objectName(), action);
    }

    void setShortcut(QAction* action, const QKeySequence& /*shortcut*/) override
    {
        if (action)
            m_actions.insert(action->objectName(), action);
    }

    void setGlobalShortcut(QAction* action, const QKeySequence& /*shortcut*/) override
    {
        if (action)
            m_actions.insert(action->objectName(), action);
    }

    void removeAllShortcuts(QAction* action) override
    {
        if (action)
            m_actions.remove(action->objectName());
    }

    void flush() override
    {
        Q_EMIT shortcutsReady();
    }

    Q_SCRIPTABLE void TriggerAction(const QString& actionId)
    {
        QAction* action = m_actions.value(actionId);
        if (action) {
            qCDebug(lcShortcuts) << "D-Bus trigger:" << actionId;
            action->trigger();
        } else {
            qCWarning(lcShortcuts) << "D-Bus trigger: unknown action" << actionId;
        }
    }

private:
    QHash<QString, QAction*> m_actions;
};

// ═══════════════════════════════════════════════════════════════════════════════
// Factory
// ═══════════════════════════════════════════════════════════════════════════════

std::unique_ptr<IShortcutBackend> createShortcutBackend(QObject* parent)
{
    auto* bus = QDBusConnection::sessionBus().interface();
    if (!bus) {
        qCWarning(lcShortcuts) << "No D-Bus session bus — using trigger fallback";
        return std::make_unique<DBusTriggerBackend>(parent);
    }

#ifdef USE_KDE_FRAMEWORKS
    // Lightweight service name check (no introspection)
    if (bus->isServiceRegistered(QStringLiteral("org.kde.kglobalaccel"))) {
        qCInfo(lcShortcuts) << "Using KGlobalAccel shortcut backend";
        return std::make_unique<KGlobalAccelBackend>(parent);
    }
#endif

    if (bus->isServiceRegistered(QStringLiteral("org.freedesktop.portal.Desktop"))) {
        // Verify the portal actually supports GlobalShortcuts
        QDBusInterface portalCheck(
            QStringLiteral("org.freedesktop.portal.Desktop"), QStringLiteral("/org/freedesktop/portal/desktop"),
            QStringLiteral("org.freedesktop.portal.GlobalShortcuts"), QDBusConnection::sessionBus());
        if (portalCheck.isValid()) {
            qCInfo(lcShortcuts) << "Using Portal GlobalShortcuts backend";
            return std::make_unique<PortalShortcutBackend>(parent);
        }
    }

    qCInfo(lcShortcuts) << "Using D-Bus trigger fallback backend";
    return std::make_unique<DBusTriggerBackend>(parent);
}

} // namespace PlasmaZones

#include "shortcutbackend.moc"
