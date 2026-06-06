// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceNetwork/AccessPointModel.h>

#include <PhosphorDBus/Client.h>

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QPointer>

Q_LOGGING_CATEGORY(lcAccessPointModel, "phosphor.service.network.apmodel")

namespace {
constexpr auto kService = "org.freedesktop.NetworkManager";
constexpr auto kWirelessIface = "org.freedesktop.NetworkManager.Device.Wireless";
} // namespace

namespace PhosphorServiceNetwork {

AccessPointModel::AccessPointModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

AccessPointModel::~AccessPointModel() = default;

NetworkDevice* AccessPointModel::device() const
{
    return m_device;
}

void AccessPointModel::setDevice(NetworkDevice* device)
{
    if (m_device == device)
        return;
    if (m_device) {
        unsubscribe();
        disconnect(m_device, nullptr, this, nullptr);
    }
    // Detach before clearRows(): its model-reset signals can re-enter
    // consumer code (proxy models, QML delegates), which must observe the
    // detached state rather than the device being unbound. This mirrors the
    // device-destroyed handler below, which documents the same ordering.
    m_device = nullptr;
    clearRows();
    m_device = device;
    if (m_device) {
        // Auto-detach if the device is destroyed while still bound, so the
        // model never dereferences a dangling NetworkDevice. unsubscribe()
        // uses the cached path, not m_device, which is essential here: by
        // the time QObject::destroyed fires, NetworkDevice's pimpl is gone.
        connect(m_device, &QObject::destroyed, this, [this]() {
            // Null m_device FIRST: clearRows() emits model-reset signals,
            // and anything they re-enter must observe the detached state,
            // never the dying device pointer. unsubscribe() keys off the
            // cached m_subscribedPath, so its order relative to this is
            // immaterial.
            m_device = nullptr;
            unsubscribe();
            clearRows();
            Q_EMIT deviceChanged();
        });
        subscribe();
        rebuild();
    }
    Q_EMIT deviceChanged();
}

void AccessPointModel::subscribe()
{
    if (!m_device || !m_bus.isConnected())
        return;
    // Subscribe regardless of the device's cached type: a non-wireless
    // device simply never emits these, and GetAllAccessPoints below
    // returns an error we ignore. This sidesteps the race where the
    // device's own DeviceType hasn't been read back from the bus yet.
    m_subscribedPath = m_device->dbusPath();
    m_bus.connect(QLatin1String(kService), m_subscribedPath, QLatin1String(kWirelessIface),
                  QStringLiteral("AccessPointAdded"), this, SLOT(_q_onAccessPointAdded(QDBusObjectPath)));
    m_bus.connect(QLatin1String(kService), m_subscribedPath, QLatin1String(kWirelessIface),
                  QStringLiteral("AccessPointRemoved"), this, SLOT(_q_onAccessPointRemoved(QDBusObjectPath)));
}

void AccessPointModel::unsubscribe()
{
    // Keyed off the cached path, never m_device: this runs from the
    // device-destroyed handler too, where the device is already gone.
    if (m_subscribedPath.isEmpty() || !m_bus.isConnected()) {
        m_subscribedPath.clear();
        return;
    }
    m_bus.disconnect(QLatin1String(kService), m_subscribedPath, QLatin1String(kWirelessIface),
                     QStringLiteral("AccessPointAdded"), this, SLOT(_q_onAccessPointAdded(QDBusObjectPath)));
    m_bus.disconnect(QLatin1String(kService), m_subscribedPath, QLatin1String(kWirelessIface),
                     QStringLiteral("AccessPointRemoved"), this, SLOT(_q_onAccessPointRemoved(QDBusObjectPath)));
    m_subscribedPath.clear();
}

void AccessPointModel::rebuild()
{
    if (!m_device || !m_bus.isConnected())
        return;
    PhosphorDBus::Client client(m_bus, QLatin1String(kService), m_device->dbusPath(), &lcAccessPointModel());
    auto* watcher = new QDBusPendingCallWatcher(
        client.asyncCall(QLatin1String(kWirelessIface), QStringLiteral("GetAllAccessPoints")), this);
    // Capture the device we queried for so a reply that lands after a
    // setDevice swap is dropped instead of populating the wrong device's
    // rows.
    QPointer<NetworkDevice> queried = m_device;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, queried](QDBusPendingCallWatcher* call) {
        call->deleteLater();
        const QDBusPendingReply<QList<QDBusObjectPath>> reply = *call;
        if (reply.isError()) {
            // Expected for non-wireless devices (UnknownMethod); debug only.
            qCDebug(lcAccessPointModel) << "GetAllAccessPoints failed:" << reply.error().message();
            return;
        }
        // Drop the reply if the model detached or swapped devices since the
        // query. The explicit !m_device check matters in addition to the
        // QPointer compare: if the queried device was destroyed, both
        // m_device and `queried` are null, and `m_device != queried` alone
        // would be false — without this guard a late non-error reply could
        // populate a model that no longer has a device bound.
        if (!m_device || m_device != queried)
            return;
        const auto paths = reply.value();
        for (const QDBusObjectPath& p : paths)
            addAccessPoint(p.path());
    });
}

