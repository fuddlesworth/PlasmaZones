// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AirplaneController.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QProcess>
#include <QTest>
#include <QTimer>

#include <memory>
#include <utility>

using Interfaces = QMap<QString, QVariantMap>;
using Objects = QMap<QDBusObjectPath, Interfaces>;
Q_DECLARE_METATYPE(Interfaces)
Q_DECLARE_METATYPE(Objects)

namespace {
const QString manager = QStringLiteral("org.freedesktop.NetworkManager");
const QString networkPath = QStringLiteral("/org/freedesktop/NetworkManager");
const QString bluez = QStringLiteral("org.bluez");
const QString adapterInterface = QStringLiteral("org.bluez.Adapter1");
const QString properties = QStringLiteral("org.freedesktop.DBus.Properties");
const QString firstAdapter = QStringLiteral("/org/bluez/hci0");
const QString secondAdapter = QStringLiteral("/org/bluez/hci1");

class RadioFixture : public QDBusVirtualObject
{
public:
    explicit RadioFixture(QDBusConnection connection)
        : bus(std::move(connection))
    {
    }
    QDBusConnection bus;
    QVariantMap network{{QStringLiteral("NetworkingEnabled"), true},
                        {QStringLiteral("WirelessEnabled"), true},
                        {QStringLiteral("WirelessHardwareEnabled"), true},
                        {QStringLiteral("WwanEnabled"), true},
                        {QStringLiteral("WwanHardwareEnabled"), true}};
    QMap<QString, QVariantMap> adapters{
        {firstAdapter,
         {{QStringLiteral("Address"), QStringLiteral("00:11:22:33:44:55")}, {QStringLiteral("Powered"), true}}},
        {secondAdapter,
         {{QStringLiteral("Address"), QStringLiteral("00:11:22:33:44:66")}, {QStringLiteral("Powered"), false}}}};
    QStringList writes;
    QString denied;
    bool delayed = false;
    QList<QDBusMessage> held;

    QString introspect(const QString&) const override
    {
        return {};
    }
    void property(const QString& path, const QString& key, bool value)
    {
        const bool nm = path == networkPath;
        if (nm)
            network.insert(key, value);
        else
            adapters[path].insert(key, value);
        auto signal = QDBusMessage::createSignal(path, properties, QStringLiteral("PropertiesChanged"));
        signal << (nm ? manager : adapterInterface) << QVariantMap{{key, value}} << QStringList{};
        bus.send(signal);
    }
    void addAdapter(const QString& path)
    {
        adapters.insert(
            path,
            {{QStringLiteral("Address"), QStringLiteral("00:11:22:33:44:77")}, {QStringLiteral("Powered"), true}});
        auto signal =
            QDBusMessage::createSignal(QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                       QStringLiteral("InterfacesAdded"));
        signal << QVariant::fromValue(QDBusObjectPath(path))
               << QVariant::fromValue(Interfaces{{adapterInterface, adapters.value(path)}});
        bus.send(signal);
    }
    void finishWrite(const QDBusMessage& message)
    {
        const auto key = message.arguments().at(1).toString();
        if (denied == key) {
            bus.send(message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.AccessDenied"),
                                              QStringLiteral("Fixture refusal")));
            return;
        }
        property(message.path(), key, qvariant_cast<QDBusVariant>(message.arguments().at(2)).variant().toBool());
        bus.send(message.createReply());
    }
    void releaseWrites()
    {
        const auto requests = std::exchange(held, {});
        for (const auto& request : requests)
            finishWrite(request);
    }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection&) override
    {
        QVariantList values;
        if (message.member() == QLatin1String("GetAll")) {
            if (message.path() == networkPath)
                values << network;
            else if (adapters.contains(message.path()))
                values << adapters.value(message.path());
            else {
                const bool wifi = message.path().endsWith(QLatin1String("/1"));
                values << QVariantMap{{QStringLiteral("DeviceType"), wifi ? 2u : 8u},
                                      {QStringLiteral("Managed"), true}};
            }
        } else if (message.member() == QLatin1String("GetDevices")) {
            values << QVariant::fromValue(
                QList<QDBusObjectPath>{QDBusObjectPath(networkPath + QStringLiteral("/Devices/1")),
                                       QDBusObjectPath(networkPath + QStringLiteral("/Devices/2"))});
        } else if (message.member() == QLatin1String("GetManagedObjects")) {
            Objects objects;
            for (auto it = adapters.cbegin(); it != adapters.cend(); ++it)
                objects.insert(QDBusObjectPath(it.key()), {{adapterInterface, it.value()}});
            values << QVariant::fromValue(objects);
        } else if (message.member() == QLatin1String("Set")) {
            writes.append(message.arguments().at(1).toString());
            if (delayed) {
                message.setDelayedReply(true);
                held.append(message);
            } else {
                finishWrite(message);
            }
            return true;
        } else if (message.member() == QLatin1String("Enable")) {
            writes.append(QStringLiteral("FORBIDDEN_GLOBAL_ENABLE"));
        }
        bus.send(message.createReply(values));
        return true;
    }
};
}

