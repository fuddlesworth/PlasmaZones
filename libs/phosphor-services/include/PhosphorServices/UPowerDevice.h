// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServices/phosphorservices_export.h>

#include <QObject>
#include <QString>

#include <memory>

namespace PhosphorServices {

class PHOSPHORSERVICES_EXPORT UPowerDevice : public QObject
{
    Q_OBJECT

    Q_PROPERTY(qreal percentage READ percentage NOTIFY percentageChanged)
    Q_PROPERTY(DeviceState state READ state NOTIFY stateChanged)
    Q_PROPERTY(DeviceType type READ type NOTIFY typeChanged)
    Q_PROPERTY(qreal timeToEmpty READ timeToEmpty NOTIFY timeToEmptyChanged)
    Q_PROPERTY(qreal timeToFull READ timeToFull NOTIFY timeToFullChanged)
    Q_PROPERTY(qreal energy READ energy NOTIFY energyChanged)
    Q_PROPERTY(qreal energyCapacity READ energyCapacity NOTIFY energyCapacityChanged)
    Q_PROPERTY(qreal energyRate READ energyRate NOTIFY energyRateChanged)
    Q_PROPERTY(QString nativePath READ nativePath NOTIFY nativePathChanged)
    Q_PROPERTY(QString model READ model NOTIFY modelChanged)
    Q_PROPERTY(QString iconName READ iconName NOTIFY iconNameChanged)
    Q_PROPERTY(bool powerSupply READ powerSupply NOTIFY powerSupplyChanged)
    Q_PROPERTY(bool isPresent READ isPresent NOTIFY isPresentChanged)
    Q_PROPERTY(bool isLaptopBattery READ isLaptopBattery NOTIFY isLaptopBatteryChanged)
    Q_PROPERTY(qreal healthPercentage READ healthPercentage NOTIFY healthPercentageChanged)

public:
    enum DeviceState {
        UnknownState = 0,
        Charging = 1,
        Discharging = 2,
        Empty = 3,
        FullyCharged = 4,
        PendingCharge = 5,
        PendingDischarge = 6,
    };
    Q_ENUM(DeviceState)

    enum DeviceType {
        UnknownType = 0,
        LinePower = 1,
        Battery = 2,
        Ups = 3,
        Monitor = 4,
        Mouse = 5,
        Keyboard = 6,
        Pda = 7,
        Phone = 8,
        MediaPlayer = 9,
    };
    Q_ENUM(DeviceType)

    explicit UPowerDevice(const QString& dbusPath, QObject* parent = nullptr);
    ~UPowerDevice() override;

    [[nodiscard]] QString dbusPath() const;
    [[nodiscard]] qreal percentage() const;
    [[nodiscard]] DeviceState state() const;
    [[nodiscard]] DeviceType type() const;
    [[nodiscard]] qreal timeToEmpty() const;
    [[nodiscard]] qreal timeToFull() const;
    [[nodiscard]] qreal energy() const;
    [[nodiscard]] qreal energyCapacity() const;
    [[nodiscard]] qreal energyRate() const;
    [[nodiscard]] QString nativePath() const;
    [[nodiscard]] QString model() const;
    [[nodiscard]] QString iconName() const;
    [[nodiscard]] bool powerSupply() const;
    [[nodiscard]] bool isPresent() const;
    [[nodiscard]] bool isLaptopBattery() const;
    [[nodiscard]] qreal healthPercentage() const;

Q_SIGNALS:
    void percentageChanged();
    void stateChanged();
    void typeChanged();
    void timeToEmptyChanged();
    void timeToFullChanged();
    void energyChanged();
    void energyCapacityChanged();
    void energyRateChanged();
    void nativePathChanged();
    void modelChanged();
    void iconNameChanged();
    void powerSupplyChanged();
    void isPresentChanged();
    void isLaptopBatteryChanged();
    void healthPercentageChanged();
    void propertiesRefreshed();

private Q_SLOTS:
    void _q_onPropertiesChanged(const QString& iface, const QVariantMap& changed, const QStringList& invalidated);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServices
