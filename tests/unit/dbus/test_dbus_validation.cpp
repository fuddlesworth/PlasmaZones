// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_dbus_validation.cpp
 * @brief Pins the validationError() methods on D-Bus struct types.
 *
 * The D-Bus types carry payloads across a process boundary. Qt's
 * marshaller will decode any byte sequence that matches the signature
 * without checking semantic invariants — so an out-of-range action
 * enum, an empty zoneId on an ApplySnap outcome, or a zero-size tiled
 * request unmarshals silently and corrupts state downstream.
 *
 * validationError() methods enforce the cross-field invariants at the
 * unmarshal boundary. This test fixture pins the accept/reject table
 * so the invariants can't drift as producer code evolves.
 */

#include <QCoreApplication>
#include <QObject>
#include <QTest>

#include "compositor-common/dbus_types.h"

using namespace PlasmaZones;

class TestDbusValidation : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ═════════════════════════════════════════════════════════════════════
    // DragOutcome
    // ═════════════════════════════════════════════════════════════════════

    void dragOutcome_noOp_noRequirements()
    {
        DragOutcome o;
        o.action = DragOutcome::NoOp;
        QVERIFY(o.validationError().isEmpty());
    }

    void dragOutcome_actionOutOfRange_rejected()
    {
        DragOutcome o;
        o.action = 999;
        o.windowId = QStringLiteral("win-1");
        const QString err = o.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("out of range")));
    }

    void dragOutcome_negativeAction_rejected()
    {
        DragOutcome o;
        o.action = -1;
        const QString err = o.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("out of range")));
    }

    void dragOutcome_applyFloatRequiresWindowId()
    {
        DragOutcome o;
        o.action = DragOutcome::ApplyFloat;
        // windowId empty
        const QString err = o.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("windowId required")));
    }

    void dragOutcome_applyFloat_valid()
    {
        DragOutcome o;
        o.action = DragOutcome::ApplyFloat;
        o.windowId = QStringLiteral("win-1");
        // x/y can be zero (top-left corner), targetScreenId optional
        QVERIFY(o.validationError().isEmpty());
    }

    void dragOutcome_applySnapRequiresZoneId()
    {
        DragOutcome o;
        o.action = DragOutcome::ApplySnap;
        o.windowId = QStringLiteral("win-1");
        o.width = 800;
        o.height = 600;
        // zoneId empty
        const QString err = o.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("ApplySnap requires non-empty zoneId")));
    }

    void dragOutcome_applySnapRequiresNonZeroSize()
    {
        DragOutcome o;
        o.action = DragOutcome::ApplySnap;
        o.windowId = QStringLiteral("win-1");
        o.zoneId = QStringLiteral("zone-1");
        o.width = 0;
        o.height = 600;
        const QString err = o.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("non-zero size")));
    }

    void dragOutcome_applySnap_valid()
    {
        DragOutcome o;
        o.action = DragOutcome::ApplySnap;
        o.windowId = QStringLiteral("win-1");
        o.zoneId = QStringLiteral("zone-1");
        o.targetScreenId = QStringLiteral("DP-1");
        o.x = 100;
        o.y = 200;
        o.width = 800;
        o.height = 600;
        QVERIFY(o.validationError().isEmpty());
    }

    void dragOutcome_restoreSizeRequiresNonZeroSize()
    {
        DragOutcome o;
        o.action = DragOutcome::RestoreSize;
        o.windowId = QStringLiteral("win-1");
        o.width = 800;
        o.height = 0;
        const QString err = o.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("non-zero size")));
    }

    void dragOutcome_cancelSnap_noSizeNeeded()
    {
        DragOutcome o;
        o.action = DragOutcome::CancelSnap;
        o.windowId = QStringLiteral("win-1");
        // CancelSnap has no geometry requirement
        QVERIFY(o.validationError().isEmpty());
    }

    void dragOutcome_notifyDragOutUnsnap_requiresWindowId()
    {
        DragOutcome o;
        o.action = DragOutcome::NotifyDragOutUnsnap;
        // windowId empty
        const QString err = o.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("windowId required")));
    }

    // ═════════════════════════════════════════════════════════════════════
    // DragPolicy
    // ═════════════════════════════════════════════════════════════════════

    void dragPolicy_canonicalSnap_valid()
    {
        DragPolicy p;
        p.streamDragMoved = true;
        p.showOverlay = true;
        p.grabKeyboard = true;
        p.captureGeometry = true;
        p.screenId = QStringLiteral("DP-1");
        p.bypassReason = DragBypassReason::None;
        QVERIFY(p.validationError().isEmpty());
    }

    void dragPolicy_autotileBypassRequiresScreenId()
    {
        DragPolicy p;
        p.bypassReason = DragBypassReason::AutotileScreen;
        p.captureGeometry = true;
        // screenId empty
        const QString err = p.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("AutotileScreen bypass requires non-empty screenId")));
    }

    void dragPolicy_autotileBypass_valid()
    {
        DragPolicy p;
        p.bypassReason = DragBypassReason::AutotileScreen;
        p.screenId = QStringLiteral("HP-1");
        p.captureGeometry = true;
        QVERIFY(p.validationError().isEmpty());
    }

    void dragPolicy_snappingDisabledEmptyScreen_tolerated()
    {
        // beginDrag with empty startScreenId returns SnappingDisabled with
        // empty screenId. That path is deliberately tolerated — the drag
        // still needs to complete via endDrag NoOp.
        DragPolicy p;
        p.bypassReason = DragBypassReason::SnappingDisabled;
        p.screenId = QString();
        QVERIFY(p.validationError().isEmpty());
    }

    // ═════════════════════════════════════════════════════════════════════
    // TileRequestEntry
    // ═════════════════════════════════════════════════════════════════════

    void tileRequestEntry_emptyWindowId_rejected()
    {
        TileRequestEntry e;
        e.screenId = QStringLiteral("DP-1");
        e.width = 800;
        e.height = 600;
        const QString err = e.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("empty windowId")));
    }

    void tileRequestEntry_emptyScreenId_rejected()
    {
        TileRequestEntry e;
        e.windowId = QStringLiteral("win-1");
        e.width = 800;
        e.height = 600;
        const QString err = e.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("empty screenId")));
    }

    void tileRequestEntry_negativeSize_rejected()
    {
        TileRequestEntry e;
        e.windowId = QStringLiteral("win-1");
        e.screenId = QStringLiteral("DP-1");
        e.width = -10;
        e.height = 600;
        const QString err = e.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("negative size")));
    }

    void tileRequestEntry_tiledZeroSize_rejected()
    {
        TileRequestEntry e;
        e.windowId = QStringLiteral("win-1");
        e.screenId = QStringLiteral("DP-1");
        e.floating = false;
        e.width = 0;
        e.height = 0;
        const QString err = e.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("tiled request requires non-zero size")));
    }

    void tileRequestEntry_floatingZeroSize_tolerated()
    {
        // Floating requests legitimately carry zero size — the plugin
        // resolves geometry from the current frame.
        TileRequestEntry e;
        e.windowId = QStringLiteral("win-1");
        e.screenId = QStringLiteral("DP-1");
        e.floating = true;
        e.width = 0;
        e.height = 0;
        QVERIFY(e.validationError().isEmpty());
    }

    void tileRequestEntry_tiledValid()
    {
        TileRequestEntry e;
        e.windowId = QStringLiteral("win-1");
        e.screenId = QStringLiteral("DP-1");
        e.zoneId = QStringLiteral("zone-1");
        e.x = 0;
        e.y = 0;
        e.width = 1920;
        e.height = 1080;
        QVERIFY(e.validationError().isEmpty());
    }

    // ═════════════════════════════════════════════════════════════════════
    // BridgeRegistrationResult
    // ═════════════════════════════════════════════════════════════════════

    void bridgeRegistration_valid()
    {
        BridgeRegistrationResult r;
        r.apiVersion = QStringLiteral("2");
        r.bridgeName = QStringLiteral("kwin");
        r.sessionId = QStringLiteral("abcd-1234");
        QVERIFY(r.validationError().isEmpty());
    }

    void bridgeRegistration_rejectedSentinel_tolerated()
    {
        // "REJECTED" is how the daemon signals version mismatch. It's
        // not an invalid result — callers branch on it explicitly.
        BridgeRegistrationResult r;
        r.apiVersion = QStringLiteral("999");
        r.bridgeName = QString(); // sentinel path may leave these empty
        r.sessionId = QStringLiteral("REJECTED");
        QVERIFY(r.validationError().isEmpty());
    }

    void bridgeRegistration_emptyApiVersion_rejected()
    {
        BridgeRegistrationResult r;
        r.bridgeName = QStringLiteral("kwin");
        r.sessionId = QStringLiteral("abcd-1234");
        const QString err = r.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("empty apiVersion")));
    }

    void bridgeRegistration_nonNumericApiVersion_rejected()
    {
        BridgeRegistrationResult r;
        r.apiVersion = QStringLiteral("one");
        r.bridgeName = QStringLiteral("kwin");
        r.sessionId = QStringLiteral("abcd-1234");
        const QString err = r.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("not an integer")));
    }

    void bridgeRegistration_emptyBridgeName_rejected()
    {
        BridgeRegistrationResult r;
        r.apiVersion = QStringLiteral("2");
        r.sessionId = QStringLiteral("abcd-1234");
        const QString err = r.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("empty bridgeName")));
    }

    void bridgeRegistration_emptySessionId_rejected()
    {
        BridgeRegistrationResult r;
        r.apiVersion = QStringLiteral("2");
        r.bridgeName = QStringLiteral("kwin");
        const QString err = r.validationError();
        QVERIFY(!err.isEmpty());
        QVERIFY(err.contains(QStringLiteral("empty sessionId")));
    }

    // ═════════════════════════════════════════════════════════════════════
    // DragBypassReason wire round-trip
    // ═════════════════════════════════════════════════════════════════════

    void dragBypassReason_wireRoundTrip_all()
    {
        // Every enum value must round-trip through the wire format.
        for (auto r : {DragBypassReason::None, DragBypassReason::AutotileScreen, DragBypassReason::SnappingDisabled,
                       DragBypassReason::ContextDisabled}) {
            QCOMPARE(bypassReasonFromWireString(toWireString(r)), r);
        }
    }

    void dragBypassReason_unknownWire_mapsToNone()
    {
        // Unknown wire strings map to None to preserve the legacy
        // fall-through behavior where unrecognized values didn't match
        // the autotile branch.
        QCOMPARE(bypassReasonFromWireString(QStringLiteral("future_reason")), DragBypassReason::None);
        QCOMPARE(bypassReasonFromWireString(QString()), DragBypassReason::None);
    }
};

QTEST_MAIN(TestDbusValidation)
#include "test_dbus_validation.moc"
