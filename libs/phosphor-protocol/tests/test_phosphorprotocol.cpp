// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorProtocol/WireTypes.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <QTest>

using namespace PhosphorProtocol;

class TestPhosphorProtocol : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        registerWireTypes();
    }

    // D-Bus round-trip tests are in test_compositor_common (requires full message transport).

    void testWindowGeometryToRect()
    {
        WindowGeometryEntry e{QStringLiteral("w"), 5, 10, 100, 200, QString()};
        QCOMPARE(e.toRect(), QRect(5, 10, 100, 200));
    }

    void testWindowGeometryFromRect()
    {
        auto e = WindowGeometryEntry::fromRect(QStringLiteral("w"), QRect(1, 2, 3, 4));
        QCOMPARE(e.windowId, QStringLiteral("w"));
        QCOMPARE(e.x, 1);
        QCOMPARE(e.y, 2);
        QCOMPARE(e.width, 3);
        QCOMPARE(e.height, 4);
    }

    // DragPolicy and DragOutcome round-trips are covered by test_compositor_common
    // (they require full D-Bus message transport for nested types like EmptyZoneList).

    void testBypassReasonWireStringRoundTrip()
    {
        QCOMPARE(bypassReasonFromWireString(toWireString(DragBypassReason::None)), DragBypassReason::None);
        QCOMPARE(bypassReasonFromWireString(toWireString(DragBypassReason::AutotileScreen)),
                 DragBypassReason::AutotileScreen);
        QCOMPARE(bypassReasonFromWireString(toWireString(DragBypassReason::SnappingDisabled)),
                 DragBypassReason::SnappingDisabled);
        QCOMPARE(bypassReasonFromWireString(toWireString(DragBypassReason::ContextDisabled)),
                 DragBypassReason::ContextDisabled);
    }

    void testBypassReasonUnknownFallback()
    {
        QCOMPARE(bypassReasonFromWireString(QStringLiteral("bogus")), DragBypassReason::None);
    }

    void testTileRequestValidationEmpty()
    {
        TileRequestEntry e;
        QVERIFY(!e.validationError().isEmpty());
    }

    void testTileRequestValidationValid()
    {
        TileRequestEntry e;
        e.windowId = QStringLiteral("w");
        e.screenId = QStringLiteral("s");
        e.width = 100;
        e.height = 100;
        QVERIFY(e.validationError().isEmpty());
    }

    void testTileRequestValidationFloatingZeroSize()
    {
        TileRequestEntry e;
        e.windowId = QStringLiteral("w");
        e.screenId = QStringLiteral("s");
        e.floating = true;
        e.width = 0;
        e.height = 0;
        QVERIFY(e.validationError().isEmpty());
    }

    void testDragPolicyValidationAutotileNoScreen()
    {
        DragPolicy p;
        p.bypassReason = DragBypassReason::AutotileScreen;
        p.screenId.clear();
        QVERIFY(!p.validationError().isEmpty());
    }

    void testDragOutcomeValidationApplySnapNoZone()
    {
        DragOutcome o;
        o.action = DragOutcome::ApplySnap;
        o.windowId = QStringLiteral("w");
        o.zoneId.clear();
        QVERIFY(!o.validationError().isEmpty());
    }

    void testDragOutcomeValidationNoOp()
    {
        DragOutcome o;
        o.action = DragOutcome::NoOp;
        QVERIFY(o.validationError().isEmpty());
    }

    void testBridgeRegistrationValidation()
    {
        BridgeRegistrationResult r;
        r.apiVersion = QStringLiteral("2");
        r.bridgeName = QStringLiteral("kwin");
        r.sessionId = QStringLiteral("abc");
        QVERIFY(r.validationError().isEmpty());
    }

    void testBridgeRegistrationRejected()
    {
        BridgeRegistrationResult r;
        r.sessionId = QStringLiteral("REJECTED");
        QVERIFY(r.validationError().isEmpty());
    }

    void testServiceConstants()
    {
        QCOMPARE(Service::Name, QLatin1String("org.plasmazones"));
        QCOMPARE(Service::ObjectPath, QLatin1String("/PlasmaZones"));
        QCOMPARE(Service::ApiVersion, 2);
        QCOMPARE(Service::MinPeerApiVersion, 2);
    }

    // SnapAssistCandidate round-trip is covered by test_compositor_common.
};

QTEST_GUILESS_MAIN(TestPhosphorProtocol)
#include "test_phosphorprotocol.moc"
