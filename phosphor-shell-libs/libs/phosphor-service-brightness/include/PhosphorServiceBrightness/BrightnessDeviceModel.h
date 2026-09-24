// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceBrightness/phosphorservicebrightness_export.h>

#include <PhosphorServiceBrightness/BrightnessDevice.h>
#include <PhosphorServiceBrightness/BrightnessHost.h>

#include <QAbstractListModel>

namespace PhosphorServiceBrightness {

/// List model over a BrightnessHost's devices. Bind `host`, then drive a
/// Repeater/ListView off the rows. Sysfs devices are present at bind time;
/// external displays (DDC/CI) arrive asynchronously, so the model tracks the
/// host's `deviceAdded` / `deviceRemoved` with insert/remove transactions, and
/// forwards per-device `brightness` / `percentage` changes as `dataChanged`.
class PHOSPHORSERVICEBRIGHTNESS_EXPORT BrightnessDeviceModel : public QAbstractListModel
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(BrightnessDeviceModel)
    Q_PROPERTY(PhosphorServiceBrightness::BrightnessHost* host READ host WRITE setHost NOTIFY hostChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        DeviceRole = Qt::UserRole + 1,
        IdRole,
        NameRole,
        KindRole,
        BrightnessRole,
        MaxBrightnessRole,
        PercentageRole,
    };
    Q_ENUM(Roles)

    explicit BrightnessDeviceModel(QObject* parent = nullptr);
    ~BrightnessDeviceModel() override;

    [[nodiscard]] BrightnessHost* host() const;
    void setHost(BrightnessHost* host);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void hostChanged();
    void countChanged();

private Q_SLOTS:
    void onDeviceAdded(PhosphorServiceBrightness::BrightnessDevice* device);
    void onDeviceRemoved(PhosphorServiceBrightness::BrightnessDevice* device);
    void onDeviceBrightnessChanged(PhosphorServiceBrightness::BrightnessDevice* device);

private:
    void connectDevice(BrightnessDevice* device);

    BrightnessHost* m_host = nullptr;
    // Row mirror of host-owned BrightnessDevice pointers; the model does NOT
    // own them. Dangling-safe because the host outlives its devices and clears
    // the model on its own destruction (QObject::destroyed fires before the
    // host's children are deleted).
    QList<BrightnessDevice*> m_rows;
};

} // namespace PhosphorServiceBrightness
