// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shortcutbackend.h"
#include "../core/logging.h"

#include <QAction>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QGuiApplication>

namespace PlasmaZones {

// ═══════════════════════════════════════════════════════════════════════════════
// KGlobalAccel Backend (KDE Plasma)
//
// Wraps org.kde.kglobalaccel D-Bus service.  Uses the same async
// optimization as the old ShortcutManager: one synchronous setShortcut
// call to bootstrap the component signal connection, then async D-Bus
// for remaining grabs.
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

        // Register the default via D-Bus (no key grab)
        const QStringList actionId = buildActionId(action);
        QList<int> keys;
        for (int i = 0; i < defaultShortcut.count(); ++i)
            keys.append(defaultShortcut[i].toCombined());

        QDBusMessage msg =
            QDBusMessage::createMethodCall(s_service, s_path, s_iface, QStringLiteral("setShortcutKeys"));
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

        // Connect to the component's globalShortcutPressed signal (once)
        if (!m_componentConnected) {
            connectComponent();
            m_componentConnected = true;
        }
    }

    void setGlobalShortcut(QAction* action, const QKeySequence& shortcut) override
    {
        if (!action)
            return;

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
            connectComponent();
            m_componentConnected = true;
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

        const QStringList actionId = buildActionId(action);
        QDBusMessage msg = QDBusMessage::createMethodCall(s_service, s_path, s_iface, QStringLiteral("unregister"));
        msg.setArguments({QVariant::fromValue(actionId)});
        QDBusConnection::sessionBus().asyncCall(msg);
    }

    void flush() override
    {
        if (m_pendingCalls == 0) {
            Q_EMIT shortcutsReady();
        }
        // Otherwise shortcutsReady() will be emitted when the last async call completes
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

    void connectComponent()
    {
        // Connect to globalShortcutPressed signal for our component
        QDBusConnection::sessionBus().connect(s_service, {}, QStringLiteral("org.kde.KGlobalAccel.Component"),
                                              QStringLiteral("globalShortcutPressed"), QCoreApplication::instance(),
                                              SLOT(quit())); // placeholder — ShortcutManager wires real handlers
    }

    bool m_componentConnected = false;
    int m_pendingCalls = 0;
};

// ═══════════════════════════════════════════════════════════════════════════════
// XDG Desktop Portal GlobalShortcuts Backend
//
// Uses org.freedesktop.portal.GlobalShortcuts (available on Hyprland,
// GNOME 48+, KDE).  The compositor assigns the actual key — the
// preferredTrigger is a hint only.
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

    void setDefaultShortcut(QAction* action, const QKeySequence& defaultShortcut) override
    {
        Q_UNUSED(defaultShortcut)
        // Portal doesn't have a "default" concept — just bind when setGlobalShortcut is called
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

        // BindShortcuts via portal
        QVariantMap options;
        options[QStringLiteral("description")] = action->text();
        if (!shortcut.isEmpty()) {
            options[QStringLiteral("preferred_trigger")] = shortcut.toString();
        }

        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.portal.Desktop"), QStringLiteral("/org/freedesktop/portal/desktop"),
            QStringLiteral("org.freedesktop.portal.GlobalShortcuts"), QStringLiteral("BindShortcuts"));

        // Shortcuts array: list of (id, options) tuples
        QVariantList shortcutsList;
        QVariantMap shortcutEntry;
        shortcutEntry[QStringLiteral("id")] = action->objectName();
        shortcutEntry[QStringLiteral("options")] = options;
        shortcutsList.append(shortcutEntry);

        msg.setArguments({
            QVariant::fromValue(m_sessionHandle),
            QVariant::fromValue(shortcutsList),
            QVariant::fromValue(QStringLiteral("")), // parent_window
            QVariant::fromValue(QVariantMap{}), // options
        });

        QDBusConnection::sessionBus().asyncCall(msg);
    }

    void removeAllShortcuts(QAction* action) override
    {
        if (!action)
            return;
        m_actions.remove(action->objectName());
        // Portal doesn't have per-shortcut removal — shortcuts are session-scoped
    }

    void flush() override
    {
        Q_EMIT shortcutsReady();
    }

