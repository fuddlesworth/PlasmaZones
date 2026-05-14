// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/UPowerDevice.h>

#include <type_traits>

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
    // 2 s timeout — see mprisplayer.cpp:dbusProperty for the rationale.
    // UPower is usually fast (system bus, kernel-side state) but a slow
    // initial response would silently leave the battery indicator empty.
    QDBusMessage reply = bus.call(msg, QDBus::Block, 2000);
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
    qreal energyFullDesign = 0.0;
    qreal energyRate = 0.0;
    QString nativePath;
    QString model;
    QString iconName;
    bool powerSupply = false;
    bool isPresent = false;

    template <typename T, typename Signal>
    static void setField(T& field, T val, Signal signal, UPowerDevice* o)
    {
        if constexpr (std::is_same_v<T, qreal>) {
            if (qFuzzyCompare(field + 1.0, val + 1.0))
                return;
        } else {
            if (field == val)
                return;
        }
        field = val;
        Q_EMIT(o->*signal)();
    }

    void refreshAll()
    {
        setField(percentage, deviceProp(bus, path, "Percentage").toDouble(), &UPowerDevice::percentageChanged, owner);
        setField(energy, deviceProp(bus, path, "Energy").toDouble(), &UPowerDevice::energyChanged, owner);
        setField(energyFull, deviceProp(bus, path, "EnergyFull").toDouble(), &UPowerDevice::energyCapacityChanged,
                owner);
        setField(energyFullDesign, deviceProp(bus, path, "EnergyFullDesign").toDouble(),
                &UPowerDevice::energyFullDesignChanged, owner);
        setField(energyRate, deviceProp(bus, path, "EnergyRate").toDouble(), &UPowerDevice::energyRateChanged, owner);
        setField(nativePath, deviceProp(bus, path, "NativePath").toString(), &UPowerDevice::nativePathChanged, owner);
        setField(model, deviceProp(bus, path, "Model").toString(), &UPowerDevice::modelChanged, owner);
        setField(iconName, deviceProp(bus, path, "IconName").toString(), &UPowerDevice::iconNameChanged, owner);
        setField(powerSupply, deviceProp(bus, path, "PowerSupply").toBool(), &UPowerDevice::powerSupplyChanged, owner);
        setField(isPresent, deviceProp(bus, path, "IsPresent").toBool(), &UPowerDevice::isPresentChanged, owner);

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

    void refreshChanged(const QVariantMap& changed, const QStringList& invalidated)
    {
        auto propVal = [this, &changed, &invalidated](const char* name) -> QVariant {
            QString key = QLatin1String(name);
            if (changed.contains(key))
                return changed.value(key);
            if (invalidated.contains(key))
                return deviceProp(bus, path, name);
            return {};
        };

        QVariant v;
        if ((v = propVal("Percentage")).isValid())
            setField(percentage, v.toDouble(), &UPowerDevice::percentageChanged, owner);
        if ((v = propVal("Energy")).isValid())
            setField(energy, v.toDouble(), &UPowerDevice::energyChanged, owner);
        if ((v = propVal("EnergyFull")).isValid())
            setField(energyFull, v.toDouble(), &UPowerDevice::energyCapacityChanged, owner);
        if ((v = propVal("EnergyFullDesign")).isValid())
            setField(energyFullDesign, v.toDouble(), &UPowerDevice::energyFullDesignChanged, owner);
        if ((v = propVal("EnergyRate")).isValid())
            setField(energyRate, v.toDouble(), &UPowerDevice::energyRateChanged, owner);
        if ((v = propVal("NativePath")).isValid())
            setField(nativePath, v.toString(), &UPowerDevice::nativePathChanged, owner);
        if ((v = propVal("Model")).isValid())
            setField(model, v.toString(), &UPowerDevice::modelChanged, owner);
        if ((v = propVal("IconName")).isValid())
            setField(iconName, v.toString(), &UPowerDevice::iconNameChanged, owner);
        if ((v = propVal("PowerSupply")).isValid())
            setField(powerSupply, v.toBool(), &UPowerDevice::powerSupplyChanged, owner);
        if ((v = propVal("IsPresent")).isValid())
            setField(isPresent, v.toBool(), &UPowerDevice::isPresentChanged, owner);

        if ((v = propVal("State")).isValid()) {
            auto newState = static_cast<DeviceState>(v.toUInt());
            if (state != newState) {
                state = newState;
                Q_EMIT owner->stateChanged();
            }
        }
        if ((v = propVal("Type")).isValid()) {
            auto newType = static_cast<DeviceType>(v.toUInt());
            bool oldIsLaptop = (type == Battery && powerSupply);
            if (type != newType) {
                type = newType;
                Q_EMIT owner->typeChanged();
            }
            bool newIsLaptop = (type == Battery && powerSupply);
            if (oldIsLaptop != newIsLaptop)
                Q_EMIT owner->isLaptopBatteryChanged();
        }
        if ((v = propVal("TimeToEmpty")).isValid()) {
            qint64 newTTE = v.toLongLong();
            if (timeToEmpty != newTTE) {
                timeToEmpty = newTTE;
                Q_EMIT owner->timeToEmptyChanged();
            }
        }
        if ((v = propVal("TimeToFull")).isValid()) {
            qint64 newTTF = v.toLongLong();
            if (timeToFull != newTTF) {
                timeToFull = newTTF;
                Q_EMIT owner->timeToFullChanged();
            }
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
qreal UPowerDevice::energyFullDesign() const
{
    return d->energyFullDesign;
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
    if (d->energyFullDesign <= 0.0)
        return 0.0;
    return (d->energyFull / d->energyFullDesign) * 100.0;
}

void UPowerDevice::_q_onPropertiesChanged(const QString& iface, const QVariantMap& changed,
                                          const QStringList& invalidated)
{
    if (iface != QLatin1String(kDeviceIface))
        return;
    d->refreshChanged(changed, invalidated);
}

} // namespace PhosphorServices