class TestAirplaneController : public QObject
{
    Q_OBJECT
    QProcess m_daemon;
    QString m_address;
    std::unique_ptr<QDBusConnection> m_service;
    std::unique_ptr<QDBusConnection> m_client;
    std::unique_ptr<RadioFixture> m_fixture;
private Q_SLOTS:
    void initTestCase()
    {
        qDBusRegisterMetaType<Interfaces>();
        qDBusRegisterMetaType<Objects>();
        m_daemon.start(QStringLiteral("dbus-daemon"),
                       {QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address=1")});
        QVERIFY(m_daemon.waitForReadyRead());
        m_address = QString::fromUtf8(m_daemon.readLine()).trimmed();
    }
    void init()
    {
        m_service = std::make_unique<QDBusConnection>(
            QDBusConnection::connectToBus(m_address, QStringLiteral("airplane-service")));
        m_client = std::make_unique<QDBusConnection>(
            QDBusConnection::connectToBus(m_address, QStringLiteral("airplane-client")));
        QVERIFY(m_service->isConnected());
        m_fixture = std::make_unique<RadioFixture>(*m_service);
        QVERIFY(m_service->registerVirtualObject(QStringLiteral("/"), m_fixture.get(), QDBusConnection::SubPath));
        QVERIFY(m_service->registerService(manager));
        QVERIFY(m_service->registerService(bluez));
    }
    void cleanup()
    {
        m_service->unregisterService(manager);
        m_service->unregisterService(bluez);
        m_service->unregisterObject(QStringLiteral("/"), QDBusConnection::UnregisterTree);
        m_fixture.reset();
        m_client.reset();
        m_service.reset();
        QDBusConnection::disconnectFromBus(QStringLiteral("airplane-client"));
        QDBusConnection::disconnectFromBus(QStringLiteral("airplane-service"));
    }
    void cleanupTestCase()
    {
        m_daemon.terminate();
        QVERIFY(m_daemon.waitForFinished());
    }
    void restoresEachRadioAndPreservesWiredNetworking()
    {
        PhosphorShellApp::AirplaneController controller(*m_client);
        QTRY_VERIFY(controller.wifiAvailable());
        QTRY_VERIFY(controller.wwanAvailable());
        QTRY_VERIFY(controller.bluetoothEnabled());
        QVERIFY(m_fixture->writes.isEmpty());
        controller.setEnabled(true);
        QTRY_VERIFY(!controller.pending());
        QVERIFY(controller.enabled());
        QVERIFY(!controller.wifiEnabled());
        QVERIFY(!controller.wwanEnabled());
        QVERIFY(!controller.bluetoothEnabled());
        controller.setEnabled(false);
        QTRY_VERIFY(!controller.pending());
        QVERIFY(!controller.enabled());
        QVERIFY(controller.wifiEnabled());
        QVERIFY(controller.wwanEnabled());
        QVERIFY(controller.bluetoothEnabled());
        QVERIFY(!m_fixture->adapters.value(secondAdapter).value(QStringLiteral("Powered")).toBool());
        QVERIFY(m_fixture->network.value(QStringLiteral("NetworkingEnabled")).toBool());
        QVERIFY(!m_fixture->writes.contains(QStringLiteral("FORBIDDEN_GLOBAL_ENABLE")));
    }
    void bluetoothExceptionAndExternalWifiChange()
    {
        PhosphorShellApp::AirplaneController controller(*m_client);
        QTRY_VERIFY(controller.wifiAvailable());
        QTRY_VERIFY(controller.bluetoothEnabled());
        controller.setEnabled(true);
        QTRY_VERIFY(controller.enabled());
        controller.setBluetoothEnabled(true);
        QTRY_VERIFY(!controller.pending());
        QVERIFY(controller.enabled());
        QVERIFY(controller.bluetoothEnabled());
        m_fixture->property(networkPath, QStringLiteral("WirelessEnabled"), true);
        QTRY_VERIFY(!controller.enabled());
        QVERIFY(!controller.restoreAvailable());
        QVERIFY(!controller.wwanEnabled());
    }
    void partialFailureCanRestoreSuccessfulWrites()
    {
        PhosphorShellApp::AirplaneController controller(*m_client);
        QTRY_VERIFY(controller.wifiAvailable());
        QTRY_VERIFY(controller.bluetoothEnabled());
        m_fixture->denied = QStringLiteral("Powered");
        controller.setEnabled(true);
        QTRY_VERIFY(!controller.pending());
        QVERIFY(!controller.enabled());
        QVERIFY(!controller.error().isEmpty());
        QVERIFY(controller.restoreAvailable());
        QTRY_VERIFY(!controller.wifiEnabled());
        QVERIFY(controller.bluetoothEnabled());
        m_fixture->denied.clear();
        controller.restoreRadios();
        QTRY_VERIFY(!controller.pending());
        QVERIFY(controller.wifiEnabled());
        QVERIFY(controller.wwanEnabled());
        QVERIFY(!controller.restoreAvailable());
        QVERIFY(controller.error().isEmpty());
    }
    void ignoresRepeatedClicksUntilChangesAreConfirmed()
    {
        PhosphorShellApp::AirplaneController controller(*m_client);
        QTRY_VERIFY(controller.wifiAvailable());
        QTRY_VERIFY(controller.bluetoothEnabled());
        m_fixture->delayed = true;
        controller.setEnabled(true);
        controller.setEnabled(true);
        controller.setEnabled(false);
        QTRY_COMPARE(m_fixture->held.size(), 3);
        QVERIFY(controller.pending());
        QVERIFY(!controller.enabled());
        m_fixture->releaseWrites();
        QTRY_VERIFY(!controller.pending());
        QVERIFY(controller.enabled());
        QCOMPARE(m_fixture->writes.size(), 3);
    }
    void hotpluggedAdapterIsDisabledAndRestored()
    {
        PhosphorShellApp::AirplaneController controller(*m_client);
        QTRY_VERIFY(controller.wifiAvailable());
        QTRY_VERIFY(controller.bluetoothEnabled());
        controller.setEnabled(true);
        QTRY_VERIFY(controller.enabled());
        const auto added = QStringLiteral("/org/bluez/hci2");
        m_fixture->addAdapter(added);
        QTRY_VERIFY(!m_fixture->adapters.value(added).value(QStringLiteral("Powered")).toBool());
        QTRY_VERIFY(!controller.pending());
        QVERIFY(controller.enabled());
        controller.setEnabled(false);
        QTRY_VERIFY(!controller.pending());
        QVERIFY(m_fixture->adapters.value(added).value(QStringLiteral("Powered")).toBool());
    }
    void serviceRestartInvalidatesPendingReplies()
    {
        PhosphorShellApp::AirplaneController controller(*m_client);
        QTRY_VERIFY(controller.wifiAvailable());
        QTRY_VERIFY(controller.bluetoothEnabled());
        m_fixture->delayed = true;
        controller.setEnabled(true);
        QTRY_COMPARE(m_fixture->held.size(), 3);
        QVERIFY(m_service->unregisterService(manager));
        QTRY_VERIFY(!controller.pending());
        QVERIFY(!controller.enabled());
        QVERIFY(!controller.error().isEmpty());
        m_fixture->releaseWrites();
        QTest::qWait(30);
        QVERIFY(!controller.enabled());
        QVERIFY(m_service->registerService(manager));
        QTRY_VERIFY(controller.wifiAvailable());
        QVERIFY(!controller.enabled());
        m_fixture->delayed = false;
        controller.restoreRadios();
        QTRY_VERIFY(!controller.pending());
        QVERIFY(controller.wifiEnabled());
    }
    void hardwareBlockedRadioCannotBeEnabled()
    {
        m_fixture->network.insert(QStringLiteral("WirelessEnabled"), false);
        m_fixture->network.insert(QStringLiteral("WirelessHardwareEnabled"), false);
        PhosphorShellApp::AirplaneController controller(*m_client);
        QTRY_VERIFY(controller.wifiAvailable());
        QVERIFY(!controller.wifiHardwareEnabled());
        controller.setWifiEnabled(true);
        QVERIFY(m_fixture->writes.isEmpty());
        QVERIFY(!controller.pending());
    }
};

QTEST_GUILESS_MAIN(TestAirplaneController)
#include "test_airplane_controller.moc"
