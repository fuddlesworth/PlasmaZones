// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceBluetooth/phosphorservicebluetooth_export.h>

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <memory>

class QDBusConnection;

namespace PhosphorServiceBluetooth {

/**
 * @brief One BlueZ adapter (`org.bluez.Adapter1`), e.g. `/org/bluez/hci0`.
 *
 * Read-only view of the adapter's state. Initial properties are supplied by
 * `BluetoothHost` from the ObjectManager enumeration (no construction-time
 * round-trip); subsequent changes arrive via `PropertiesChanged`. The
 * discovery / power write surface lands on this class in a later milestone.
 *
 * Owned by `BluetoothHost`; never constructed from QML.
 */
class PHOSPHORSERVICEBLUETOOTH_EXPORT BluetoothAdapter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString dbusPath READ dbusPath CONSTANT)
    Q_PROPERTY(QString address READ address NOTIFY addressChanged)
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    Q_PROPERTY(QString alias READ alias NOTIFY aliasChanged)
    Q_PROPERTY(bool powered READ powered NOTIFY poweredChanged)
    Q_PROPERTY(bool discoverable READ discoverable NOTIFY discoverableChanged)
    Q_PROPERTY(bool pairable READ pairable NOTIFY pairableChanged)
    Q_PROPERTY(bool discovering READ discovering NOTIFY discoveringChanged)

public:
    BluetoothAdapter(QDBusConnection connection, const QString& dbusPath, const QVariantMap& initialProperties,
                     QObject* parent = nullptr);
    ~BluetoothAdapter() override;

    [[nodiscard]] QString dbusPath() const;
    [[nodiscard]] QString address() const;
    [[nodiscard]] QString name() const;
    [[nodiscard]] QString alias() const;
    [[nodiscard]] bool powered() const;
    [[nodiscard]] bool discoverable() const;
    [[nodiscard]] bool pairable() const;
    [[nodiscard]] bool discovering() const;

Q_SIGNALS:
    void addressChanged();
    void nameChanged();
    void aliasChanged();
    void poweredChanged();
    void discoverableChanged();
    void pairableChanged();
    void discoveringChanged();

private Q_SLOTS:
    void _q_onPropertiesChanged(const QString& interfaceName, const QVariantMap& changed,
                                const QStringList& invalidated);

private:
    Q_DISABLE_COPY_MOVE(BluetoothAdapter)
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceBluetooth
