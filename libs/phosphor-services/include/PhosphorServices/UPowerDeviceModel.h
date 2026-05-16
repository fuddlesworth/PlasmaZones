// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServices/phosphorservices_export.h>

#include <PhosphorServices/UPowerDevice.h>
#include <PhosphorServices/UPowerHost.h>

#include <QAbstractListModel>

namespace PhosphorServices {

class PHOSPHORSERVICES_EXPORT UPowerDeviceModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(PhosphorServices::UPowerHost* host READ host WRITE setHost NOTIFY hostChanged)
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
    void onDeviceAdded(PhosphorServices::UPowerDevice* device);
    void onDeviceRemoved(PhosphorServices::UPowerDevice* device);
    void onDeviceDataChanged(PhosphorServices::UPowerDevice* device);

private:
    void connectDevice(UPowerDevice* device);

    UPowerHost* m_host = nullptr;
    // Row mirror owned by the model — see MprisPlayerModel for rationale:
    // keeps the begin/end transaction boundaries correct independent of
    // the host's list-mutation timing.
    QList<UPowerDevice*> m_rows;
};

} // namespace PhosphorServices
