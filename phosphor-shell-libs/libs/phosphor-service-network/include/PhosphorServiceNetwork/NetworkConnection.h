// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceNetwork/phosphorservicenetwork_export.h>

#include <QObject>
#include <QString>

#include <memory>

namespace PhosphorServiceNetwork {

/// One saved profile — an `org.freedesktop.NetworkManager.Settings.Connection`.
/// Owned by a NetworkConnectionModel (parented to it), vended via the
/// model's rows. Reads its id / uuid / type from the `connection` group of
/// GetSettings() and refreshes on the connection's `Updated` signal.
class PHOSPHORSERVICENETWORK_EXPORT NetworkConnection : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NetworkConnection)

    Q_PROPERTY(QString id READ id NOTIFY idChanged)
    Q_PROPERTY(QString uuid READ uuid NOTIFY uuidChanged)
    Q_PROPERTY(QString connectionType READ connectionType NOTIFY connectionTypeChanged)

public:
    explicit NetworkConnection(const QString& dbusPath, QObject* parent = nullptr);
    ~NetworkConnection() override;

    [[nodiscard]] QString dbusPath() const;
    [[nodiscard]] QString id() const;
    [[nodiscard]] QString uuid() const;
    /// The NetworkManager connection type token, e.g. "802-11-wireless",
    /// "802-3-ethernet", "vpn".
    [[nodiscard]] QString connectionType() const;

Q_SIGNALS:
    void idChanged();
    void uuidChanged();
    void connectionTypeChanged();

private Q_SLOTS:
    void _q_onUpdated();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceNetwork
