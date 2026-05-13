// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/UPowerDevice.h>

#include "upower_device_interface.h"

#include <QDBusConnection>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcUPowerDevice, "phosphorservices.upower.device")

static constexpr auto kUPowerService = "org.freedesktop.UPower";

namespace PhosphorServices {

class UPowerDevice::Private
{
public:
    UPowerDevice* owner = nullptr;
    QString path;
    std::unique_ptr<OrgFreedesktopUPowerDeviceInterface> proxy;

    qreal percentage = 0.0;
    DeviceState state = UnknownState;
    DeviceType type = UnknownType;
    qint64 timeToEmpty = 0;
    qint64 timeToFull = 0;
    qreal energy = 0.0;
    qreal energyFull = 0.0;
    qreal energyRate = 0.0;
    QString nativePath;
    QString model;
    QString iconName;
    bool powerSupply = false;
    bool isPresent = false;

    void refreshAll()
    {
        auto setReal = [](qreal& field, qreal val, auto signal, auto* o) {
            if (qFuzzyCompare(field + 1.0, val + 1.0))
                return;
            field = val;
            Q_EMIT(o->*signal)();
        };
        auto setStr = [](QString& field, const QString& val, auto signal, auto* o) {
            if (field == val)
                return;
            field = val;
            Q_EMIT(o->*signal)();
        };
        auto setBool = [](bool& field, bool val, auto signal, auto* o) {
            if (field == val)
                return;
            field = val;
            Q_EMIT(o->*signal)();
        };

        setReal(percentage, proxy->percentage(), &UPowerDevice::percentageChanged, owner);
        setReal(energy, proxy->energy(), &UPowerDevice::energyChanged, owner);
        setReal(energyFull, proxy->energyFull(), &UPowerDevice::energyCapacityChanged, owner);
        setReal(energyRate, proxy->energyRate(), &UPowerDevice::energyRateChanged, owner);
        setStr(nativePath, proxy->nativePath(), &UPowerDevice::nativePathChanged, owner);
        setStr(model, proxy->model(), &UPowerDevice::modelChanged, owner);
        setStr(iconName, proxy->iconName(), &UPowerDevice::iconNameChanged, owner);
        setBool(powerSupply, proxy->powerSupply(), &UPowerDevice::powerSupplyChanged, owner);
        setBool(isPresent, proxy->isPresent(), &UPowerDevice::isPresentChanged, owner);

        auto newState = static_cast<DeviceState>(proxy->state());
        if (state != newState) {
            state = newState;
            Q_EMIT owner->stateChanged();
        }
        auto newType = static_cast<DeviceType>(proxy->type());
        bool oldIsLaptop = (type == Battery && powerSupply);
        if (type != newType) {
            type = newType;
            Q_EMIT owner->typeChanged();
        }
        bool newIsLaptop = (type == Battery && powerSupply);
        if (oldIsLaptop != newIsLaptop)
            Q_EMIT owner->isLaptopBatteryChanged();

        qint64 newTTE = proxy->timeToEmpty();
        if (timeToEmpty != newTTE) {
            timeToEmpty = newTTE;
            Q_EMIT owner->timeToEmptyChanged();
        }
        qint64 newTTF = proxy->timeToFull();
        if (timeToFull != newTTF) {
            timeToFull = newTTF;
            Q_EMIT owner->timeToFullChanged();
        }

        Q_EMIT owner->healthPercentageChanged();
        Q_EMIT owner->propertiesRefreshed();
    }
};

UPowerDevice::UPowerDevice(const QString& dbusPath, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->owner = this;
    d->path = dbusPath;
    d->proxy = std::make_unique<OrgFreedesktopUPowerDeviceInterface>(QLatin1String(kUPowerService), dbusPath,
                                                                     QDBusConnection::systemBus(), this);

    QDBusConnection::systemBus().connect(
        QLatin1String(kUPowerService), dbusPath, QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"), this, SLOT(_q_onPropertiesChanged(QString, QVariantMap, QStringList)));

    d->refreshAll();
}

UPowerDevice::~UPowerDevice() = default;

QString UPowerDevice::dbusPath() const
{
    return d->path;
}
qreal UPowerDevice::percentage() const
{
    return d->percentage;
}
UPowerDevice::DeviceState UPowerDevice::state() const
{
    return d->state;
}
UPowerDevice::DeviceType UPowerDevice::type() const
{
    return d->type;
}
qreal UPowerDevice::timeToEmpty() const
{
    return static_cast<qreal>(d->timeToEmpty);
}
qreal UPowerDevice::timeToFull() const
{
    return static_cast<qreal>(d->timeToFull);
}
qreal UPowerDevice::energy() const
{
    return d->energy;
}
qreal UPowerDevice::energyCapacity() const
{
    return d->energyFull;
}
qreal UPowerDevice::energyRate() const
{
    return d->energyRate;
}
QString UPowerDevice::nativePath() const
{
    return d->nativePath;
}
QString UPowerDevice::model() const
{
    return d->model;
}
QString UPowerDevice::iconName() const
{
    return d->iconName;
}
bool UPowerDevice::powerSupply() const
{
    return d->powerSupply;
}
bool UPowerDevice::isPresent() const
{
    return d->isPresent;
}
bool UPowerDevice::isLaptopBattery() const
{
    return d->type == Battery && d->powerSupply;
}

qreal UPowerDevice::healthPercentage() const
{
    if (d->energyFull <= 0.0)
        return 0.0;
    return (d->energy / d->energyFull) * 100.0;
}

void UPowerDevice::_q_onPropertiesChanged(const QString& iface, const QVariantMap& /*changed*/,
                                          const QStringList& /*invalidated*/)
{
    if (iface != QLatin1String("org.freedesktop.UPower.Device"))
        return;
    d->refreshAll();
}

} // namespace PhosphorServices