void AccessPointModel::clearRows()
{
    if (m_rows.isEmpty())
        return;
    beginResetModel();
    for (auto* ap : std::as_const(m_rows))
        ap->deleteLater();
    m_rows.clear();
    endResetModel();
    Q_EMIT countChanged();
}

void AccessPointModel::addAccessPoint(const QString& path)
{
    for (auto* ap : std::as_const(m_rows)) {
        if (ap->dbusPath() == path)
            return;
    }
    const int row = m_rows.size();
    beginInsertRows({}, row, row);
    auto* ap = new AccessPoint(path, this);
    m_rows.append(ap);
    connectAccessPoint(ap);
    endInsertRows();
    Q_EMIT countChanged();
}

void AccessPointModel::removeAccessPoint(const QString& path)
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i)->dbusPath() == path) {
            auto* ap = m_rows.at(i);
            beginRemoveRows({}, i, i);
            m_rows.removeAt(i);
            endRemoveRows();
            ap->deleteLater();
            Q_EMIT countChanged();
            return;
        }
    }
}

void AccessPointModel::connectAccessPoint(AccessPoint* ap)
{
    auto emitRoles = [this, ap](const QList<int>& roles) {
        const int row = m_rows.indexOf(ap);
        if (row >= 0) {
            const auto idx = index(row);
            Q_EMIT dataChanged(idx, idx, roles);
        }
    };
    connect(ap, &AccessPoint::ssidChanged, this, [emitRoles]() {
        emitRoles({SsidRole});
    });
    connect(ap, &AccessPoint::strengthChanged, this, [emitRoles]() {
        emitRoles({StrengthRole});
    });
    connect(ap, &AccessPoint::frequencyChanged, this, [emitRoles]() {
        emitRoles({FrequencyRole});
    });
    connect(ap, &AccessPoint::bssidChanged, this, [emitRoles]() {
        emitRoles({BssidRole});
    });
    connect(ap, &AccessPoint::securityChanged, this, [emitRoles]() {
        emitRoles({SecurityRole, SecuredRole});
    });
}

int AccessPointModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

QVariant AccessPointModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    auto* ap = m_rows.at(index.row());
    if (!ap)
        return {};
    switch (role) {
    case AccessPointRole:
        return QVariant::fromValue<QObject*>(ap);
    case SsidRole:
        return ap->ssid();
    case StrengthRole:
        return ap->strength();
    case FrequencyRole:
        return ap->frequency();
    case BssidRole:
        return ap->bssid();
    case SecurityRole:
        return ap->security();
    case SecuredRole:
        return ap->secured();
    default:
        return {};
    }
}

QHash<int, QByteArray> AccessPointModel::roleNames() const
{
    return {
        {AccessPointRole, "accessPoint"}, {SsidRole, "ssid"},   {StrengthRole, "strength"},
        {FrequencyRole, "frequency"},     {BssidRole, "bssid"}, {SecurityRole, "security"},
        {SecuredRole, "secured"},
    };
}

void AccessPointModel::_q_onAccessPointAdded(const QDBusObjectPath& path)
{
    addAccessPoint(path.path());
}

void AccessPointModel::_q_onAccessPointRemoved(const QDBusObjectPath& path)
{
    removeAccessPoint(path.path());
}

} // namespace PhosphorServiceNetwork
