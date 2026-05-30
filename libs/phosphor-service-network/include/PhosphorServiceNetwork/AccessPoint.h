// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorServiceNetwork/phosphorservicenetwork_export.h>

#include <QObject>
#include <QString>

#include <memory>

namespace PhosphorServiceNetwork {

/// One `org.freedesktop.NetworkManager.AccessPoint` — a Wi-Fi network as
/// seen by a wireless device's last scan. Owned by an AccessPointModel
/// (parented to it), vended to consumers via the model's rows. Reads its
/// SSID / signal strength / frequency / BSSID / security flags
/// asynchronously via Properties.GetAll and keeps the live-varying
/// strength current through a PropertiesChanged subscription.
class PHOSPHORSERVICENETWORK_EXPORT AccessPoint : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AccessPoint)

    Q_PROPERTY(QString ssid READ ssid NOTIFY ssidChanged)
    Q_PROPERTY(int strength READ strength NOTIFY strengthChanged)
    Q_PROPERTY(int frequency READ frequency NOTIFY frequencyChanged)
    Q_PROPERTY(QString bssid READ bssid NOTIFY bssidChanged)
    /// Human-readable security label derived from the AP's flag triple:
    /// "" (open), "WEP", "WPA", "WPA2", "WPA3", or "802.1X". Recomputed
    /// whenever any underlying flag changes.
    Q_PROPERTY(QString security READ security NOTIFY securityChanged)
    Q_PROPERTY(bool secured READ secured NOTIFY securityChanged)

public:
    explicit AccessPoint(const QString& dbusPath, QObject* parent = nullptr);
    ~AccessPoint() override;

    [[nodiscard]] QString dbusPath() const;
    [[nodiscard]] QString ssid() const;
    [[nodiscard]] int strength() const;
    [[nodiscard]] int frequency() const;
    [[nodiscard]] QString bssid() const;
    [[nodiscard]] QString security() const;
    [[nodiscard]] bool secured() const;

Q_SIGNALS:
    void ssidChanged();
    void strengthChanged();
    void frequencyChanged();
    void bssidChanged();
    void securityChanged();

private Q_SLOTS:
    void _q_onPropertiesChanged(const QString& iface, const QVariantMap& changed, const QStringList& invalidated);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace PhosphorServiceNetwork
