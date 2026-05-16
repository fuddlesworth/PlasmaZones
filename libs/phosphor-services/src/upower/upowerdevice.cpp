// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/UPowerDevice.h>

#include <type_traits>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcUPowerDevice, "phosphorservices.upower.device")

namespace {
constexpr auto kService = "org.freedesktop.UPower";
constexpr auto kDeviceIface = "org.freedesktop.UPower.Device";
constexpr auto kPropsIface = "org.freedesktop.DBus.Properties";
} // namespace

namespace PhosphorServices {

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

    // Returns true when the value actually changed (and the NOTIFY was emitted).
    template<typename T, typename Signal>
    static bool setField(T& field, T val, Signal signal, UPowerDevice* o)
    {
        if constexpr (std::is_same_v<T, qreal>) {
            if (qFuzzyCompare(field + 1.0, val + 1.0))
                return false;
        } else {
            if (field == val)
                return false;
        }
        field = val;
        Q_EMIT(o->*signal)();
        return true;
    }

    // Async GetAll on the Properties interface — one round trip for the
    // whole device, never blocking the calling thread. The watcher is
    // parented to `owner` so an in-flight reply cancels cleanly if the
    // device is removed.
    void requestAll()
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(kService), path, QLatin1String(kPropsIface),
                                                          QStringLiteral("GetAll"));
        msg << QLatin1String(kDeviceIface);
        auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), owner);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner, [this](QDBusPendingCallWatcher* call) {
            call->deleteLater();
            const QDBusPendingReply<QVariantMap> reply = *call;
            if (reply.isError()) {
                qCDebug(lcUPowerDevice) << "GetAll failed for" << path << ":" << reply.error().message();
                return;
            }
            applyProps(reply.value());
        });
    }

    // Applies a device-interface property map. Works for both a full
    // GetAll reply and a partial PropertiesChanged `changed` map — every
    // field is gated on isValid().
    void applyProps(const QVariantMap& props)
    {
        // Snapshot derived state up front so we can detect transitions
        // caused by ANY of their inputs (isLaptopBattery depends on both
        // `type` and `powerSupply`; healthPercentage on the energy pair).
        const bool oldIsLaptop = (type == Battery && powerSupply);
        const qreal oldHealth = owner->healthPercentage();
        bool changed = false;

        auto val = [&props](const char* name) -> QVariant {
            return props.value(QLatin1String(name));
        };
        QVariant v;

        if ((v = val("Percentage")).isValid())
            changed |= setField(percentage, v.toDouble(), &UPowerDevice::percentageChanged, owner);
        if ((v = val("Energy")).isValid())
            changed |= setField(energy, v.toDouble(), &UPowerDevice::energyChanged, owner);
        if ((v = val("EnergyFull")).isValid())
            changed |= setField(energyFull, v.toDouble(), &UPowerDevice::energyCapacityChanged, owner);
        if ((v = val("EnergyFullDesign")).isValid())
            changed |= setField(energyFullDesign, v.toDouble(), &UPowerDevice::energyFullDesignChanged, owner);
        if ((v = val("EnergyRate")).isValid())
            changed |= setField(energyRate, v.toDouble(), &UPowerDevice::energyRateChanged, owner);
        if ((v = val("NativePath")).isValid())
            changed |= setField(nativePath, v.toString(), &UPowerDevice::nativePathChanged, owner);
        if ((v = val("Model")).isValid())
            changed |= setField(model, v.toString(), &UPowerDevice::modelChanged, owner);
        if ((v = val("IconName")).isValid())
            changed |= setField(iconName, v.toString(), &UPowerDevice::iconNameChanged, owner);
        if ((v = val("PowerSupply")).isValid())
            changed |= setField(powerSupply, v.toBool(), &UPowerDevice::powerSupplyChanged, owner);
        if ((v = val("IsPresent")).isValid())
            changed |= setField(isPresent, v.toBool(), &UPowerDevice::isPresentChanged, owner);

        if ((v = val("State")).isValid())
            changed |= setField(state, static_cast<DeviceState>(v.toUInt()), &UPowerDevice::stateChanged, owner);
        if ((v = val("Type")).isValid())
            changed |= setField(type, static_cast<DeviceType>(v.toUInt()), &UPowerDevice::typeChanged, owner);
        if ((v = val("TimeToEmpty")).isValid())
            changed |= setField(timeToEmpty, v.toLongLong(), &UPowerDevice::timeToEmptyChanged, owner);
        if ((v = val("TimeToFull")).isValid())
            changed |= setField(timeToFull, v.toLongLong(), &UPowerDevice::timeToFullChanged, owner);

        // Derived-state transitions — emitted only on an actual change,
        // catching the case where only one input (e.g. powerSupply)
        // moved without the other.
        if (oldIsLaptop != (type == Battery && powerSupply))
            Q_EMIT owner->isLaptopBatteryChanged();
        const qreal newHealth = owner->healthPercentage();
        if (!qFuzzyCompare(oldHealth + 1.0, newHealth + 1.0))
            Q_EMIT owner->healthPercentageChanged();

        // propertiesRefreshed is the model's data-changed hook — fire it
        // only when something the model exposes could have moved.
        if (changed)
            Q_EMIT owner->propertiesRefreshed();
    }
};

UPowerDevice::UPowerDevice(const QString& dbusPath, QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->owner = this;
    d->path = dbusPath;

    d->bus.connect(QLatin1String(kService), dbusPath, QLatin1String(kPropsIface), QStringLiteral("PropertiesChanged"),
                   this, SLOT(_q_onPropertiesChanged(QString, QVariantMap, QStringList)));

    d->requestAll();
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
    d->applyProps(changed);
    // Invalidated properties carry no value — re-fetch the whole
    // interface asynchronously to pick them up.
    if (!invalidated.isEmpty())
        d->requestAll();
}

} // namespace PhosphorServices
