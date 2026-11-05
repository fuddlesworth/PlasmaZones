// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceUPower/phosphorserviceupower_export.h>

#include <PhosphorServiceUPower/UPowerDevice.h>
#include <PhosphorServiceUPower/UPowerHost.h>

#include <QAbstractListModel>

namespace PhosphorServiceUPower {

class PHOSPHORSERVICEUPOWER_EXPORT UPowerDeviceModel : public QAbstractListModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(UPowerDeviceModel)
    Q_PROPERTY(PhosphorServiceUPower::UPowerHost* host READ host WRITE setHost NOTIFY hostChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        DeviceRole = Qt::UserRole + 1,
        PercentageRole,
        StateRole,
        TypeRole,
        IconNameRole,
        IsLaptopBatteryRole,
    };
    Q_ENUM(Roles)

    explicit UPowerDeviceModel(QObject* parent = nullptr);
    ~UPowerDeviceModel() override;

    [[nodiscard]] UPowerHost* host() const;
    void setHost(UPowerHost* host);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void hostChanged();
    void countChanged();

private Q_SLOTS:
    void onDeviceAdded(PhosphorServiceUPower::UPowerDevice* device);
    void onDeviceRemoved(PhosphorServiceUPower::UPowerDevice* device);
    void onDeviceDataChanged(PhosphorServiceUPower::UPowerDevice* device, const QList<int>& roles);

private:
    void connectDevice(UPowerDevice* device);

    UPowerHost* m_host = nullptr;
    // Row mirror owned by the model. rowCount and data index into this
    // list, never the host's. Keeping a local mirror means the
    // begin/end-insert/remove transaction boundaries always straddle
    // the actual mutation regardless of when the host emits
    // deviceAdded/deviceRemoved relative to its own list.
    QList<UPowerDevice*> m_rows;
};

} // namespace PhosphorServiceUPower
