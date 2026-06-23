// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorProtocol/AutotileMarshalling.h>
#include <PhosphorProtocol/BridgeMarshalling.h>
#include <PhosphorProtocol/DragMarshalling.h>
#include <PhosphorProtocol/Registration.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <PhosphorProtocol/WindowTypeEnum.h>
#include <PhosphorProtocol/ZoneMarshalling.h>

#include <QSet>
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

    void testBridgeRegistrationRejectedSentinelIsNotAValidationError()
    {
        // "REJECTED" is the documented version-mismatch sentinel for
        // `sessionId`, not an invariant violation: `validationError()` must
        // stay empty so the sentinel survives the validity gate. Callers
        // detect the rejection by checking the sessionId value separately —
        // see BridgeRegistrationResult::validationError() docs.
        BridgeRegistrationResult r;
        r.sessionId = QStringLiteral("REJECTED");
        QVERIFY(r.validationError().isEmpty());
    }

    void testServiceConstants()
    {
        QCOMPARE(Service::Name, QLatin1String("org.plasmazones"));
        QCOMPARE(Service::ObjectPath, QLatin1String("/PlasmaZones"));
        // Bumped to 4 alongside setWindowMetadata's signature widening
        // (4 args → 9 args) so a stale effect can't silently send the old
        // wire format and crash on marshalling.
        QCOMPARE(Service::ApiVersion, 4);
        QCOMPARE(Service::MinPeerApiVersion, 4);
    }

    // SnapAssistCandidate round-trip is covered by test_compositor_common.

    // ── WindowType enum ──────────────────────────────────────────────────

    void testWindowTypeStringRoundTrip()
    {
        QSet<QString> tokens;
        for (int v = windowTypeMinValue; v <= windowTypeMaxValue; ++v) {
            const auto type = static_cast<WindowType>(v);
            const QString token = windowTypeToString(type);
            const auto parsed = windowTypeFromString(token);
            QVERIFY(parsed.has_value());
            QVERIFY(*parsed == type);
            tokens.insert(token);
        }
        // Every enum value must map to a DISTINCT wire token — a copy-paste
        // bug returning the same token for two values would round-trip one of
        // them to the wrong enum. The set size equals the enum value count.
        QCOMPARE(tokens.size(), windowTypeMaxValue - windowTypeMinValue + 1);
    }

    void testWindowTypeFromStringCaseInsensitive()
    {
        const auto upper = windowTypeFromString(QStringLiteral("DIALOG"));
        QVERIFY(upper.has_value() && *upper == WindowType::Dialog);
        const auto mixed = windowTypeFromString(QStringLiteral("DiAlOg"));
        QVERIFY(mixed.has_value() && *mixed == WindowType::Dialog);
    }

    void testWindowTypeFromStringUnknownTokenIsNullopt()
    {
        QVERIFY(!windowTypeFromString(QStringLiteral("not-a-type")).has_value());
        QVERIFY(!windowTypeFromString(QString()).has_value());
    }

    void testWindowTypeFromIntClampsOutOfRange()
    {
        QVERIFY(windowTypeFromInt(static_cast<int>(WindowType::Dialog)) == WindowType::Dialog);
        QVERIFY(windowTypeFromInt(-1) == WindowType::Unknown);
        QVERIFY(windowTypeFromInt(9999) == WindowType::Unknown);
        QVERIFY(!isValidWindowType(-1));
        QVERIFY(!isValidWindowType(9999));
        QVERIFY(isValidWindowType(static_cast<int>(WindowType::Popup)));
    }
};

QTEST_GUILESS_MAIN(TestPhosphorProtocol)
#include "test_phosphorprotocol.moc"
