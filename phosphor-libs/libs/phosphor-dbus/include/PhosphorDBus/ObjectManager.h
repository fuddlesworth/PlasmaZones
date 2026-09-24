// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorDBus/phosphordbus_export.h>

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <memory>

class QDBusConnection;
class QDBusMessage;
class QLoggingCategory;

namespace PhosphorDBus {

/// One D-Bus object's interfaces as reported by ObjectManager: a map of
/// interface name → that interface's property dict. This is the demarshalled
/// `a{sa{sv}}` payload carried by `GetManagedObjects` and `InterfacesAdded`.
using InterfaceMap = QMap<QString, QVariantMap>;

/**
 * @brief Service-agnostic observer for `org.freedesktop.DBus.ObjectManager`.
 *
 * Binds a `(connection, service, rootPath)` triple, issues an async
 * `GetManagedObjects` on construction, and subscribes to `InterfacesAdded` /
 * `InterfacesRemoved`. It does NOT materialise typed objects — it emits the
 * raw `(path, interfaces)` payloads and lets each consumer build its own
 * domain objects from them. This keeps the helper reusable across every
 * ObjectManager-rooted service (BlueZ devices, logind sessions, etc.).
 *
 * The initial enumeration and both signals are delivered as raw
 * `QDBusMessage`s and demarshalled by hand, so no nested-container metatype
 * (`a{oa{sa{sv}}}`) ever needs registering.
 *
 * Lifetime: the observer is inert if the bus is disconnected at construction
 * (no call is issued and @ref ready never fires). Otherwise @ref ready fires
 * exactly once, after the initial `GetManagedObjects` round-trip completes —
 * whether it succeeded or errored — so consumers get a deterministic
 * "initial snapshot delivered" edge before relying on incremental signals.
 */
class PHOSPHORDBUS_EXPORT ObjectManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @param connection  Bus the service lives on (e.g. system bus for BlueZ).
     * @param service     Destination D-Bus service name (e.g. `org.bluez`).
     * @param rootPath    Path of the ObjectManager (the `GetManagedObjects`
     *                    target and the signal-match path); BlueZ uses `/`.
     * @param parent      QObject parent.
     * @param log         Logging category for call-failure warnings; when
     *                    null, `lcPhosphorDBus()` is used. Must have static /
     *                    program lifetime (an async callback dereferences it).
     */
    explicit ObjectManager(QDBusConnection connection, QString service, QString rootPath = QStringLiteral("/"),
                           QObject* parent = nullptr, const QLoggingCategory* log = nullptr);
    ~ObjectManager() override;

    [[nodiscard]] QString service() const;
    [[nodiscard]] QString rootPath() const;

    /// True once the initial `GetManagedObjects` round-trip has completed
    /// (success or error). Always false while the bus is disconnected.
    [[nodiscard]] bool isReady() const;

Q_SIGNALS:
    /// A managed object appeared, or the initial walk surfaced it. Carries the
    /// object path and every interface (with properties) the object exposes.
    void interfacesAdded(const QString& path, const PhosphorDBus::InterfaceMap& interfaces);

    /// Some of an object's interfaces went away. @p interfaces lists the
    /// interface names removed; when it covers the object's last interface the
    /// object itself is gone.
    void interfacesRemoved(const QString& path, const QStringList& interfaces);

    /// Emitted once after the initial `GetManagedObjects` round-trip completes.
    void ready();

private Q_SLOTS:
    void _q_onInterfacesAdded(const QDBusMessage& message);
    void _q_onInterfacesRemoved(const QDBusMessage& message);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorDBus
