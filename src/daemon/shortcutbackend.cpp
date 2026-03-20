// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shortcutbackend.h"
#include "../core/logging.h"

#include <QAction>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QGuiApplication>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// KGlobalAccel Backend (KDE Plasma)
//
// Wraps org.kde.kglobalaccel D-Bus service directly.  One synchronous
// setShortcut call bootstraps the component signal connection, then
// remaining grabs are async.  Dispatches globalShortcutPressed signals
// to the registered QAction::triggered().
// ═══════════════════════════════════════════════════════════════════════════════

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
        m_actions.insert(action->objectName(), action);

        const QStringList actionId = buildActionId(action);
        QList<int> keys;
        for (int i = 0; i < defaultShortcut.count(); ++i)
            keys.append(defaultShortcut[i].toCombined());

        // Register default via D-Bus (no key grab) — uses setShortcut with IsDefault flag
        QDBusMessage msg = QDBusMessage::createMethodCall(s_service, s_path, s_iface, QStringLiteral("setShortcut"));
        msg.setArguments({
            QVariant::fromValue(actionId),
            QVariant::fromValue(keys),
            QVariant::fromValue(static_cast<uint>(IsDefault)),
        });
        QDBusConnection::sessionBus().asyncCall(msg);
    }

    void setShortcut(QAction* action, const QKeySequence& shortcut) override
    {
        if (!action)
            return;
        m_actions.insert(action->objectName(), action);

        const QStringList actionId = buildActionId(action);
        QList<int> keys;
        for (int i = 0; i < shortcut.count(); ++i)
            keys.append(shortcut[i].toCombined());

        QDBusMessage msg = QDBusMessage::createMethodCall(s_service, s_path, s_iface, QStringLiteral("setShortcut"));
        msg.setArguments({
            QVariant::fromValue(actionId),
            QVariant::fromValue(keys),
            QVariant::fromValue(static_cast<uint>(SetPresent)),
        });

        // Synchronous call — establishes the component signal connection
        QDBusConnection::sessionBus().call(msg);
        ensureComponentConnected();
    }

    void setGlobalShortcut(QAction* action, const QKeySequence& shortcut) override
    {
        if (!action)
            return;
        m_actions.insert(action->objectName(), action);

        const QStringList actionId = buildActionId(action);
        QList<int> keys;
        for (int i = 0; i < shortcut.count(); ++i)
            keys.append(shortcut[i].toCombined());

        if (!m_componentConnected) {
            // First shortcut: synchronous to establish component signal
            QDBusMessage msg =
                QDBusMessage::createMethodCall(s_service, s_path, s_iface, QStringLiteral("setShortcut"));
            msg.setArguments({
                QVariant::fromValue(actionId),
                QVariant::fromValue(keys),
                QVariant::fromValue(static_cast<uint>(SetPresent)),
            });
            QDBusConnection::sessionBus().call(msg);
            ensureComponentConnected();
        } else {
            // Async grab
            QDBusMessage msg =
                QDBusMessage::createMethodCall(s_service, s_path, s_iface, QStringLiteral("setShortcut"));
            msg.setArguments({
                QVariant::fromValue(actionId),
                QVariant::fromValue(keys),
                QVariant::fromValue(static_cast<uint>(SetPresent)),
            });
            ++m_pendingCalls;
            auto pending = QDBusConnection::sessionBus().asyncCall(msg);
            auto* watcher = new QDBusPendingCallWatcher(pending, this);
            connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                if (--m_pendingCalls == 0)
                    Q_EMIT shortcutsReady();
            });
        }
    }

    void removeAllShortcuts(QAction* action) override
    {
        if (!action)
            return;
        m_actions.remove(action->objectName());

        // setInactive removes a single action's key grabs from kglobalacceld
        const QStringList actionId = buildActionId(action);
        QDBusMessage msg = QDBusMessage::createMethodCall(s_service, s_path, s_iface, QStringLiteral("setInactive"));
        msg.setArguments({QVariant::fromValue(actionId)});
        QDBusConnection::sessionBus().asyncCall(msg);
    }

    void flush() override
    {
        if (m_pendingCalls == 0) {
            Q_EMIT shortcutsReady();
        }
    }

private Q_SLOTS:
    // Dispatches kglobalacceld's globalShortcutPressed signal to the right QAction.
    // Signal signature: globalShortcutPressed(QString componentUnique, QString shortcutUnique, qlonglong timestamp)
    void onGlobalShortcutPressed(const QString& /*componentUnique*/, const QString& shortcutUnique,
                                 qlonglong /*timestamp*/)
    {
        QAction* action = m_actions.value(shortcutUnique);
        if (action) {
            action->trigger();
        }
    }

