// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorServiceNetwork/NetworkHost.h>
#include <PhosphorServiceNetwork/NetworkDevice.h>
#include <PhosphorServiceNetwork/NetworkConnection.h>
#include <PhosphorServiceNetwork/AccessPoint.h>
#include <QDBusVirtualObject>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusArgument>
#include <QtTest>
#include <QTimer>
using namespace PhosphorServiceNetwork;
using Settings = QMap<QString, QVariantMap>;
Q_DECLARE_METATYPE(Settings)
namespace NetworkFixture {
const QString manager = QStringLiteral("/org/freedesktop/NetworkManager");
const QString devicePath = manager + QStringLiteral("/Devices/1");
const QString apPath = manager + QStringLiteral("/AccessPoints/1");
const QString profilePath = manager + QStringLiteral("/Settings/1");
class ManagerFixture : public QDBusVirtualObject
{
public:
    Settings settings;
    QStringList calls;
    bool denyScan = false;
    bool delayActivation = false;
    QString introspect(const QString&) const override
    {
        return {};
    }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& bus) override
    {
        calls.append(message.member());
        QVariantList result;
        if (message.member() == QLatin1String("GetAll")) {
            const auto iface = message.arguments().value(0).toString();
            QVariantMap props;
            if (iface == QLatin1String("org.freedesktop.NetworkManager"))
                props = {{QStringLiteral("NetworkingEnabled"), true},
                         {QStringLiteral("WirelessEnabled"), true},
                         {QStringLiteral("WirelessHardwareEnabled"), true},
                         {QStringLiteral("Connectivity"), 4u},
                         {QStringLiteral("ConnectivityCheckUri"), QStringLiteral("https://example.test/check")}};
            else if (iface.endsWith(QLatin1String("Device.Wireless")))
                props = {{QStringLiteral("ActiveAccessPoint"), QVariant::fromValue(QDBusObjectPath(apPath))},
                         {QStringLiteral("Bitrate"), 240000u},
                         {QStringLiteral("LastScan"), qint64(1234)}};
            else if (iface.endsWith(QLatin1String("Device")))
                props = {{QStringLiteral("Interface"), QStringLiteral("wlan-test")},
                         {QStringLiteral("DeviceType"), 2u},
                         {QStringLiteral("State"), 100u},
                         {QStringLiteral("Managed"), true}};
            else if (iface.endsWith(QLatin1String("AccessPoint")))
                props = {{QStringLiteral("Ssid"), QByteArray("Fixture Wi-Fi")},
                         {QStringLiteral("Strength"), uchar(86)},
                         {QStringLiteral("Frequency"), 5180u},
                         {QStringLiteral("RsnFlags"), 256u}};
            result << props;
        } else if (message.member() == QLatin1String("GetDevices")) {
            result << QVariant::fromValue(QList<QDBusObjectPath>{QDBusObjectPath(devicePath)});
        } else if (message.member() == QLatin1String("GetSettings")) {
            result << QVariant::fromValue(
                Settings{{QStringLiteral("connection"),
                          {{QStringLiteral("id"), QStringLiteral("Custom profile name")},
                           {QStringLiteral("type"), QStringLiteral("802-11-wireless")},
                           {QStringLiteral("autoconnect"), false}}},
                         {QStringLiteral("802-11-wireless"), {{QStringLiteral("ssid"), QByteArray("Fixture Wi-Fi")}}},
                         {QStringLiteral("802-11-wireless-security"),
                          {{QStringLiteral("key-mgmt"), QStringLiteral("wpa-psk")}}}});
        } else if (message.member() == QLatin1String("AddAndActivateConnection")) {
            settings = qdbus_cast<Settings>(message.arguments().at(0));
            result << QVariant::fromValue(QDBusObjectPath(profilePath))
                   << QVariant::fromValue(QDBusObjectPath(manager + QStringLiteral("/ActiveConnection/1")));
        } else if (message.member() == QLatin1String("Update")) {
            settings = qdbus_cast<Settings>(message.arguments().at(0));
        } else if (message.member() == QLatin1String("ActivateConnection")) {
            result << QVariant::fromValue(QDBusObjectPath(manager + QStringLiteral("/ActiveConnection/1")));
        } else if (message.member() == QLatin1String("RequestScan") && denyScan) {
            bus.send(message.createErrorReply(QStringLiteral("org.freedesktop.NetworkManager.PermissionDenied"),
                                              QStringLiteral("fixture refusal")));
            return true;
        }
        if (delayActivation && message.member() == QLatin1String("AddAndActivateConnection")) {
            message.setDelayedReply(true);
            QTimer::singleShot(50, this, [message, bus, result] {
                bus.send(message.createReply(result));
            });
        } else
            bus.send(message.createReply(result));
        return true;
    }
};
}
using namespace NetworkFixture;
class NetworkOperations : public QObject
{
    Q_OBJECT
    NetworkFixture::ManagerFixture fixture;
private Q_SLOTS:
    void initTestCase()
    {
        qputenv("DBUS_SYSTEM_BUS_ADDRESS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
        qDBusRegisterMetaType<Settings>();
        auto bus = QDBusConnection::connectToBus(QString::fromUtf8(qgetenv("DBUS_SESSION_BUS_ADDRESS")),
                                                 QStringLiteral("network-fixture"));
        QVERIFY(bus.registerService(QStringLiteral("org.freedesktop.NetworkManager")));
        QVERIFY(bus.registerVirtualObject(manager, &fixture, QDBusConnection::SubPath));
    }
    void liveDetailsAndProfileIdentity()
    {
        NetworkHost host;
        QTRY_VERIFY(host.available());
        QTRY_COMPARE(host.deviceCount(), 1);
        auto* device = host.deviceAt(0);
        QTRY_COMPARE(device->interfaceName(), QStringLiteral("wlan-test"));
        QTRY_COMPARE(device->bitrate(), 240000u);
        QCOMPARE(device->lastScan(), qint64(1234));
        QVERIFY(host.wirelessHardwareEnabled());
        NetworkConnection profile(profilePath);
        QTRY_COMPARE(profile.ssid(), QStringLiteral("Fixture Wi-Fi"));
        QCOMPARE(profile.id(), QStringLiteral("Custom profile name"));
        QCOMPARE(profile.security(), QStringLiteral("wpa-psk"));
        QVERIFY(!profile.autoConnect());
    }
    void connectionSettingsAndFailures()
    {
        NetworkHost host;
        QTRY_COMPARE(host.deviceCount(), 1);
        AccessPoint ap(apPath);
        QTRY_COMPARE(ap.security(), QStringLiteral("WPA2"));
        QSignalSpy finished(&host, &NetworkHost::operationFinished);
        host.connectToAccessPoint(host.deviceAt(0), &ap, QStringLiteral("fixture-passphrase"), false);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(2).toString(), QString{});
        QCOMPARE(fixture.settings[QStringLiteral("connection")][QStringLiteral("autoconnect")].toBool(), false);
        QCOMPARE(fixture.settings[QStringLiteral("802-11-wireless")][QStringLiteral("ssid")].toByteArray(),
                 QByteArray("Fixture Wi-Fi"));
        QCOMPARE(fixture.settings[QStringLiteral("802-11-wireless-security")][QStringLiteral("key-mgmt")].toString(),
                 QStringLiteral("wpa-psk"));
        host.connectToAccessPoint(host.deviceAt(0), &ap, QStringLiteral("short"));
        QCOMPARE(finished.count(), 2);
        QCOMPARE(finished.at(1).at(2).toString(), QStringLiteral("InvalidPassphrase"));
        fixture.denyScan = true;
        host.scanWifi();
        QTRY_COMPARE(finished.count(), 3);
        QVERIFY(finished.at(2).at(2).toString().endsWith(QStringLiteral("PermissionDenied")));
        host.disconnectDevice(host.deviceAt(0));
        QTRY_COMPARE(finished.count(), 4);
        QVERIFY(fixture.calls.contains(QStringLiteral("Disconnect")));
    }
    void retryUpdatesExistingProfile()
    {
        NetworkHost host;
        QTRY_COMPARE(host.deviceCount(), 1);
        NetworkConnection profile(profilePath);
        AccessPoint ap(apPath);
        QTRY_COMPARE(profile.ssid(), QStringLiteral("Fixture Wi-Fi"));
        QTRY_VERIFY(ap.secured());
        fixture.calls.clear();
        QSignalSpy finished(&host, &NetworkHost::operationFinished);
        host.connectToAccessPoint(host.deviceAt(0), &ap, QStringLiteral("replacement-passphrase"), true, &profile);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(finished.at(0).at(2).toString().isEmpty());
        QVERIFY(fixture.calls.contains(QStringLiteral("Update")));
        QVERIFY(fixture.calls.contains(QStringLiteral("ActivateConnection")));
        QVERIFY(!fixture.calls.contains(QStringLiteral("AddAndActivateConnection")));
        QCOMPARE(fixture.settings[QStringLiteral("connection")][QStringLiteral("id")].toString(),
                 QStringLiteral("Custom profile name"));
        QCOMPARE(fixture.settings[QStringLiteral("802-11-wireless-security")][QStringLiteral("psk")].toString(),
                 QStringLiteral("replacement-passphrase"));
    }
    void cancellationDeactivatesLateReply()
    {
        NetworkHost host;
        QTRY_COMPARE(host.deviceCount(), 1);
        AccessPoint ap(apPath);
        QTRY_VERIFY(ap.secured());
        fixture.calls.clear();
        fixture.delayActivation = true;
        host.connectToAccessPoint(host.deviceAt(0), &ap, QStringLiteral("fixture-passphrase"));
        host.disconnectDevice(host.deviceAt(0));
        QTRY_VERIFY(fixture.calls.contains(QStringLiteral("DeactivateConnection")));
        fixture.delayActivation = false;
    }
};
QTEST_GUILESS_MAIN(NetworkOperations)
#include "test_network_operations.moc"
