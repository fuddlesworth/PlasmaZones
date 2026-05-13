// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/UPowerDevice.h>

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcUPowerDevice, "phosphorservices.upower.device")

namespace {
constexpr auto kService = "org.freedesktop.UPower";
constexpr auto kDeviceIface = "org.freedesktop.UPower.Device";
constexpr auto kPropsIface = "org.freedesktop.DBus.Properties";
} // namespace

namespace PhosphorServices {

static QVariant deviceProp(QDBusConnection& bus, const QString& path, const char* prop)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(kService), path, QLatin1String(kPropsIface),
                                                      QStringLiteral("Get"));
    msg << QLatin1String(kDeviceIface) << QLatin1String(prop);
    QDBusMessage reply = bus.call(msg, QDBus::Block, 200);
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        return reply.arguments().first().value<QDBusVariant>().variant();
    return {};
}

class UPowerDevice::Private
{
public:
    UPowerDevice* owner = nullptr;
    QString path;
    QDBusConnection bus = QDBusConnection::systemBus();

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

        setReal(percentage, deviceProp(bus, path, "Percentage").toDouble(), &UPowerDevice::percentageChanged, owner);
        setReal(energy, deviceProp(bus, path, "Energy").toDouble(), &UPowerDevice::energyChanged, owner);
        setReal(energyFull, deviceProp(bus, path, "EnergyFull").toDouble(), &UPowerDevice::energyCapacityChanged,
                owner);
        setReal(energyRate, deviceProp(bus, path, "EnergyRate").toDouble(), &UPowerDevice::energyRateChanged, owner);
        setStr(nativePath, deviceProp(bus, path, "NativePath").toString(), &UPowerDevice::nativePathChanged, owner);
        setStr(model, deviceProp(bus, path, "Model").toString(), &UPowerDevice::modelChanged, owner);
        setStr(iconName, deviceProp(bus, path, "IconName").toString(), &UPowerDevice::iconNameChanged, owner);
        setBool(powerSupply, deviceProp(bus, path, "PowerSupply").toBool(), &UPowerDevice::powerSupplyChanged, owner);
        setBool(isPresent, deviceProp(bus, path, "IsPresent").toBool(), &UPowerDevice::isPresentChanged, owner);

        auto newState = static_cast<DeviceState>(deviceProp(bus, path, "State").toUInt());
        if (state != newState) {
            state = newState;
            Q_EMIT owner->stateChanged();
        }
        auto newType = static_cast<DeviceType>(deviceProp(bus, path, "Type").toUInt());
        bool oldIsLaptop = (type == Battery && powerSupply);
        if (type != newType) {
            type = newType;
            Q_EMIT owner->typeChanged();
        }
        bool newIsLaptop = (type == Battery && powerSupply);
        if (oldIsLaptop != newIsLaptop)
            Q_EMIT owner->isLaptopBatteryChanged();

        qint64 newTTE = deviceProp(bus, path, "TimeToEmpty").toLongLong();
        if (timeToEmpty != newTTE) {
            timeToEmpty = newTTE;
            Q_EMIT owner->timeToEmptyChanged();
        }
        qint64 newTTF = deviceProp(bus, path, "TimeToFull").toLongLong();
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

    QDBusConnection::systemBus().connect(
        QLatin1String(kService), dbusPath, QStringLiteral("org.freedesktop.DBus.Properties"),
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
    if (iface != QLatin1String(kDeviceIface))
        return;
    d->refreshAll();
}

} // namespace PhosphorServices