private:
    static constexpr uint IsDefault = 4;
    static constexpr uint SetPresent = 2;

    static inline const QString s_service = QStringLiteral("org.kde.kglobalaccel");
    static inline const QString s_path = QStringLiteral("/kglobalaccel");
    static inline const QString s_iface = QStringLiteral("org.kde.KGlobalAccel");

    QStringList buildActionId(QAction* action) const
    {
        return {
            QCoreApplication::applicationName(),
            action->objectName(),
            QGuiApplication::applicationDisplayName(),
            action->text().remove(QLatin1Char('&')),
        };
    }

    void ensureComponentConnected()
    {
        if (m_componentConnected)
            return;

        // Call getComponent() — kglobalacceld uses this to register our process
        // for signal delivery and returns the component object path.
        const QString appName = QCoreApplication::applicationName();
        QDBusMessage getComp =
            QDBusMessage::createMethodCall(s_service, s_path, s_iface, QStringLiteral("getComponent"));
        getComp.setArguments({QVariant::fromValue(appName)});

        QDBusMessage reply = QDBusConnection::sessionBus().call(getComp);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            qCWarning(lcShortcuts) << "getComponent failed:" << reply.errorMessage();
            // Fall back to wildcard path
            QDBusConnection::sessionBus().connect(s_service, {}, QStringLiteral("org.kde.KGlobalAccel.Component"),
                                                  QStringLiteral("globalShortcutPressed"), this,
                                                  SLOT(onGlobalShortcutPressed(QString, QString, qlonglong)));
        } else {
            QString componentPath = reply.arguments().first().value<QDBusObjectPath>().path();
            qCDebug(lcShortcuts) << "Component path:" << componentPath;

            // Connect to globalShortcutPressed on the specific component path
            QDBusConnection::sessionBus().connect(s_service, componentPath,
                                                  QStringLiteral("org.kde.KGlobalAccel.Component"),
                                                  QStringLiteral("globalShortcutPressed"), this,
                                                  SLOT(onGlobalShortcutPressed(QString, QString, qlonglong)));
        }

        m_componentConnected = true;
    }

    bool m_componentConnected = false;
    int m_pendingCalls = 0;
    QHash<QString, QAction*> m_actions; // objectName -> action
};

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
        if (!action || m_sessionHandle.isEmpty())
            return;
        m_actions.insert(action->objectName(), action);

        // Queue for batch bind after flush
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

        // Build the shortcuts array for BindShortcuts
        // Portal expects: BindShortcuts(session, shortcuts a(sa{sv}), parent_window, options)
        QDBusMessage msg = QDBusMessage::createMethodCall(s_portalService, s_portalPath,
                                                          QStringLiteral("org.freedesktop.portal.GlobalShortcuts"),
                                                          QStringLiteral("BindShortcuts"));

        // Build shortcuts as QVariantList of QVariantMaps with "id" and options
        // Note: the actual D-Bus signature requires a(sa{sv}) but Qt's auto-marshaling
        // from QVariant may not produce the exact struct type. For full portal compliance,
        // this would need QDBusArgument. For compositors that accept the variant form
        // (Hyprland, KDE), QVariant works pragmatically.
        QVariantList shortcuts;
        for (auto it = m_pendingBinds.constBegin(); it != m_pendingBinds.constEnd(); ++it) {
            QVariantMap entry;
            entry[QStringLiteral("id")] = it.key();
            QVariantMap options;
            QAction* action = m_actions.value(it.key());
            if (action) {
                options[QStringLiteral("description")] = action->text();
            }
            if (!it.value().isEmpty()) {
                options[QStringLiteral("preferred_trigger")] = it.value().toString();
            }
            entry[QStringLiteral("options")] = options;
            shortcuts.append(entry);
        }

        msg.setArguments({
            QVariant::fromValue(m_sessionHandle),
            QVariant::fromValue(shortcuts),
            QVariant::fromValue(QStringLiteral("")), // parent_window
            QVariant::fromValue(QVariantMap{}), // options
        });

        QDBusConnection::sessionBus().asyncCall(msg);
        m_pendingBinds.clear();
        Q_EMIT shortcutsReady();
    }

private:
    static inline const QString s_portalService = QStringLiteral("org.freedesktop.portal.Desktop");
    static inline const QString s_portalPath = QStringLiteral("/org/freedesktop/portal/desktop");

    void createSession()
    {
        // Use the portal's CreateSession. The response comes via the Response signal
        // on the request object path. For simplicity, we use a synchronous approach:
        // the reply contains the request object path, and we wait for the Response.
        QVariantMap options;
        options[QStringLiteral("session_handle_token")] = QStringLiteral("plasmazones");
        options[QStringLiteral("handle_token")] = QStringLiteral("plasmazones_session");

        QDBusMessage msg = QDBusMessage::createMethodCall(s_portalService, s_portalPath,
                                                          QStringLiteral("org.freedesktop.portal.GlobalShortcuts"),
                                                          QStringLiteral("CreateSession"));
        msg.setArguments({QVariant::fromValue(options)});

        auto reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 5000);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            qCWarning(lcShortcuts) << "Portal CreateSession failed:" << reply.errorMessage();
            return;
        }

        // The reply contains the request object path. Subscribe to its Response.
        QString requestPath = reply.arguments().first().value<QDBusObjectPath>().path();

        // The session handle follows the pattern:
        // /org/freedesktop/portal/desktop/session/<sender>/<token>
        // Wait briefly for the Response signal, or construct the handle from the token.
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
    }

private Q_SLOTS:
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

    // Lightweight service name check (no introspection)
    if (bus->isServiceRegistered(QStringLiteral("org.kde.kglobalaccel"))) {
        qCInfo(lcShortcuts) << "Using KGlobalAccel shortcut backend";
        return std::make_unique<KGlobalAccelBackend>(parent);
    }

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
