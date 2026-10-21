// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_compositor_only_gate.cpp
 * @brief DbusHelpers::admitCompositorReport admits the registered compositor
 *        bridge and no other bus caller (#1155), and registerBridge keeps one
 *        bridge at a time.
 *
 * The adaptors are exported through a BusHost, as Daemon exports them, beside
 * a CompositorBridgeAdaptor, and called by second connections on the private
 * test bus.
 *
 *  1. Before any bridge registered over the bus, every bus caller is refused
 *     with AccessDenied and the slot does nothing; a direct call always runs.
 *  2. The peer that registered is admitted and another is refused.
 *  3. A slot reached from inside another bus call (an ungated slot calling a
 *     gated one) is admitted: the outer slot answers for its caller.
 *  4. A second peer cannot register while the first is on the bus. Once the
 *     first has left, the second registers and the first name is refused.
 *  5. CompositorBridge.reportGesture answers to the same gate.
 */

#include <QDBusAbstractAdaptor>
#include <QDBusConnectionInterface>
#include <QSignalSpy>
#include <QTest>

#include <PhosphorProtocol/BridgeMarshalling.h>
#include <PhosphorProtocol/Registration.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include "dbus/compositorbridgeadaptor.h"
#include "dbus/dbushelpers.h"

#include "helpers/BusPeer.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::BusHost;
using PlasmaZones::TestHelpers::BusPeer;
using PlasmaZones::TestHelpers::isBusError;

namespace CompositorGateTestSupport {

const QString BridgeInterface = QStringLiteral("org.plasmazones.CompositorBridge");
const QString ProbeInterface = QStringLiteral("org.plasmazones.test.GateProbe");

/// An adaptor with one compositor-only slot and one open slot that reaches it.
class GateProbe : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.test.GateProbe")

public:
    explicit GateProbe(QObject* parent)
        : QDBusAbstractAdaptor(parent)
    {
    }
    int reports = 0;

public Q_SLOTS:
    void report()
    {
        if (!DbusHelpers::admitCompositorReport(this, QLatin1StringView(__func__))) {
            return;
        }
        ++reports;
    }
    /// Open to any caller, and reaches the gated slot the way endDrag reaches
    /// windowScreenChanged.
    void outer()
    {
        report();
    }
};

struct Fixture
{
    BusHost host;
    CompositorBridgeAdaptor* bridge = new CompositorBridgeAdaptor(&host);
    GateProbe* probe = new GateProbe(&host);
};

/// Registers @p peer as the bridge over the bus and answers the session id
/// the daemon handed back ("REJECTED" for a refused registration).
QString registerOverBus(BusPeer& peer)
{
    const QDBusMessage reply =
        peer.call(BridgeInterface, QStringLiteral("registerBridge"),
                  {QStringLiteral("kwin"), QString::number(PhosphorProtocol::Service::ApiVersion), QStringList{}});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return QString();
    }
    return qdbus_cast<PhosphorProtocol::BridgeRegistrationResult>(reply.arguments().constFirst()).sessionId;
}

} // namespace CompositorGateTestSupport

using namespace CompositorGateTestSupport;

