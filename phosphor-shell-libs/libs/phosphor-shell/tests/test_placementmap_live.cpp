// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorShell/PlacementMap.h>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using namespace PhosphorProtocol::Service;
using namespace PhosphorShell;

class PlacementService : public QDBusVirtualObject
{
public:
    PlacementService()
        : bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("placement-map-fixture")))
    {
    }
    ~PlacementService() override
    {
        withdraw();
        bus.unregisterObject(ObjectPath);
        QDBusConnection::disconnectFromBus(QStringLiteral("placement-map-fixture"));
    }
    bool publish()
    {
        published = bus.registerVirtualObject(ObjectPath, this) && bus.registerService(Name);
        return published;
    }
    void withdraw()
    {
        if (published)
            bus.unregisterService(Name);
        published = false;
    }
    bool settingsChanged()
    {
        return bus.send(QDBusMessage::createSignal(ObjectPath, Interface::Settings, QStringLiteral("settingsChanged")));
    }
    QString introspect(const QString&) const override
    {
        return {};
    }
    bool handleMessage(const QDBusMessage& message, const QDBusConnection& connection) override
    {
        if (message.interface() == Interface::Screen && message.member() == QLatin1String("getScreenId")) {
            connection.send(message.createReply(QVariantList{message.arguments().value(0).toString()}));
        } else if (message.interface() == Interface::LayoutRegistry
                   && message.member() == QLatin1String("getScreenStates")) {
            QJsonArray states;
            for (const auto& name : {QStringLiteral("DP-1"), QStringLiteral("DP-2")}) {
                states.append(QJsonObject{{QStringLiteral("screenId"), name},
                                          {QStringLiteral("mode"), 1},
                                          {QStringLiteral("layoutsAvailable"), layoutsAvailable}});
            }
            connection.send(message.createReply(
                QVariantList{QString::fromUtf8(QJsonDocument(states).toJson(QJsonDocument::Compact))}));
        } else {
            connection.send(
                message.createErrorReply(QDBusError::UnknownMethod, QStringLiteral("Not used by this fixture")));
        }
        return true;
    }

    bool layoutsAvailable = true;

private:
    QDBusConnection bus;
    bool published = false;
};

class TestPlacementMapLive : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void settingsRefreshCapabilitiesOnEveryOutputAndOwnerLossClearsThem()
    {
        const auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.isConnected());
        if (bus.interface()->isServiceRegistered(Name))
            QSKIP("Requires the isolated test bus; never replace a running daemon");
        PlacementService service;
        QVERIFY(service.publish());
        PlacementMap map;
        auto* first = map.forScreen(QStringLiteral("DP-1"));
        auto* second = map.forScreen(QStringLiteral("DP-2"));
        QTRY_VERIFY(first->layoutsAvailable());
        QTRY_VERIFY(second->layoutsAvailable());
        QSignalSpy firstChanges(first, &PlacementMapScreen::layoutsAvailableChanged);
        QSignalSpy secondChanges(second, &PlacementMapScreen::layoutsAvailableChanged);

        service.layoutsAvailable = false;
        QVERIFY(service.settingsChanged());
        QTRY_VERIFY(!first->layoutsAvailable());
        QTRY_VERIFY(!second->layoutsAvailable());
        QCOMPARE(firstChanges.count(), 1);
        QCOMPARE(secondChanges.count(), 1);
        QCOMPARE(first->mode(), 1);
        QCOMPARE(second->mode(), 1);

        service.layoutsAvailable = true;
        QVERIFY(service.settingsChanged());
        QTRY_VERIFY(first->layoutsAvailable());
        QTRY_VERIFY(second->layoutsAvailable());
        QCOMPARE(firstChanges.count(), 2);
        QCOMPARE(secondChanges.count(), 2);

        service.withdraw();
        QTRY_VERIFY(!map.isAvailable());
        QVERIFY(!first->layoutsAvailable());
        QVERIFY(!second->layoutsAvailable());
        QCOMPARE(firstChanges.count(), 3);
        QCOMPARE(secondChanges.count(), 3);
    }
};

QTEST_MAIN(TestPlacementMapLive)
#include "test_placementmap_live.moc"