private:
    void createSession()
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.portal.Desktop"), QStringLiteral("/org/freedesktop/portal/desktop"),
            QStringLiteral("org.freedesktop.portal.GlobalShortcuts"), QStringLiteral("CreateSession"));
        msg.setArguments({QVariant::fromValue(QVariantMap{})});

        auto reply = QDBusConnection::sessionBus().call(msg);
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            m_sessionHandle = reply.arguments().first().toString();
            qCInfo(lcShortcuts) << "Portal GlobalShortcuts session:" << m_sessionHandle;

            // Listen for Activated signal
            QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.portal.Desktop"), m_sessionHandle,
                                                  QStringLiteral("org.freedesktop.portal.GlobalShortcuts"),
                                                  QStringLiteral("Activated"), this,
                                                  SLOT(onActivated(QString, QVariantMap)));
        } else {
            qCWarning(lcShortcuts) << "Portal GlobalShortcuts session creation failed";
        }
    }

private Q_SLOTS:
    void onActivated(const QString& shortcutId, const QVariantMap& /*options*/)
    {
        QAction* action = m_actions.value(shortcutId);
        if (action) {
            action->trigger();
        }
    }

private:
    QString m_sessionHandle;
    QHash<QString, QAction*> m_actions;
};

// ═══════════════════════════════════════════════════════════════════════════════
// D-Bus Trigger Fallback Backend
//
// For compositors without portal support (Sway, COSMIC).
// Exposes org.plasmazones.daemon.TriggerAction(actionId) on D-Bus.
// Users bind compositor keybindings to:
//   dbus-send --session --dest=org.plasmazones.daemon /Shortcuts
//     org.plasmazones.daemon.TriggerAction string:"toggle-autotile"
// ═══════════════════════════════════════════════════════════════════════════════

class DBusTriggerBackend : public IShortcutBackend
{
    Q_OBJECT
public:
    explicit DBusTriggerBackend(QObject* parent)
        : IShortcutBackend(parent)
    {
        QDBusConnection::sessionBus().registerObject(QStringLiteral("/Shortcuts"), this,
                                                     QDBusConnection::ExportScriptableSlots);
        qCInfo(lcShortcuts) << "D-Bus trigger backend active — bind shortcuts via:"
                            << "dbus-send --session --dest=org.plasmazones.daemon"
                            << "/Shortcuts org.plasmazones.daemon.TriggerAction string:\"<action-id>\"";
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

    /// D-Bus method: org.plasmazones.daemon.TriggerAction(actionId)
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
    // Check for KDE's kglobalaccel service
    QDBusInterface kgaCheck(QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/kglobalaccel"),
                            QStringLiteral("org.kde.KGlobalAccel"), QDBusConnection::sessionBus());
    if (kgaCheck.isValid()) {
        qCInfo(lcShortcuts) << "Using KGlobalAccel shortcut backend";
        return std::make_unique<KGlobalAccelBackend>(parent);
    }

    // Check for XDG Desktop Portal GlobalShortcuts
    QDBusInterface portalCheck(QStringLiteral("org.freedesktop.portal.Desktop"),
                               QStringLiteral("/org/freedesktop/portal/desktop"),
                               QStringLiteral("org.freedesktop.portal.GlobalShortcuts"), QDBusConnection::sessionBus());
    if (portalCheck.isValid()) {
        qCInfo(lcShortcuts) << "Using Portal GlobalShortcuts backend";
        return std::make_unique<PortalShortcutBackend>(parent);
    }

    // Fallback: D-Bus trigger method
    qCInfo(lcShortcuts) << "Using D-Bus trigger fallback backend";
    return std::make_unique<DBusTriggerBackend>(parent);
}

} // namespace PlasmaZones

#include "shortcutbackend.moc"