class TestCompositorOnlyGate : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        PhosphorProtocol::registerWireTypes();
    }

    void beforeRegistrationEveryBusCallerIsRefused()
    {
        Fixture f;
        BusPeer peer(&f.host, QStringLiteral("Early"));
        QVERIFY(peer.ready());

        const QDBusMessage reply = peer.call(ProbeInterface, QStringLiteral("report"));
        QVERIFY2(isBusError(reply, QDBusError::AccessDenied), qPrintable(reply.errorMessage()));
        QCOMPARE(f.probe->reports, 0);

        f.probe->report();
        QCOMPARE(f.probe->reports, 1);
    }

    void theRegisteredPeerIsAdmittedAndAnotherIsRefused()
    {
        Fixture f;
        BusPeer bridgePeer(&f.host, QStringLiteral("Bridge"));
        BusPeer stranger(&f.host, QStringLiteral("Stranger"));
        QVERIFY(bridgePeer.ready() && stranger.ready());
        QVERIFY(registerOverBus(bridgePeer) != QStringLiteral("REJECTED"));

        const QDBusMessage admitted = bridgePeer.call(ProbeInterface, QStringLiteral("report"));
        QCOMPARE(admitted.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(f.probe->reports, 1);

        const QDBusMessage refused = stranger.call(ProbeInterface, QStringLiteral("report"));
        QVERIFY2(isBusError(refused, QDBusError::AccessDenied), qPrintable(refused.errorMessage()));
        QCOMPARE(f.probe->reports, 1);
    }

    void aSlotReachedFromAnotherCallIsAdmitted()
    {
        Fixture f;
        BusPeer stranger(&f.host, QStringLiteral("Outer"));
        QVERIFY(stranger.ready());

        const QDBusMessage reply = stranger.call(ProbeInterface, QStringLiteral("outer"));
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(f.probe->reports, 1);
    }

    void oneBridgeAtATime()
    {
        Fixture f;
        BusPeer first(&f.host, QStringLiteral("First"));
        BusPeer second(&f.host, QStringLiteral("Second"));
        QVERIFY(first.ready() && second.ready());
        const QString firstName = first.service();
        QVERIFY(registerOverBus(first) != QStringLiteral("REJECTED"));

        QCOMPARE(registerOverBus(second), QStringLiteral("REJECTED"));
        QVERIFY(f.bridge->isBridgeService(firstName));
        QVERIFY2(isBusError(second.call(ProbeInterface, QStringLiteral("report")), QDBusError::AccessDenied),
                 "a refused registration admits nothing");

        // The same peer registering again, as the effect does on a reload.
        QVERIFY(registerOverBus(first) != QStringLiteral("REJECTED"));
        QVERIFY(f.bridge->isBridgeService(firstName));

        // Gone from the bus, as a KWin that exited is: the next one registers.
        first.leave();
        QTRY_VERIFY(!QDBusConnection::sessionBus().interface()->isServiceRegistered(firstName));
        QVERIFY(registerOverBus(second) != QStringLiteral("REJECTED"));
        QVERIFY(f.bridge->isBridgeService(second.service()));
        QVERIFY(!f.bridge->isBridgeService(firstName));
        QCOMPARE(second.call(ProbeInterface, QStringLiteral("report")).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(f.probe->reports, 1);
    }

    void aGestureFromAStrangerIsRefused()
    {
        Fixture f;
        BusPeer bridgePeer(&f.host, QStringLiteral("GestureBridge"));
        BusPeer stranger(&f.host, QStringLiteral("GestureStranger"));
        QVERIFY(bridgePeer.ready() && stranger.ready());
        const QDBusMessage registered =
            bridgePeer.call(BridgeInterface, QStringLiteral("registerBridge"),
                            {QStringLiteral("kwin"), QString::number(PhosphorProtocol::Service::ApiVersion),
                             QStringList{QStringLiteral("gestures")}});
        QCOMPARE(registered.type(), QDBusMessage::ReplyMessage);
        QSignalSpy gestures(f.bridge, &CompositorBridgeAdaptor::gestureReported);

        const QVariantList swipe{QStringLiteral("swipe"), QStringLiteral("up"), 3u};
        const QDBusMessage refused = stranger.call(BridgeInterface, QStringLiteral("reportGesture"), swipe);
        QVERIFY2(isBusError(refused, QDBusError::AccessDenied), qPrintable(refused.errorMessage()));
        QCOMPARE(gestures.count(), 0);

        QCOMPARE(bridgePeer.call(BridgeInterface, QStringLiteral("reportGesture"), swipe).type(),
                 QDBusMessage::ReplyMessage);
        QCOMPARE(gestures.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestCompositorOnlyGate)
#include "test_compositor_only_gate.moc"
